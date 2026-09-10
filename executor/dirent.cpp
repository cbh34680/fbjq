// executor/worker.cpp
#include "local.hpp"

int for_each_queue_file(const app_args_t* app_args, const libconfig::Config* app_cfg, sigset_t* sigset)
{
    namespace fs = std::filesystem;

    // create thread-pool
    int max_process = 1;

    if (! app_cfg->lookupValue(std::format("queue.{}.max_process", app_args->q_name), max_process)) {
        LOG_INFO("queue.{}.max_process: invalid value, set default({})", app_args->q_name, max_process);
    }

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    const fs::path archive_dir{ spool_dir / "archive" };
    const fs::path dead_dir{ spool_dir / "dead" };

    auto jd{ JobDispatcher::make(sigset, max_process) };

    const auto on_regular_file = [&](const auto& entry_path, const auto* rfhdr) -> bool {
        bool success = rfhdr
            ? jd->dispatch(entry_path, rfhdr, archive_dir)
            : false;

        if (! success) {
            const auto newpath{ dead_dir / entry_path.filename() };
            LOG_INFO("move to {}", newpath);
            fs::rename(entry_path, newpath);
        }

        return true;
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
