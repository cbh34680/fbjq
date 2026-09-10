// executor/main.cpp
#include "local.hpp"
#include <sys/stat.h>
#include <sys/types.h>

namespace {

int main_(int argc, char** argv)
{
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

    // ブロックしたいシグナルのセットを作成
    sigset_t sigset;
    ::sigemptyset(&sigset);
    ::sigaddset(&sigset, SIGINT);
    ::sigaddset(&sigset, SIGTERM);

    // メインスレッド（および今後生成される全スレッド）でシグナルをブロック
    // スレッドは、親スレッドの sigmask を継承するため、スレッド生成前に pthread_sigmask を呼ぶ
    if (::pthread_sigmask(SIG_BLOCK, &sigset, NULL) != 0) {
        LOG_ERROR("pthread_sigmask error");
        return EXIT_FAILURE;
    }

    fbjqutil::queue_item_view_t queue_item;
    if (! fbjqutil::get_queue_item(app_cfg, app_args.q_name, &queue_item)) {
        LOG_ERROR("get_queue_item");
        return EXIT_FAILURE;
    }

    return for_each_queue_file(&sigset, &app_args, app_cfg);
}

} // namespace

int main(int argc, char** argv)
{
    ENTER_FUNCTION();
    LOG_DEBUG("argv: {}", fbjqutil::join_argv(argc, argv));
    ::umask(0);

    const int rc = main_(argc, argv);
    LOG_INFO("program return-code={}", rc);

    return rc;
}
