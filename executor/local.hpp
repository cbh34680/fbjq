// executor/local.hpp
#pragma once
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <vector>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include "fbjq-common.hpp"

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

struct [[nodiscard]] critical_section {
    pthread_mutex_t* mutex;
    explicit critical_section(pthread_mutex_t* arg) : mutex{ arg } {
        const auto rc = ::pthread_mutex_lock(mutex);
        if (rc != 0) {
            throw std::runtime_error(std::format("pthread_mutex_lock rc={}", rc));
        }
    }

    ~critical_section() noexcept {
        ::pthread_mutex_unlock(mutex);
    }

    critical_section(const critical_section&) = delete;
    critical_section& operator=(const critical_section&) = delete;

    critical_section(critical_section&&) = delete;
    critical_section& operator=(critical_section&&) = delete;
};

struct work_queue_item_t {
    std::filesystem::path entry_path;
    fbjqutil::request_file_header_t rfhdr;
    std::filesystem::path archive_dir;
    std::filesystem::path dead_dir;
};

struct worker_param_t
{
    int id = -1;
    sem_t* worker_slots = nullptr;
    pthread_mutex_t* mutex = nullptr;
    pthread_cond_t* cond = nullptr;
    std::deque<std::unique_ptr<work_queue_item_t>>* work_queue = nullptr;
    bool* term_requested = nullptr;
};

class JobDispatcher
{
private:
    const std::filesystem::path archive_dir;
    const std::filesystem::path dead_dir;

    sem_t* worker_slots = nullptr;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    std::vector<std::unique_ptr<worker_param_t>> worker_params;
    std::vector<pthread_t> workers;
    std::deque<std::unique_ptr<work_queue_item_t>> work_queue;
    bool term_requested = false;

public:
    JobDispatcher(const std::filesystem::path& arg_archive_dir, const std::filesystem::path& arg_dead_dir)
        : archive_dir{ arg_archive_dir }, dead_dir{ arg_dead_dir } { }

    ~JobDispatcher();

    JobDispatcher(const JobDispatcher&) = delete;
    JobDispatcher& operator=(const JobDispatcher&) = delete;

    static std::unique_ptr<JobDispatcher> make(
        const std::filesystem::path& archive_dir, const std::filesystem::path& dead_dir,
        int max_process);

    bool dispatch(sigset_t* sigset,
        const std::filesystem::path& entry_path,
        const fbjqutil::request_file_header_t* rfhdr);
};

void* worker(void* param_) noexcept;
