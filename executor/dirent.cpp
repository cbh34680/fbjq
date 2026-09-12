// executor/dirent.cpp
#include "local.hpp"

int for_each_queue_file(const app_args_t* app_args,
    const libconfig::Config* app_cfg,sigset_t* sigset, const int max_process)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    auto jd{ JobDispatcher::make(sigset, spool_dir, max_process) };
    if (! jd) {
        LOG_ERROR("JobDispatcher::make");
        return EXIT_FAILURE;
    }

    const auto on_regular_file = [&](const auto& entry_path, const auto* rfhdr) -> fbjqutil::OnRegularFileResult {
        LOG_DEBUG("dispatch entry_path={}", entry_path);

        return jd->dispatch(entry_path, rfhdr);
    };

    const auto q_name_dir{ spool_dir / "queue" / app_args->q_name };

    const auto rc = fbjqutil::for_each_file(app_cfg, q_name_dir, app_args->max_files, on_regular_file);
    if (rc < 0) {
        LOG_ERROR("for_each_file: rc={}", rc);
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files", rc);

    return EXIT_SUCCESS;
}
