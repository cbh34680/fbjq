// fs/main.cpp
#include "local.h"
#include <iostream>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <unistd.h>

struct FuseArgsHelper
{
	struct fuse_args args = FUSE_ARGS_INIT(0, nullptr);

	FuseArgsHelper(const char* progname, const std::filesystem::path& mountpoint)
	{
		fuse_opt_add_arg(&args, progname);
        fuse_opt_add_arg(&args, "-f");
#if defined(DEBUG)
        fuse_opt_add_arg(&args, "-d");
#endif
        fuse_opt_add_arg(&args, "-o");
        fuse_opt_add_arg(&args, "allow_other");
        fuse_opt_add_arg(&args, "-o");
        fuse_opt_add_arg(&args, "default_permissions");
        fuse_opt_add_arg(&args, "-o");
        fuse_opt_add_arg(&args, "fsname=fbjq");
        fuse_opt_add_arg(&args, "-o");
        fuse_opt_add_arg(&args, "auto_unmount");
        fuse_opt_add_arg(&args, mountpoint.c_str());
	}

	~FuseArgsHelper()
	{
		fuse_opt_free_args(&args);
	}
};

struct SystemdUnitHelper
{
	bool success = false;
	const libconfig::Config* app_cfg;

	SystemdUnitHelper(const libconfig::Config* app_cfg_arg, const std::filesystem::path spool_dir)
		: app_cfg{ app_cfg_arg }
	{
		namespace fs = std::filesystem;

		// .path ユニットの起動関数
		auto fn_start = [&spool_dir](const libconfig::Setting& q_item) -> bool {
			std::string unit_name{ "fbjq-executor@" };
			unit_name += q_item.getName();
			unit_name += ".path";

			const auto subdir{ spool_dir / "queue" / q_item.getName() };

			if (! fs::exists(subdir)) {
				std::error_code ec;
				fs::create_directory(subdir, ec);

				if (ec) {
					std::cerr << "create directory: " << ec.message() << std::endl;
					return false;
				}
			}

			uid_t uid;
			get_uid_by_name(q_item["exec_user"].c_str(), &uid);

			// 
			if (::chown(subdir.c_str(), uid, 0) != 0) {
				std::cerr << "error: chown" << std::endl;
				return false;
			}
			
			if (::chmod(subdir.c_str(), 0700) != 0) {
				std::cerr << "error: chmod" << std::endl;
				return false;
			}

			return systemd_unit_call_method(unit_name, "StartUnit");
		};

		// .path ユニットの起動
		if (foreach_valid_queue_items(app_cfg, fn_start) <= 0) {
			std::cerr << "有効な queue アイテムが見つかりません。" << std::endl;
			return;
		}

		success = true;
	}

	~SystemdUnitHelper()
	{
		// .path ユニットの停止関数
		auto fn_stop = [](const libconfig::Setting& q_item) -> bool {
			std::string unit_name{ "fbjq-executor@" };
			unit_name += q_item.getName();
			unit_name += ".path";

			return systemd_unit_call_method(unit_name, "StopUnit");
		};

		// .path ユニットの停止
		foreach_valid_queue_items(app_cfg, fn_stop);
	}
};

int main(int argc, char** argv)
{
	namespace fs = std::filesystem;

	::umask(0);

	// 設定ファイルの読み込み
	auto _appConfig{ load_config(argc, argv) };
	if (! _appConfig) {
		return EXIT_FAILURE;
	}
	const auto* app_cfg{ _appConfig.get() };

	fs::path mountpoint{ app_cfg->lookup("mountpoint").c_str() };
	fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };

	// FUSE の引数を生成
	FuseArgsHelper _fuseArgs{ argv[0], mountpoint };
	const auto& args = _fuseArgs.args;

	// FUSE コンテキストの作成
	struct fbjq_context_type app_ctx {
		.cfg = app_cfg,
		//.mountpoint = mountpoint,
		.spool_dir = spool_dir,
		.boot_time = ::time(nullptr),
	};

	// systemd ユニットの起動
	SystemdUnitHelper sdUnit{ app_cfg, spool_dir };
	if (! sdUnit.success) {
		return EXIT_FAILURE;
	}

	// FUSE メインループの開始
	return fuse_main(args.argc, args.argv, fbjq_operations(), &app_ctx);
}
