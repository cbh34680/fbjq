// executor/local.hpp
#pragma once
#include "fbjq-common.hpp"
#include <csignal>
#include <deque>
#include <memory>
#include <vector>
#include <pthread.h>
#include <semaphore.h>

struct app_args_t
{
    int check_only = 0;
    const char* cfg_file = fbjqutil::DEFAULT_CONFIG_FILE;
    int max_files = 0;
    const char* q_name = nullptr;

    std::string string() {
        return std::format("check_only={}, cfg_file={}, max_files={}, q_name={}",
            check_only, cfg_file, max_files, NULLABLE_CSTR(q_name));
    }
};

bool set_app_args(int argc, char** argv, app_args_t* app_args);
int for_each_queue_file(const app_args_t* app_args, const libconfig::Config* app_cfg, sigset_t* sigset);

struct [[nodiscard]] lock_mutex {
    pthread_mutex_t* mutex;
    explicit lock_mutex(pthread_mutex_t* arg) : mutex{ arg } {
        const auto rc = ::pthread_mutex_lock(mutex);
        if (rc != 0) {
            throw std::runtime_error(std::format("pthread_mutex_lock rc={}", rc));
        }
    }

    ~lock_mutex() noexcept {
        ::pthread_mutex_unlock(mutex);
    }

    lock_mutex(const lock_mutex&) = delete;
    lock_mutex& operator=(const lock_mutex&) = delete;

    lock_mutex(lock_mutex&&) = delete;
    lock_mutex& operator=(lock_mutex&&) = delete;
};

struct worker_param_t
{
    int id = -1;
    sem_t* worker_slots = nullptr;
    pthread_mutex_t* mutex = nullptr;
    pthread_cond_t* cond = nullptr;
    std::deque<std::string>* data_queue = nullptr;
    bool* terminate = nullptr;
};

class JobDispatcher
{
private:
    sigset_t* sigset = nullptr;
    sem_t* worker_slots = nullptr;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    std::vector<std::unique_ptr<worker_param_t>> worker_params;
    std::vector<pthread_t> worker_ids;
    std::deque<std::string> data_queue;
    bool terminate = false;


public:
    JobDispatcher() = default;
    ~JobDispatcher();

    JobDispatcher(const JobDispatcher&) = delete;
    JobDispatcher& operator=(const JobDispatcher&) = delete;

    static std::unique_ptr<JobDispatcher> make(sigset_t* arg_sigset, int max_process);
    bool dispatch(const std::filesystem::path& entry_path,
        const fbjqutil::request_file_header_t* rfhdr, const std::filesystem::path& archive_dir);
};

void* worker(void* param_) ;
