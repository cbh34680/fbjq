#include "local.hpp"

std::unique_ptr<JobDispatcher> JobDispatcher::make(sigset_t* sigset, int max_process)
{
    bool success = false;

    LOG_DEBUG("new JobDispatcher");
    JobDispatcher* jd = new JobDispatcher{};

    jd->sigset = sigset;
    jd->worker_slots = static_cast<sem_t*>(::malloc(sizeof(sem_t)));
    if (! jd->worker_slots) {
        LOG_ERROR("malloc");
        goto EXIT_LABEL;
    }

    if (::sem_init(jd->worker_slots, 0, max_process) != 0) {
        ::free(jd->worker_slots);
        jd->worker_slots = nullptr;

        LOG_ERROR("sem_init");
        goto EXIT_LABEL;
    }

    jd->worker_params.reserve(max_process);
    jd->worker_ids.reserve(max_process);

    for (int i=0; i<max_process; ++i) {
        LOG_DEBUG("create worker[{}]", i);

        auto& param = jd->worker_params.emplace_back(std::make_unique<worker_param_t>(worker_param_t{
            .id = i,
            .worker_slots = jd->worker_slots,
            .mutex = &jd->mutex,
            .cond = &jd->cond,
            .data_queue = &jd->data_queue,
            .terminate = &jd->terminate,
        }));

        pthread_t worker_id;

        if (::pthread_create(&worker_id, nullptr, worker, param.get()) != 0) {
            LOG_ERROR("pthread_create i={}", i);
            goto EXIT_LABEL;
        }

        jd->worker_ids.push_back(worker_id);
    }

    LOG_DEBUG("initialize done");

    success = true;

EXIT_LABEL:
    if (! success) {
        LOG_DEBUG("initialize error");
        delete jd;
        jd = nullptr;
    }

    return std::unique_ptr<JobDispatcher>(jd);
}

bool JobDispatcher::dispatch(const std::filesystem::path& entry_path,
        const fbjqutil::request_file_header_t* rfhdr, const std::filesystem::path& archive_dir)
{
    return false;
}

JobDispatcher::~JobDispatcher()
{
    {
        lock_mutex lock{ &mutex };
        terminate = true;
        LOG_DEBUG("send broadcast event");
        ::pthread_cond_broadcast(&cond);
    }

    for (size_t i=0; i<worker_ids.size(); ++i) {
        LOG_DEBUG("wait for worker[{}] terminate ...", i);
        ::pthread_join(worker_ids[i], nullptr);
        LOG_DEBUG("worker[{}] terminated", i);
    }

    if (worker_slots) {
        LOG_DEBUG("destroy semaphore");
        ::sem_destroy(worker_slots);
        ::free(worker_slots);
        worker_slots = nullptr;
    }
}
