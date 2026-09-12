// executor/local.hpp
#pragma once
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <vector>
#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include "fbjq-common.hpp"

struct app_args_t
{
    int check_only = 0;
    const char* cfg_file = fbjqutil::DEFAULT_CONFIG_FILE;
    int max_files = 0;
    const char* q_name = nullptr;

    std::string string() const {
        return std::format("check_only={}, cfg_file={}, max_files={}, q_name={}",
            check_only, cfg_file, max_files, NULLABLE_CSTR(q_name));
    }
};

bool set_app_args(int argc, char** argv, app_args_t* app_args);
int for_each_queue_file(const app_args_t* app_args, const libconfig::Config* app_cfg, sigset_t* sigset, const int max_process);

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
};

struct worker_param_t
{
    int id = -1;
    int sem_fd = -1;
    pthread_mutex_t* mutex = nullptr;
    pthread_cond_t* cond = nullptr;
    std::deque<std::unique_ptr<work_queue_item_t>>* work_queue = nullptr;
    bool* term_requested = nullptr;
    bool* term_immediate = nullptr;
    std::filesystem::path spool_dir;
};

class JobDispatcher
{
private:
    const std::filesystem::path spool_dir;

    int sig_fd = -1;
    int sem_fd = -1;
    int epoll_fd = -1;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    std::vector<worker_param_t> worker_params;
    std::vector<pthread_t> workers;
    std::deque<std::unique_ptr<work_queue_item_t>> work_queue;
    bool term_requested = false;
    bool term_immediate = false;

    bool dispatch_internal(const std::filesystem::path& entry_path,
        const fbjqutil::request_file_header_t* rfhdr);


public:
    JobDispatcher(const std::filesystem::path& arg_spool_dir)
        : spool_dir{ arg_spool_dir } { }

    ~JobDispatcher();

    JobDispatcher(const JobDispatcher&) = delete;
    JobDispatcher& operator=(const JobDispatcher&) = delete;

    static std::unique_ptr<JobDispatcher> make(sigset_t* sigset,
        const std::filesystem::path& spool_dir,  int max_process);

    fbjqutil::OnRegularFileResult dispatch(const std::filesystem::path& entry_path,
        const fbjqutil::request_file_header_t* rfhdr);
};

void* worker(void* param_) noexcept;
