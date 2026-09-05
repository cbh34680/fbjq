// fs/main.cpp
#include "local.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <fuse3/fuse_opt.h>

struct FuseArgsHelper
{
    struct fuse_args args;
    
    FuseArgsHelper(int argc, char** argv)
    {
        ENTER_FUNCTION();
        args = FUSE_ARGS_INIT(argc, argv);
    }

    ~FuseArgsHelper()
    {
        ENTER_FUNCTION();
        fuse_opt_free_args(&args);
    }
};

struct SystemdUnitHelper
{
    bool success = false;
    const libconfig::Config* app_cfg;

    SystemdUnitHelper(const libconfig::Config* app_cfg_, const std::filesystem::path& spool_dir)
        : app_cfg{ app_cfg_ }
    {
        namespace fs = std::filesystem;
        ENTER_FUNCTION();

        // .path ユニットの起動関数
        const auto start_unit = [&spool_dir](const char* q_name, const auto& q_item) -> bool {
            std::string unit_name{ "fbjq-executor@" };
            unit_name += q_name;
            unit_name += ".path";

            const auto subdir{ spool_dir / "queue" / q_name};

            if (fs::exists(subdir)) {
                if (fs::is_directory(subdir)) {
                    // go next

                } else {
                    LOG_ERROR("{}: exists, but not directory", subdir.c_str());
                    return false;
                }
            } else {
                std::error_code ec;
                fs::create_directory(subdir, ec);

                if (ec) {
                    LOG_ERROR("{}: create: message={}", subdir.c_str(), ec.message());
                    return false;
                }
            }

            if (::chown(subdir.c_str(), q_item.exec_user_uid, fbjqlib::DEFAULT_FILE_GROUP) != 0) {
                LOG_ERROR("chown");
                return false;
            }
            
            if (::chmod(subdir.c_str(), 0700) != 0) {
                LOG_ERROR("chmod");
                return false;
            }

            return fbjqlib::call_systemd_unit_method(unit_name, "StartUnit");
        };

        // .path ユニットの起動
        if (fbjqlib::for_each_queue_item(app_cfg, start_unit) <= 0) {
            LOG_ERROR("for_each_queue_item");
            return;
        }

        success = true;
    }

    ~SystemdUnitHelper()
    {
        ENTER_FUNCTION();

        // .path ユニットの停止関数
        const auto stop_unit = [](const char* q_name, const auto& q_item) -> bool {
            (void) q_item;

            std::string unit_name{ "fbjq-executor@" };
            unit_name += q_name;
            unit_name += ".path";

            return fbjqlib::call_systemd_unit_method(unit_name, "StopUnit");
        };

        // .path ユニットの停止
        fbjqlib::for_each_queue_item(app_cfg, stop_unit);
    }
};

struct app_args_t
{
    int check_only{ 0 };
    const char* cfg_file{ nullptr };
};

#define APP_OPT(t, p, v) { t, offsetof(struct app_args_t, p), v }

static const struct fuse_opt app_opts[] =
{
    APP_OPT("-C",          check_only, 1),
    APP_OPT("--check",     check_only, 1),
    APP_OPT("-c %s",       cfg_file,   0),
    APP_OPT("--config=%s", cfg_file,   0),
    FUSE_OPT_END
};

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    (void) argc;
    ENTER_FUNCTION();

    ::umask(0);

    FuseArgsHelper fuseArgs_{ argc, argv };
    struct fuse_args& args = fuseArgs_.args;

    app_args_t app_args;

    // 第4引数 (proc) に NULL を渡すことで、完全に offsetof による自動代入モードにする
    if (::fuse_opt_parse(&args, &app_args, app_opts, nullptr) == -1) {
        LOG_ERROR("fuse_opt_parse");
        return EXIT_FAILURE;
    }

    if (! app_args.cfg_file) {
        app_args.cfg_file = fbjqlib::DEFAULT_CONFIG_FILE;
    }

    LOG_INFO("load_config config={}", app_args.cfg_file);

    // 設定ファイルの読み込み
    auto appConfigPtr{ fbjqlib::load_config(app_args.cfg_file) };
    if (appConfigPtr) {
        if (app_args.check_only) {
            LOG_INFO("config check ok");
            return EXIT_SUCCESS;
        }
    } else {
        LOG_ERROR("load_config");
        return EXIT_FAILURE;
    }

    const auto* app_cfg{ appConfigPtr.get() };
    fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };

    LOG_DEBUG("spool_dir={}", spool_dir.c_str());

    // FUSE コンテキストの作成
    struct fbjqlib::app_context_t app_ctx {
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
