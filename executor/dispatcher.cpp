// executor/dispatcher.cpp
#include "local.hpp"

bool JobDispatcher::dispatch(sigset_t* sigset, const std::filesystem::path& entry_path,
    const fbjqutil::request_file_header_t* rfhdr)
{
    LOG_DEBUG("dispatch entry_path={}", entry_path);

    while (1) {
        LOG_DEBUG("Waiting for a worker to become available ...");

        struct timespec timeout5s;
        if (::clock_gettime(CLOCK_REALTIME, &timeout5s) == -1) {
            LOG_ERROR("clock_gettime");
            return false;
        }

        timeout5s.tv_sec += 5;

        const auto semrc = TEMP_FAILURE_RETRY(::sem_timedwait(worker_slots, &timeout5s));
        const auto semec = errno;

        const struct timespec timeout0s{};
        const auto signo = ::sigtimedwait(sigset, nullptr, &timeout0s);
        if (signo > 0) {
            LOG_INFO("Signal received signo={}, send terminate to all workers", signo);
            return false;
        }

        // no signal

        if (semec == EINVAL) {
            LOG_ERROR("sem_timedwait");
            return false;
        }

        if (semrc == 0) {
            // exist free-worker

            critical_section lock{ &mutex };

            work_queue.emplace_back(std::make_unique<work_queue_item_t>(work_queue_item_t{
                .entry_path = entry_path,
                .rfhdr = *rfhdr,
            }));

            ::pthread_cond_signal(&cond);

            break;
        }

        LOG_DEBUG("Time's up.");
    }

    return true;
}

std::unique_ptr<JobDispatcher> JobDispatcher::make(
    const std::filesystem::path& archive_dir, const std::filesystem::path& dead_dir,
    int max_process)
{
    bool success = false;

    LOG_DEBUG("new JobDispatcher archive_dir={} dead_dir={}", archive_dir, dead_dir);
    JobDispatcher* jd = new JobDispatcher{ archive_dir, dead_dir };

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
    jd->workers.reserve(max_process);

    for (int i=0; i<max_process; ++i) {
        LOG_DEBUG("create worker[{}]", i);

        auto& param = jd->worker_params.emplace_back(std::make_unique<worker_param_t>(worker_param_t{
            .id = i,
            .worker_slots = jd->worker_slots,
            .mutex = &jd->mutex,
            .cond = &jd->cond,
            .work_queue = &jd->work_queue,
            .terminate = &jd->terminate,
        }));

        pthread_t thrid;
        if (::pthread_create(&thrid, nullptr, worker, param.get()) != 0) {
            LOG_ERROR("pthread_create i={}", i);
            goto EXIT_LABEL;
        }

        jd->workers.push_back(thrid);
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

JobDispatcher::~JobDispatcher()
{
    {
        critical_section cs_{ &mutex };
        terminate = true;
        LOG_DEBUG("send broadcast event");
        ::pthread_cond_broadcast(&cond);
    }

    for (size_t i=0; i<workers.size(); ++i) {
        LOG_DEBUG("wait for worker[{}] terminate ...", i);
        ::pthread_join(workers[i], nullptr);
        LOG_DEBUG("worker[{}] terminated", i);
    }

    if (worker_slots) {
        LOG_DEBUG("destroy semaphore");
        ::sem_destroy(worker_slots);
        ::free(worker_slots);
        worker_slots = nullptr;
    }
}
