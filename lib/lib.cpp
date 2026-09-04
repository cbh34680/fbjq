// lib/lib.cpp
#include "lib.hpp"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <alloca.h>
#include <unistd.h>
#include <sdbus-c++/sdbus-c++.h>

namespace fbjqlib {

// ナノ秒精度のモノトニックタイムスタンプ
uint64_t now_nanos()
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

std::filesystem::path get_path_from_fd(int fd)
{
    std::string proc_path = "/proc/self/fd/" + std::to_string(fd);
    std::error_code ec;
    
    std::filesystem::path target = std::filesystem::read_symlink(proc_path, ec);
    if (ec) {
        return "";
    }

    return target;
}

// ユーザー名から UID を取得する関数 (成功時: true, 失敗時: false)
bool get_uid_by_name(const char* user_name, uid_t* out_uid)
{
    struct passwd pwd;
    struct passwd* result = nullptr;

    // システムの推奨バッファサイズを取得
    long sys_buflen = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    if (sys_buflen <= 0) {
        sys_buflen = 1024;
    }

    const size_t buflen = static_cast<size_t>(sys_buflen);

    constexpr size_t kMaxStackSize = 4096;
    
    // 警告の原因となる assert 内の文字列結合 (&& "...") をやめて単純比較にする
    assert(buflen <= kMaxStackSize);

    if (buflen > kMaxStackSize) {
        return false;
    }

    char* buf_ptr = static_cast<char*>(::alloca(buflen));

    int res = ::getpwnam_r(user_name, &pwd, buf_ptr, buflen, &result);

    if (res == 0 && result != nullptr) {
        if (out_uid) {
            *out_uid = result->pw_uid;
        }
        return true;
    }

    return false;
}

// グループ名から GID を取得する関数 (成功時: true, 失敗時: false)
bool get_gid_by_name(const char* group_name, gid_t* out_gid)
{
    struct group grp;
    struct group* result = nullptr;

    // システムの推奨バッファサイズを取得
    long sys_buflen = ::sysconf(_SC_GETGR_R_SIZE_MAX);
    if (sys_buflen <= 0) {
        sys_buflen = 1024;
    }

    const size_t buflen = static_cast<size_t>(sys_buflen);

    constexpr size_t kMaxStackSize = 4096;
    
    // 警告の原因となる assert 内の文字列結合 (&& "...") をやめて単純比較にする
    assert(buflen <= kMaxStackSize);

    if (buflen > kMaxStackSize) {
        return false;
    }

    char* buf_ptr = static_cast<char*>(::alloca(buflen));

    int res = ::getgrnam_r(group_name, &grp, buf_ptr, buflen, &result);

    if (res == 0 && result != nullptr) {
        if (out_gid) {
            *out_gid = result->gr_gid;
        }

        return true;
    }

    return false;
}

static bool is_root_directory(const std::filesystem::path& path)
{
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
    // 設定ファイルの読み込み
	auto appConfigPtr{ std::make_unique<libconfig::Config>() };
    auto* app_cfg{ appConfigPtr.get() };

	try {
		app_cfg->readFile(cfg_file);

	} catch (const libconfig::FileIOException &fioex) {
		std::cerr << "設定ファイルの読み込みエラー: " << cfg_file << std::endl;
		return nullptr;
	} catch (const libconfig::ParseException &pex) {
		std::cerr << "設定ファイルの解析エラー: " << cfg_file
				  << " 行: " << pex.getLine()
				  << " エラー: " << pex.getError() << std::endl;
		return nullptr;
	} catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "unknown error" << std::endl;
        return nullptr;
    }

    std::string path_str;
    if (! app_cfg->lookupValue("spool_dir", path_str)) {
        std::cerr << "設定ファイルに 'spool_dir' が見つかりません。" << std::endl;
        return nullptr;
    }

    std::filesystem::path spool_dir{ path_str };

    if (spool_dir.empty()) {
        std::cerr << "設定ファイルの '" << spool_dir << "' が空です。" << std::endl;
        return nullptr;
    }

    if (is_root_directory(spool_dir)) {
        std::cerr << "設定ファイルの '" << spool_dir << "' がルートディレクトリです。" << std::endl;
        return nullptr;
    }

    if (! std::filesystem::is_directory(spool_dir)) {
        std::cerr << "`" << spool_dir << "': not directory" << std::endl;
        return nullptr;
    }

    const char* subdirs[] = { "tmp", "delivery", "dead", "queue", nullptr };
    const char** subdir = subdirs;

    for (; *subdir; ++subdir) {
        if (! std::filesystem::exists(spool_dir / *subdir)) {
            std::cerr << (spool_dir / *subdir) << ": not found" << std::endl;
            return nullptr;
        }
    }

    const auto noop = [](const char* q_name, const auto& q_item) {
        (void) q_name;
        (void) q_item;

        return true;
    };

    if (for_each_queue_item(app_cfg, noop) <= 0) {
        std::cerr << "有効な queue アイテムが見つかりません。" << std::endl;
        return nullptr;
    }

	return appConfigPtr;
}

static bool get_queue_item_internal(const libconfig::Setting &q_item, queue_item_t* out)
{
    const char* exec_user = nullptr;
    if (! q_item.lookupValue("exec_user", exec_user)) {
        return false;
    }

    const char* allow_group = nullptr;
    if (! q_item.lookupValue("allow_group", allow_group)) {
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
        return false;
    }

    gid_t allow_group_gid;
    if (! fbjqlib::get_gid_by_name(allow_group, &allow_group_gid)) {
        return false;
    }

    out->exec_user= exec_user;
    out->allow_group = allow_group;
    out->exec_user_uid = exec_user_uid;
    out->allow_group_gid = allow_group_gid;
    out->max_process = max_process;

    return true;
}

bool get_queue_item(const libconfig::Config* app_cfg, const char* q_name, queue_item_t* out)
{
    if (! app_cfg->exists("queue")) {
        return false;
    }

    const auto& queue = app_cfg->lookup("queue");
    if (! queue.isGroup()) {
        return false;
    }

    if (! queue.exists(q_name)) {
        return false;
    }

     if (! get_queue_item_internal(queue[q_name], out)) {
        return false;
    }

    return true;
}

int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char*, const queue_item_t&)> callback)
{
    int item_count = -1;

	if (app_cfg->exists("queue")) {
		const auto& queue = app_cfg->lookup("queue");

        if (queue.isGroup()) {
            item_count = 0; // 初期化

            const int q_len = queue.getLength();

            for (int i = 0; i < q_len; ++i) {
                const char* q_name = queue[i].getName();

                const auto q_name_len = std::strlen(q_name);
                if (q_name_len <= 0 || q_name_len > QUEUE_NAME_MAXLEN) {
                    std::cerr << q_name << ": invalid name length" << std::endl;
                    continue;
                }

                queue_item_t q_item;
                if (! get_queue_item_internal(queue[i], &q_item)) {
                    std::cerr << q_name << ": invalid name" << std::endl;
                    continue;
                }

                if (! callback(q_name, q_item)) {
                    // コールバックが false を返した場合、処理を中断して -1 を返す
                    return -1;
                }

                item_count++; // 有効なアイテムが見つかった場合にカウントを増やす
            }
        }
	}

	return item_count;
}

bool call_systemd_unit_method(const std::string& unit_name, const std::string& method)
{
    try {
        auto proxy = sdbus::createProxy(
            sdbus::createSystemBusConnection(),
            sdbus::ServiceName{"org.freedesktop.systemd1"},
            sdbus::ObjectPath{"/org/freedesktop/systemd1"},
            sdbus::dont_run_event_loop_thread);

        sdbus::ObjectPath job;

        proxy->callMethod(method)
            .onInterface("org.freedesktop.systemd1.Manager")
            .withArguments(unit_name, "replace")
            .storeResultsTo(job);

        std::cout << job << '\n';

        return true;
    }
    catch (const sdbus::Error& e) {
        std::cerr << "sbus error: " << e.what() << std::endl;
        return false;
	} catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "unknown error" << std::endl;
        return false;
    }
}

} // namespace
