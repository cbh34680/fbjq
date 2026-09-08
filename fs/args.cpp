// fs/args.cpp
#include "local.hpp"

#define APP_OPT(t, p, v) { t, offsetof(struct app_args_t, p), v }

bool set_app_args(struct fuse_args* args, app_args_t* app_args)
{
    ENTER_FUNCTION();

    const struct fuse_opt app_opts[] = {
        APP_OPT("-C",          check_only, 1),
        APP_OPT("--check",     check_only, 1),
        APP_OPT("-c %s",       cfg_file,   0),
        APP_OPT("--config=%s", cfg_file,   0),
        FUSE_OPT_END
    };

    // 第4引数 (proc) に NULL を渡すことで、完全に offsetof による自動代入モードにする
    if (::fuse_opt_parse(args, app_args, app_opts, nullptr) == -1) {
        LOG_ERROR("fuse_opt_parse");
        return false;
    }

    if (! app_args->cfg_file) {
        app_args->cfg_file = fbjqutil::DEFAULT_CONFIG_FILE;
    }

    return true;
}