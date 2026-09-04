// lib/lib.cpp

#include "lib.hpp"
#include <cassert>
#include <exception>
#include <filesystem>
#include <iostream>
#include <alloca.h>
#include <unistd.h>
#include <sdbus-c++/sdbus-c++.h>

namespace fbjqlib {

// ユーザ名から UID を取得する関数 (成功時: true, 失敗時: false)
bool get_uid_by_name(const char* user_name, uid_t* out_uid)
{
    struct passwd pwd;
    struct passwd* result = nullptr;
    
    // システムの推奨バッファサイズを取得 (取得できない場合はデフォルト値を採用)
    long buflen = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen == -1) {
        buflen = 1024;
    }

    constexpr size_t kMaxStackSize = 4096;
    assert(static_cast<size_t>(buflen) <= kMaxStackSize && "Buffer size exceeds 4KB limit");

    char* buf_ptr = static_cast<char*>(::alloca(buflen));

    int res = ::getpwnam_r(user_name, &pwd, buf_ptr, buflen, &result);

    if (res == 0 && result != nullptr) {
        if (out_uid) {
            *out_uid = result->pw_uid;
        }
        return true;
    }

    return false; // エラーまたはユーザーが存在しない場合
}

// グループ名から GID を取得する関数 (成功時: true, 失敗時: false)
bool get_gid_by_name(const char* group_name, gid_t* out_gid)
{
    struct group grp;
    struct group* result = nullptr;
    
    // システムの推奨バッファサイズを取得 (取得できない場合はデフォルト値を採用)
    long buflen = ::sysconf(_SC_GETGR_R_SIZE_MAX);
    if (buflen == -1) {
        buflen = 1024;
    }

    constexpr size_t kMaxStackSize = 4096;
    assert(static_cast<size_t>(buflen) <= kMaxStackSize && "Buffer size exceeds 4KB limit");

    char* buf_ptr = static_cast<char*>(::alloca(buflen));

    int res = ::getgrnam_r(group_name, &grp, buf_ptr, buflen, &result);

    if (res == 0 && result != nullptr) {
        if (out_gid) {
            *out_gid = result->gr_gid;
        }

        return true;
    }

    return false; // エラーまたはグループが存在しない場合
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

    auto checkdir = [&app_cfg](const char* key) -> bool {
        if (! app_cfg->exists(key)) {
            std::cerr << "設定ファイルに '" << key << "' が見つかりません。" << std::endl;
            return false;
        }

        std::filesystem::path path{ app_cfg->lookup(key).c_str() };

        if (path.empty()) {
            std::cerr << "設定ファイルの '" << key << "' が空です。" << std::endl;
            return false;
        }

        if (is_root_directory(path)) {
            std::cerr << "設定ファイルの '" << key << "' がルートディレクトリです。" << std::endl;
            return false;
        }

        if (! std::filesystem::is_directory(path)) {
            std::cerr << "`" << path << "': not directory" << std::endl;
            return false;
        }

        return true;
    };

    if (! checkdir("mountpoint")) {
        return nullptr;
    }

    if (! checkdir("spool_dir")) {
        return nullptr;
    }

    std::filesystem::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };

    const char* subdirs[] = { "tmp", "delivery", "dead", "queue", nullptr };
    const char** subdir = subdirs;

    for (; *subdir; ++subdir) {
        if (! std::filesystem::exists(spool_dir / *subdir)) {
            std::cerr << (spool_dir / *subdir) << ": not found" << std::endl;
            return nullptr;
        }
    }

    auto noop = [](const char* q_name, const auto& q_item) {
        (void)q_name;
        (void)q_item;
        
        return true;
    };

    if (for_each_queue_item(app_cfg, noop) <= 0) {
        std::cerr << "有効な queue アイテムが見つかりません。" << std::endl;
        return nullptr;
    }

	return appConfigPtr;
}

static bool get_queue_item_internal(const libconfig::Setting &q_item, queue_item* out)
{
    const char* exec_user = nullptr;
    if (! q_item.lookupValue("exec_user", exec_user)) {
        return false;
    }

    const char* allow_group = nullptr;
    if (! q_item.lookupValue("allow_group", allow_group)) {
        return false;
    }

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

    return true;
}

bool get_queue_item(const libconfig::Config* app_cfg, const char* q_name, queue_item* out) {

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

int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char*, const queue_item&)> callback)
{
    int item_count = -1;

	if (app_cfg->exists("queue")) {
		const auto& queue = app_cfg->lookup("queue");

        if (queue.isGroup()) {
            item_count = 0; // 初期化

            const int q_len = queue.getLength();

            for (int i = 0; i < q_len; ++i) {
                const char* q_name = queue[i].getName();

                queue_item q_item;
                if (! get_queue_item_internal(queue[i], &q_item)) {
                    std::cerr << q_name << ": invalid name" << std::endl;
                    continue;
                }

                if (! callback(q_name, q_item))
                {
                    return -1; // コールバックが false を返した場合、処理を中断して -1 を返す
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
            .withArguments(
                unit_name,
                "replace")
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