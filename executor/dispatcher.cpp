// executor/dispatcher.cpp
#include "local.hpp"

bool JobDispatcher::dispatch_internal(const std::filesystem::path& entry_path,
    const fbjqutil::request_file_header_t* rfhdr)
{
    ENTER_FUNCTION();

    struct epoll_event events[2];

    const auto nfds = TEMP_FAILURE_RETRY(::epoll_wait(epoll_fd, events, std::size(events), -1));
    if (nfds == -1) {
        throw std::runtime_error("epoll_wait");
    }

    for (int i=0; i<static_cast<int>(nfds); i++) {
        const auto* event = &events[i];

        if (event->events & (EPOLLERR | EPOLLHUP)) {
            throw std::runtime_error("!EPOLLERR/EPOLLHUP");
        }

        if (! (event->events & EPOLLIN)) {
            throw std::runtime_error("!EPOLLIN");
        }

        if (event->data.fd == sig_fd) {
            LOG_DEBUG("catch signal");

            struct signalfd_siginfo fdsi;
            const auto s = TEMP_FAILURE_RETRY(::read(sig_fd, &fdsi, sizeof(fdsi)));

            if (s == sizeof(fdsi)) {
                if (fdsi.ssi_signo == SIGTERM || fdsi.ssi_signo == SIGINT) {
                    LOG_INFO("Received signo={}, graceful-terminate", fdsi.ssi_signo);
                    return false;

                } else {
                    throw std::runtime_error(std::format("Received signo={}, immediate-terminate", fdsi.ssi_signo));
                }

            } else if (s == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // continue
                LOG_DEBUG("retry");

            } else {
                throw std::runtime_error(std::format("read s={}", s));
            }

        } else if (event->data.fd == sem_fd) {
            LOG_DEBUG("found free-worker");

            uint64_t count;
            const auto s = TEMP_FAILURE_RETRY(::read(sem_fd, &count, sizeof(count)));

            if (s == sizeof(count)) {
                critical_section cs_{ &mutex };

                work_queue.emplace_back(std::make_unique<work_queue_item_t>(work_queue_item_t{
                    .entry_path = entry_path,
                    .rfhdr = *rfhdr,
                }));

                LOG_DEBUG("notify entry_path={}", entry_path);
                ::pthread_cond_signal(&cond);

            } else if (s == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // continue
                LOG_DEBUG("retry");

            } else {
                throw std::runtime_error(std::format("read s={}", s));
            }

        } else {
            throw std::runtime_error(std::format("fd={}", event->data.fd));
        }
    }

    LOG_DEBUG("dispatch done");

    return true;
}

fbjqutil::OnRegularFileResult JobDispatcher::dispatch(const std::filesystem::path& entry_path,
    const fbjqutil::request_file_header_t* rfhdr)
{
    ENTER_FUNCTION();

    try {
        return dispatch_internal(entry_path, rfhdr)
            ? fbjqutil::OnRegularFileResult::Continue
            : fbjqutil::OnRegularFileResult::Break;

    } catch (const std::exception& ex) {
        LOG_ERROR("catch exception what={}", ex.what());

    } catch (...) {
        LOG_ERROR("catch exception unknown");
    }

    {
        critical_section cs_{ &mutex };

        LOG_DEBUG("set term_immediate");
        term_immediate = true;
    }

    return fbjqutil::OnRegularFileResult::Error;
}

std::unique_ptr<JobDispatcher> JobDispatcher::make(sigset_t* sigset,
    const std::filesystem::path& spool_dir, int max_process)
{
    ENTER_FUNCTION();
    bool success = false;

    LOG_DEBUG("new JobDispatcher spool_dir={}", spool_dir);
    std::unique_ptr<JobDispatcher> jd = std::make_unique<JobDispatcher>(spool_dir);
    struct epoll_event ev{};

    jd->sig_fd = ::signalfd(-1, sigset, SFD_CLOEXEC | SFD_NONBLOCK);
    if (jd->sig_fd == -1) {
        LOG_ERROR("signalfd");
        goto EXIT_LABEL;
    }

    jd->sem_fd = ::eventfd(max_process, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    if (jd->sem_fd == -1) {
        LOG_ERROR("eventfd");
        goto EXIT_LABEL;
    }

    jd->epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (jd->epoll_fd == -1) {
        LOG_ERROR("epoll_create1");
        goto EXIT_LABEL;
    }

    ev.events = EPOLLIN;
    ev.data.fd = jd->sig_fd;
    if (::epoll_ctl(jd->epoll_fd, EPOLL_CTL_ADD, jd->sig_fd, &ev) != 0) {
        LOG_ERROR("epoll_ctl sig_fd");
        goto EXIT_LABEL;
    }

    ev.events = EPOLLIN;
    ev.data.fd = jd->sem_fd;
    if (::epoll_ctl(jd->epoll_fd, EPOLL_CTL_ADD, jd->sem_fd, &ev) != 0) {
        LOG_ERROR("epoll_ctl sem_fd");
        goto EXIT_LABEL;
    }

    jd->worker_params.resize(max_process);
    jd->workers.reserve(max_process);

    for (int i=0; i<max_process; ++i) {
        LOG_DEBUG("set worker-params[{}]", i);

        auto& param = jd->worker_params[i];

        param.id = i;
        param.sem_fd = jd->sem_fd;
        param.mutex = &jd->mutex;
        param.cond = &jd->cond;
        param.work_queue = &jd->work_queue;
        param.term_requested = &jd->term_requested;
        param.term_immediate = &jd->term_immediate;
        param.spool_dir = jd->spool_dir;
    }

    for (auto& param: jd->worker_params) {
        pthread_t thrid;

        if (::pthread_create(&thrid, nullptr, worker, &param) != 0) {
            LOG_ERROR("pthread_create id={}", param.id);
            goto EXIT_LABEL;
        }

        jd->workers.push_back(thrid);
    }

    LOG_DEBUG("initialize done");

    success = true;

EXIT_LABEL:
    if (! success) {
        LOG_DEBUG("initialize error");
        return nullptr;
    }

    return jd;
}

JobDispatcher::~JobDispatcher()
{
    ENTER_FUNCTION();

    {
        critical_section cs_{ &mutex };
        term_requested = true;
        LOG_DEBUG("send broadcast event");
        ::pthread_cond_broadcast(&cond);
    }

    for (size_t i=0; i<workers.size(); ++i) {
        LOG_DEBUG("wait for worker[{}] terminate ...", i);
        ::pthread_join(workers[i], nullptr);
        LOG_DEBUG("worker[{}] terminated", i);
    }

    if (epoll_fd != -1) {
        ::close(epoll_fd);
        epoll_fd = -1;
    }

    if (sem_fd != -1) {
        ::close(sem_fd);
        sem_fd = -1;
    }

    if (sig_fd != -1) {
        ::close(sig_fd);
        sig_fd = -1;
    }

    ::pthread_cond_destroy(&cond);
    ::pthread_mutex_destroy(&mutex);
}
