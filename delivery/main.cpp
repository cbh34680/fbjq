// delivery/main.cpp
#include "local.hpp"
#include <csignal>
#include <atomic>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

std::atomic<bool> g_graceful_stop{ false };

void signal_handler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT) {
        g_graceful_stop = true; // ループを抜けるフラグを立てる
    }
}

int main_(int argc, char** argv)
{
    ENTER_FUNCTION();
    constexpr int DEFAULT_MAX_FILES = 500;

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

    struct
    {
        int check_only{ 0 };
        const char* cfg_file{ fbjqutil::DEFAULT_CONFIG_FILE };
        int max_files{ DEFAULT_MAX_FILES };

        std::string string() {
            return std::format("check_only={}, cfg_file={}, max_files={}", check_only, cfg_file, max_files);
        }
    }
    app_args;

    while ((opt = getopt_long(argc, argv, "Cc:n:", long_options, &option_index)) != -1) {
        switch (opt)
        {
            case 'C':
                app_args.check_only = 1;
                break;

            case 'c':
                app_args.cfg_file = optarg;
                break;

            case 'n':
                app_args.max_files = std::clamp(std::atoi(optarg), 1, 10000);
                break;

            case '?':
                // 未知のオプション、または引数が不足している場合
                LOG_ERROR("Unknown option or missing argument.");
                return 1;

            default:
                break;
        }
    }

    LOG_INFO("args: {}", app_args.string());

    // 設定ファイルの読み込み
    auto appConfigPtr{ fbjqutil::load_config(app_args.cfg_file) };
    if (appConfigPtr) {
        if (app_args.check_only) {
            LOG_INFO("config check ok");
            return EXIT_SUCCESS;
        }

    } else {
        LOG_ERROR("load_config");
        return EXIT_FAILURE;
    }

    // ハンドラ関数の登録
    struct sigaction sa{};
    sa.sa_handler = signal_handler;

    // シグナル処理中に他のシグナルをブロックするためのマスクをクリア
    ::sigemptyset(&sa.sa_mask);

    // SA_RESTART: シグナル割り込みによって中断されたシステムコールを自動再開する
    sa.sa_flags = SA_RESTART;

    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);

    //
    const auto should_continue = [&](const auto& entry, const int moved) -> bool {
        (void) entry;

        if (moved >= app_args.max_files) {
            // .path の停止を検知するために一定数を処理したら .service を終了する
            LOG_INFO("The maximum number of processes has been reached.");
            return false;
        }

        if (g_graceful_stop) {
            LOG_INFO("receive signal, graceful stop");
            return false;
        }

        return true;
    };

    const auto moved = for_each_delivery_file(appConfigPtr.get(), should_continue);
    if (moved < 0) {
        LOG_ERROR("for_each_delivery_file: moved={}", moved);
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files, g_graceful_stop={}", moved, g_graceful_stop.load());

    return EXIT_SUCCESS;
}

} //namespace

int main(int argc, char** argv)
{
    ENTER_FUNCTION();

    ::umask(0);

    const int rc = main_(argc, argv);
    LOG_INFO("program return-code={}", rc);

    return rc;
}
