// fs/main.cpp
#include "local.hpp"

FuseArgsHelper::FuseArgsHelper(int argc, char** argv)
{
    ENTER_FUNCTION();
    args = FUSE_ARGS_INIT(argc, argv);
}

FuseArgsHelper::~FuseArgsHelper()
{
    ENTER_FUNCTION();
    fuse_opt_free_args(&args);
}

SystemdUnitHelper::SystemdUnitHelper(const libconfig::Config* app_cfg_, const std::filesystem::path& spool_dir)
    : app_cfg{ app_cfg_ }
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    // .path ユニットの起動関数
    const auto start_unit = [&](const char* q_name, const auto& q_item) -> bool {
        std::string unit_name{ "fbjq-executor@" };
        unit_name += q_name;
        unit_name += ".path";

        const auto subdir{ spool_dir / "queue" / q_name};

        if (fs::exists(subdir)) {
            if (fs::is_directory(subdir)) {
                // go next

            } else {
                LOG_ERROR("{}: exists, but not directory", subdir.string());
                return false;
            }
        } else {
            std::error_code ec;
            fs::create_directory(subdir, ec);

            if (ec) {
                LOG_ERROR("{}: create: message={}", subdir.string(), ec.message());
                return false;
            }
        }

        if (::chown(subdir.c_str(), q_item.exec_user_uid, fbjqutil::DEFAULT_FILE_GROUP) != 0) {
            LOG_ERROR("chown");
            return false;
        }

        if (::chmod(subdir.c_str(), 0700) != 0) {
            LOG_ERROR("chmod");
            return false;
        }

        return fbjqutil::systemctl_start_unit(unit_name);
    };

    // .path ユニットの起動
    if (fbjqutil::for_each_queue_item(app_cfg, start_unit) <= 0) {
        LOG_ERROR("for_each_queue_item");
        return;
    }

    success = true;
}

SystemdUnitHelper::~SystemdUnitHelper()
{
    ENTER_FUNCTION();

    // .path ユニットの停止関数
    const auto stop_unit = [&](const char* q_name, const auto& q_item) -> bool {
        (void) q_item;

        std::string unit_name{ "fbjq-executor@" };
        unit_name += q_name;
        unit_name += ".path";

        return fbjqutil::systemctl_stop_unit(unit_name);
    };

    // .path ユニットの停止
    fbjqutil::for_each_queue_item(app_cfg, stop_unit);
}
