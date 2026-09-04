// lib/lib.cpp

#include "lib.hpp"
#include <cassert>
#include <exception>
#include <filesystem>
#include <iostream>
#include <alloca.h>
#include <grp.h>
#include <unistd.h>
#include <pwd.h>
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

static bool is_root_directory(const std::filesystem::path& p)
{
    if (p.empty()) {
        return false;
    }

    // パスを正規化して末尾の冗長なスラッシュ等を揃える
    std::filesystem::path norm = p.lexically_normal();

    // 親ディレクトリが自分自身と等しい場合はルートディレクトリ
    return norm.parent_path() == norm;
}

std::unique_ptr<libconfig::Config> load_config(const char* config_file)
{
    // 設定ファイルの読み込み
	auto _appConfig{ std::make_unique<libconfig::Config>() };
    auto* app_cfg{ _appConfig.get() };

	try {
		app_cfg->readFile(config_file);

	} catch (const libconfig::FileIOException &fioex) {
		std::cerr << "設定ファイルの読み込みエラー: " << config_file << std::endl;
		return nullptr;
	} catch (const libconfig::ParseException &pex) {
		std::cerr << "設定ファイルの解析エラー: " << config_file
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

    auto fn_checkdir = [&app_cfg](const char* key) -> bool {
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

    if (! fn_checkdir("mountpoint")) {
        return nullptr;
    }

    if (! fn_checkdir("spool_dir")) {
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

    if (each_queue_items(app_cfg, [](const auto& q_item) { return true; }) <= 0) {
        std::cerr << "有効な queue アイテムが見つかりません。" << std::endl;
        return nullptr;
    }

	return _appConfig;
}

bool is_valid_queue_item(const libconfig::Setting& q_item)
{
	if (q_item.isGroup()
		&& q_item.exists("exec_user") && q_item["exec_user"].isString()
		&& q_item.exists("allow_group") && q_item["allow_group"].isString()
	) {
		if (get_uid_by_name(q_item["exec_user"].c_str(), nullptr) &&
			get_gid_by_name(q_item["allow_group"].c_str(), nullptr)) {

				return true;
		}
	}

	return false;
}

int each_queue_items(const libconfig::Config* cfg, std::function<bool(const libconfig::Setting&)> fn)
{
    int ret = -1;

	if (cfg->exists("queue")) {
		const auto& queue = cfg->lookup("queue");

        ret = 0; // 初期化

		for (int i = 0; i < queue.getLength(); ++i) {
			const auto& q_item = queue[i];

			if (is_valid_queue_item(q_item)) {
				if (! fn(q_item))
                {
                    return -1; // コールバックが false を返した場合、処理を中断して -1 を返す
                }

                ret++; // 有効なアイテムが見つかった場合にカウントを増やす
			}
            else {
                std::cerr << q_item.getName() << ": invalid name" << std::endl;
            }
		}
	}

	return ret;
}

bool systemd_unit_call_method(const std::string& unit_name, const std::string& method)
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