// delivery/main.cpp
#include "lib.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <getopt.h>


int main(int argc, char** argv)
{
	namespace fs = std::filesystem;

	int opt = 0;
    int option_index = 0;

    // ロングオプションの定義
    static struct option long_options[] = {
        {"check",   no_argument,       nullptr, 'C'},
        {"config",  required_argument, nullptr, 'c'},
        {nullptr,   0,                 nullptr, 0  }
    };

    // オプション指定なし（または引数不足）のエラーメッセージを無効化する場合は 0 に設定
    // opterr = 0;

	struct app_args_t
	{
		int check_only{ 0 };
		const char* cfg_file{ fbjqlib::DEFAULT_CONFIG_FILE };
	};

	app_args_t app_args;

    while ((opt = getopt_long(argc, argv, "Cc:", long_options, &option_index)) != -1) {
        switch (opt)
		{
			case 'C':
				app_args.check_only = 1;
				break;

			case 'c':
				app_args.cfg_file = optarg;
				break;

			case '?':
				// 未知のオプション、または引数が不足している場合
				std::cerr << "Unknown option or missing argument." << std::endl;
				return 1;

			default:
				break;
        }
    }

	// 設定ファイルの読み込み
	auto appConfigPtr{ fbjqlib::load_config(app_args.cfg_file) };
	if (appConfigPtr) {
		if (app_args.check_only) {
			std::cerr << "Config OK";
			return EXIT_SUCCESS;
		}
	} else {
		std::cerr << "Config Error";
		return EXIT_FAILURE;
	}

	const auto* app_cfg{ appConfigPtr.get() };
	fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };

	
	
	return 0;
}
