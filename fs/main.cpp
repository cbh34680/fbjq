// fs/main.cpp
#include "local.hpp"

namespace {

int main_(int argc, char** argv)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    FuseArgsHelper fuseArgs{ argc, argv };
    struct fuse_args& args = fuseArgs.args;
    app_args_t app_args;

    if (! set_app_args(&args, &app_args)) {
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

    LOG_INFO("load_config config={}", app_args.cfg_file);

    const auto* app_cfg = appConfigPtr.get();
    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    // FUSE コンテキストの作成
    app_context_t app_ctx = {
        .app_cfg = app_cfg,
        .spool_dir = spool_dir,
        .boot_time = std::time(nullptr),
    };

    // systemd ユニットの起動
    SystemdUnitHelper sdUnit{ app_cfg, spool_dir };
    if (! sdUnit.success) {
        LOG_ERROR("sdUnit");
        return EXIT_FAILURE;
    }

    // FUSE メインループの開始
    return ::fuse_main(args.argc, args.argv, fbjq_operations(), &app_ctx);
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
