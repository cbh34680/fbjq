// delivery/args.cpp
#include "local.hpp"
#include <getopt.h>

bool set_app_args(int argc, char** argv, app_args_t* app_args)
{
    ENTER_FUNCTION();

    int opt = 0;
    int option_index = 0;

    // ロングオプションの定義
    const struct option long_options[] = {
        {"check",     no_argument,       nullptr, 'C'},
        {"config",    required_argument, nullptr, 'c'},
        {"max-files", required_argument, nullptr, 'n'},
        {nullptr,     0,                 nullptr, 0  }
    };

    // オプション指定なし（または引数不足）のエラーメッセージを無効化する場合は 0 に設定
    // opterr = 0;

    while ((opt = getopt_long(argc, argv, "Cc:n:", long_options, &option_index)) != -1) {
        switch (opt)
        {
            case 'C':
                app_args->check_only = 1;
                break;

            case 'c':
                app_args->cfg_file = optarg;
                break;

            case 'n':
                app_args->max_files = std::clamp(std::atoi(optarg), 1, 10000);
                break;

            case '?':
                // 未知のオプション、または引数が不足している場合
                LOG_ERROR("Unknown option or missing argument.");
                return false;

            default:
                break;
        }
    }

    return true;
}
