// delivery/main.cpp
#include "local.hpp"
#include <csignal>
#include <atomic>
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
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    app_args_t app_args;
    if (! set_app_args(argc, argv, &app_args)) {
        LOG_ERROR("set_app_args");
        return EXIT_FAILURE;
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

    const auto* app_cfg = appConfigPtr.get();

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
    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    const fs::path queue_dir{ spool_dir / "queue" };
    const fs::path dead_dir{ spool_dir / "dead" };

    const auto on_regular_file = [&](const int loop, const auto& entry_path, const char* q_name) -> bool {
        if (loop >= app_args.max_files) {
            // .path の停止を検知するために一定数を処理したら .service を終了する
            LOG_INFO("The maximum number of processes has been reached.");
            return false;
        }

        if (g_graceful_stop) {
            LOG_INFO("receive signal, graceful stop");
            return false;
        }

        const auto newpath = (q_name ? queue_dir / q_name : dead_dir) / entry_path.filename();
        fs::rename(entry_path, newpath);

        return true;
    };

    const auto rc = fbjqutil::for_each_file(app_cfg, spool_dir / "delivery", on_regular_file);
    if (rc < 0) {
        LOG_ERROR("for_each_file: rc={}", rc);
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files, g_graceful_stop={}", rc, g_graceful_stop.load());

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
