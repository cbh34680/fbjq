// fs/main.cpp
#include "local.hpp"

#define APP_OPT(t, p, v) { t, offsetof(struct app_args_t, p), v }

namespace {

int main_(int argc, char** argv)
{
    namespace fs = std::filesystem;
    (void) argc;
    ENTER_FUNCTION();

    FuseArgsHelper fuseArgs_{ argc, argv };
    struct fuse_args& args = fuseArgs_.args;

    struct app_args_t
    {
        int check_only{ 0 };
        const char* cfg_file{ nullptr };

        std::string string() {
            return std::format("check_only={}, cfg_file={}", check_only, cfg_file);
        }
    }
    app_args;

    const struct fuse_opt app_opts[] = {
        APP_OPT("-C",          check_only, 1),
        APP_OPT("--check",     check_only, 1),
        APP_OPT("-c %s",       cfg_file,   0),
        APP_OPT("--config=%s", cfg_file,   0),
        FUSE_OPT_END
    };

    // 第4引数 (proc) に NULL を渡すことで、完全に offsetof による自動代入モードにする
    if (::fuse_opt_parse(&args, &app_args, app_opts, nullptr) == -1) {
        LOG_ERROR("fuse_opt_parse");
        return EXIT_FAILURE;
    }

    if (! app_args.cfg_file) {
        app_args.cfg_file = fbjqutil::DEFAULT_CONFIG_FILE;
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

    ::umask(0);

    const int rc = main_(argc, argv);
    LOG_INFO("program return-code={}", rc);

    return rc;
}
