// fs/main.cpp

#include "local.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
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
        fuse_opt_add_arg(&args, "fsname=fbjq-fs");
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

	SystemdUnitHelper(const libconfig::Config* app_cfg_arg, const std::filesystem::path& spool_dir)
		: app_cfg{ app_cfg_arg }
	{
		namespace fs = std::filesystem;

		// .path ユニットの起動関数
		auto fn_start = [&spool_dir](const auto& q_item) -> bool {
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
			fbjqlib::get_uid_by_name(q_item["exec_user"].c_str(), &uid);

			// 
			if (::chown(subdir.c_str(), uid, 0) != 0) {
				std::cerr << "error: chown" << std::endl;
				return false;
			}
			
			if (::chmod(subdir.c_str(), 0700) != 0) {
				std::cerr << "error: chmod" << std::endl;
				return false;
			}

			return fbjqlib::systemd_unit_call_method(unit_name, "StartUnit");
		};

		// .path ユニットの起動
		fbjqlib::each_queue_items(app_cfg, fn_start);

		success = true;
	}

	~SystemdUnitHelper()
	{
		// .path ユニットの停止関数
		//auto fn_stop = [](const libconfig::Setting& q_item) -> bool {
		auto fn_stop = [](const auto& q_item) -> bool {
			std::string unit_name{ "fbjq-executor@" };
			unit_name += q_item.getName();
			unit_name += ".path";

			return fbjqlib::systemd_unit_call_method(unit_name, "StopUnit");
		};

		// .path ユニットの停止
		fbjqlib::each_queue_items(app_cfg, fn_stop);
	}
};

int main(int argc, char** argv)
{
	namespace fs = std::filesystem;

	int opt;
	const char* config_file = fbjqlib::DEFAULT_CONFIG_FILE;
	bool check_only = false;

	while ((opt = ::getopt(argc, argv, "cf:")) != -1) {
        switch (opt) {
			case 'c':
				check_only = true;
				break;
            case 'f':
                config_file = optarg;
                break;

            default:
                // 不明なオプション、または引数が不足している場合
                fprintf(stderr, "使用方法: %s [-f config_file]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    // optind をリセットして、後続のコードで再度 getopt を使用できるようにする
	optind = 1;

	::umask(0);

	// 設定ファイルの読み込み
	auto _appConfig{ fbjqlib::load_config(config_file) };
	if (_appConfig) {
		if (check_only) {
			std::cerr << "Config OK";
			return EXIT_SUCCESS;
		}
	} else {
		std::cerr << "Config Error";
		return EXIT_FAILURE;
	}

	const auto* app_cfg{ _appConfig.get() };
	fs::path mountpoint{ app_cfg->lookup("mountpoint").c_str() };
	fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };

	// FUSE の引数を生成
	FuseArgsHelper _fuseArgs{ argv[0], mountpoint };
	const auto& args = _fuseArgs.args;

	// FUSE コンテキストの作成
	struct fbjqlib::context_type app_ctx {
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
