// lib/lib.cpp
#include "lib.hpp"
#include <cassert>
#include <ctime>
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <alloca.h>
#include <unistd.h>
#include <sdbus-c++/sdbus-c++.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

namespace fbjqlib {

// ナノ秒精度のモノトニックタイムスタンプ
std::int64_t now_nanos()
{
    ENTER_FUNCTION();

    struct std::timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);

    // ナノ秒（Epochからの通算ナノ秒）
    return static_cast<std::int64_t>(ts.tv_sec) * std::int64_t{ 1000000000 } + ts.tv_nsec;
}

bool get_path_from_fd(int fd, char* buf, size_t buf_siz)
{
    ENTER_FUNCTION();

    if (buf == nullptr || buf_siz == 0) {
        LOG_ERROR("invalid params");
        return false;
    }

    char proc_path[PATH_MAX];
    std::snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);

    const auto len = ::readlink(proc_path, buf, buf_siz - 1);
    if (len == -1) {
        LOG_ERROR("{}: readlink", proc_path);
        return false;
    }

    buf[len] = '\0';

    return true;
}

// ユーザー名から UID を取得する関数 (成功時: true, 失敗時: false)
bool get_uid_by_name(const char* user_name, uid_t* out_uid)
{
    ENTER_FUNCTION();

    struct passwd pwd;
    struct passwd* result = nullptr;

    char buf[1024];
    int res = ::getpwnam_r(user_name, &pwd, buf, sizeof(buf), &result);

    if (res == 0 && result != nullptr) {
        if (out_uid) {
            *out_uid = result->pw_uid;
        }

        return true;
    }

    LOG_ERROR("getpwnam_r");

    return false;
}

// グループ名から GID を取得する関数 (成功時: true, 失敗時: false)
bool get_gid_by_name(const char* group_name, gid_t* out_gid)
{
    ENTER_FUNCTION();

    struct group grp;
    struct group* result = nullptr;

    char buf[1024];
    int res = ::getgrnam_r(group_name, &grp, buf, sizeof(buf), &result);

    if (res == 0 && result != nullptr) {
        if (out_gid) {
            *out_gid = result->gr_gid;
        }

        return true;
    }

    LOG_ERROR("getgrnam_r");

    return false;
}

static bool is_root_directory(const std::filesystem::path& path)
{
    ENTER_FUNCTION();

    if (path.empty()) {
        return false;
    }

    // パスを正規化して末尾の冗長なスラッシュ等を揃える
    std::filesystem::path norm = path.lexically_normal();

    // 親ディレクトリが自分自身と等しい場合はルートディレクトリ
    return norm.parent_path() == norm;
}

std::unique_ptr<libconfig::Config> load_config(const char* cfg_file)
{
    ENTER_FUNCTION();

    // 設定ファイルの読み込み
    auto appConfigPtr{ std::make_unique<libconfig::Config>() };
    auto* app_cfg{ appConfigPtr.get() };

    try {
        app_cfg->readFile(cfg_file);

    }/* catch (const libconfig::FileIOException &fioex) {
        std::cerr << "設定ファイルの読み込みエラー: " << cfg_file << std::endl;
        return nullptr;
    } catch (const libconfig::ParseException &pex) {
        std::cerr << "設定ファイルの解析エラー: " << cfg_file
                  << " 行: " << pex.getLine()
                  << " エラー: " << pex.getError() << std::endl;
        return nullptr;
    }*/ catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return nullptr;
    } catch (...) {
        LOG_ERROR("unknown");
        return nullptr;
    }

    std::string path_str;
    if (! app_cfg->lookupValue("spool_dir", path_str)) {
        LOG_ERROR("spool_dir: not exists");
        return nullptr;
    }

    std::filesystem::path spool_dir{ path_str };

    if (spool_dir.empty()) {
        LOG_ERROR("spool_dir: empty");
        return nullptr;
    }

    if (! std::filesystem::is_directory(spool_dir)) {
        LOG_ERROR("{}: not directory", spool_dir.string());
        return nullptr;
    }

    if (is_root_directory(spool_dir)) {
        LOG_ERROR("spool_dir: root directory");
        return nullptr;
    }

    const char* subdirs[] = { "tmp", "delivery", "dead", "queue", nullptr };
    const char** subdir = subdirs;

    for (; *subdir; ++subdir) {
        const auto path{ spool_dir / *subdir };

        if (! std::filesystem::is_directory(path)) {
            LOG_ERROR("{}: not directory", path.string());
            return nullptr;
        }
    }

    const auto noop = [](const char* q_name, const auto& q_item) {
        (void) q_name;
        (void) q_item;

        return true;
    };

    if (for_each_queue_item(app_cfg, noop) <= 0) {
        LOG_ERROR("no queue item");
        return nullptr;
    }

    return appConfigPtr;
}

static bool get_queue_item_internal(const libconfig::Setting &q_item, queue_item_view_t* out)
{
    ENTER_FUNCTION();

    const char* exec_user = nullptr;
    if (! q_item.lookupValue("exec_user", exec_user)) {
        LOG_ERROR("exec_user: no key");
        return false;
    }

    const char* allow_group = nullptr;
    if (! q_item.lookupValue("allow_group", allow_group)) {
        LOG_ERROR("allow_group: no key");
        return false;
    }

    int max_process = 1;
    q_item.lookupValue("max_process", max_process);

    /*
    if (max_process <= 0) {
        max_process = 1;
    } else if (max_process > QUEUE_MAX_PROCESS) {
        max_process = 32;
    }
    */
    max_process = std::clamp(max_process, 1, QUEUE_MAX_PROCESS);

    uid_t exec_user_uid;
    if (! fbjqlib::get_uid_by_name(exec_user, &exec_user_uid)) {
        LOG_ERROR("get_uid_by_name");
        return false;
    }

    gid_t allow_group_gid;
    if (! fbjqlib::get_gid_by_name(allow_group, &allow_group_gid)) {
        LOG_ERROR("get_gid_by_name");
        return false;
    }

    if (out) {
        out->exec_user= exec_user;
        out->allow_group = allow_group;
        out->exec_user_uid = exec_user_uid;
        out->allow_group_gid = allow_group_gid;
        out->max_process = max_process;
    }

    return true;
}

bool get_queue_item(const libconfig::Config* app_cfg, const char* q_name, queue_item_view_t* out)
{
    ENTER_FUNCTION();

    const auto q_name_len = std::strlen(q_name);
    if (q_name_len <= 0 || q_name_len > QUEUE_NAME_MAXLEN) {
        LOG_ERROR("{}: invalid q_name length", q_name);
        return false;
    }

    if (! app_cfg->exists("queue")) {
        LOG_ERROR("queue: no key");
        return false;
    }

    const auto& queue = app_cfg->getRoot()["queue"];
    if (! queue.isGroup()) {
        LOG_ERROR("queue: not group");
        return false;
    }

    if (! queue.exists(q_name)) {
        LOG_ERROR("{}: no key", q_name);
        return false;
    }

     if (! get_queue_item_internal(queue[q_name], out)) {
        LOG_ERROR("get_queue_item_internal");
        return false;
    }

    return true;
}

int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char*, const queue_item_view_t&)> callback)
{
    ENTER_FUNCTION();

    if (! app_cfg->exists("queue")) {
        LOG_ERROR("queue: no key");
        return -1;
    }

    const auto& queue = app_cfg->getRoot()["queue"];
    if (! queue.isGroup()) {
        LOG_ERROR("queue: not group");
        return -1;
    }

    int item_count = 0;
    const int q_len = queue.getLength();

    for (int i = 0; i < q_len; ++i) {
        const char* q_name = queue[i].getName();
        const auto q_name_len = std::strlen(q_name);

        if (q_name_len <= 0 || q_name_len > QUEUE_NAME_MAXLEN) {
            LOG_ERROR("queue[{}]: {}: invalid q_name length", i, q_name);
            return -1;
        }

        queue_item_view_t q_item;
        if (! get_queue_item_internal(queue[i], &q_item)) {
            LOG_ERROR("queue[{}]: {}: get_queue_item_internal", i, q_name);
            return -1;
        }

        if (! callback(q_name, q_item)) {
            LOG_ERROR("queue[{}]: {}: callback", i, q_name);

            // コールバックが false を返した場合、処理を中断して -1 を返す
            return -1;
        }

        item_count++; // 有効なアイテムが見つかった場合にカウントを増やす
    }

    LOG_DEBUG("item_count={}", item_count);
    return item_count;
}

bool call_systemd_unit_method(const std::string& unit_name, const std::string& method)
{
    ENTER_FUNCTION();
    
    try {
        auto proxy = sdbus::createProxy(
            sdbus::createSystemBusConnection(),
            sdbus::ServiceName{"org.freedesktop.systemd1"},
            sdbus::ObjectPath{"/org/freedesktop/systemd1"},
            sdbus::dont_run_event_loop_thread
        );

        sdbus::ObjectPath job;

        proxy->callMethod(method)
            .onInterface("org.freedesktop.systemd1.Manager")
            .withArguments(unit_name, "replace")
            .storeResultsTo(job);

        LOG_INFO("unit={} method={} job={}", unit_name, method, job.c_str());

        return true;
    }
    /*
    catch (const sdbus::Error& e) {
        std::cerr << "sbus error: " << e.what() << std::endl;
        return false;
    }*/ catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return false;
    } catch (...) {
        LOG_ERROR("exception unknown");
        return false;
    }
}

} // namespace
