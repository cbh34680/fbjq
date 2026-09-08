// executor/main.cpp
#include "local.hpp"
#include <csignal>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

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

    fbjqutil::queue_item_view_t queue_item;
    if (! fbjqutil::get_queue_item(app_cfg, app_args.q_name, &queue_item)) {
        LOG_ERROR("get_queue_item");
        return EXIT_FAILURE;
    }

    sigset_t sigset;

    // 1. ブロックしたいシグナルのセットを作成
    ::sigemptyset(&sigset);
    ::sigaddset(&sigset, SIGINT);
    ::sigaddset(&sigset, SIGTERM);

    // 2. メインスレッド（および今後生成される全スレッド）でシグナルをブロック
    // pthread_create されるスレッドは、親スレッドの sigmask を継承するため、
    // スレッド生成前に pthread_sigmask を呼ぶのがポイントです。
    if (::pthread_sigmask(SIG_BLOCK, &sigset, NULL) != 0) {
        LOG_ERROR("pthread_sigmask error");
        return EXIT_FAILURE;
    }

    //
    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    const fs::path archive_dir{ spool_dir / "archive" };
    const fs::path dead_dir{ spool_dir / "dead" };

    const struct timespec timeout{};

    const auto on_file = [&](const int loop, const auto& entry_path, const char* q_name) -> bool {
        if (loop >= app_args.max_files) {
            // .path の停止を検知するために一定数を処理したら .service を終了する
            LOG_INFO("The maximum number of processes has been reached.");
            return false;
        }

        siginfo_t siginfo;
        const int signo = ::sigtimedwait(&sigset, &siginfo, &timeout);
        if (signo > 0) {
            LOG_INFO("Signal received: {}", signo);
            return false;
        }

        const auto newpath = (q_name ? archive_dir : dead_dir) / entry_path.filename();
        fs::rename(entry_path, newpath);

        return true;
    };

    const auto rc = fbjqutil::for_each_file(app_cfg, spool_dir / "tmp", on_file);
    if (rc < 0) {
        LOG_ERROR("for_each_file: rc={}", rc);
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files", rc);

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    ENTER_FUNCTION();

    ::umask(0);

    const int rc = main_(argc, argv);
    LOG_INFO("program return-code={}", rc);

    return rc;
}
