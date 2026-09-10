// executor/worker.cpp
#include "local.hpp"
#include <cassert>
#include <fstream>
#include <thread>

int for_each_queue_file(sigset_t* sigset, const app_args_t* app_args, const libconfig::Config* app_cfg)
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

    siginfo_t siginfo{};
    const struct timespec sig_to0s{};

    const auto on_regular_file = [&](const int loop, const auto& entry_path, const auto* rfhdr) -> bool {
        if (loop >= app_args->max_files) {
            // .path の停止を検知するために一定数を処理したら .service を終了する
            LOG_INFO("The maximum number of processes has been reached.");
            return false;
        }

        bool success = false;

        if (rfhdr) {
            const int signo = ::sigtimedwait(sigset, &siginfo, &sig_to0s);
            if (signo > 0) {
                LOG_INFO("Signal received: {}", signo);
                return false;
            }



            success = true;
        }

        if (! success) {
            const auto newpath{ dead_dir / entry_path.filename() };
            LOG_INFO("move to {}", newpath);
            fs::rename(entry_path, newpath);
        }

        return true;
    };

    const auto rc = fbjqutil::for_each_file(app_cfg, spool_dir / "queue" / app_args->q_name, on_regular_file);
    if (rc < 0) {
        LOG_ERROR("for_each_file: rc={}", rc);
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files", rc);

    return EXIT_SUCCESS;
}
