// executor/worker.cpp
#include "local.hpp"
#include <fstream>

namespace {

bool parse_and_execute(std::istream& is, const fbjqutil::request_file_header_t*)
{
    ENTER_FUNCTION();

    std::string line;

    while (std::getline(is, line)) {
        std::cout << line << std::endl;
    }

    return true;
}

void* worker_(void* param_)
{
    ENTER_FUNCTION();
    worker_param_t* param = static_cast<worker_param_t*>(param_);

    LOG_DEBUG("worker[{}] enter", param->id);

    const std::filesystem::path archive_dir{ param->spool_dir / "archive" };
    const std::filesystem::path dead_dir{ param->spool_dir / "dead" };

    while (1) {
        std::unique_ptr<work_queue_item_t> wq_item;

        {
            critical_section cs_{ param->mutex };

            while (param->work_queue->empty() && ! *param->term_requested) {
                LOG_DEBUG("worker[{}] wait for event ...", param->id);

                if (::pthread_cond_wait(param->cond, param->mutex) != 0) {
                    throw std::runtime_error("pthread_cond_wait");
                }

                LOG_DEBUG("worker[{}] receive event", param->id);
            }

            LOG_DEBUG("term_requested={} term_immediate={} work_queue={}",
                *param->term_requested, *param->term_immediate, param->work_queue->size());

            if (*param->term_requested) {
                if (*param->term_immediate) {
                    LOG_DEBUG("worker[{}] loop-break force", param->id);
                    break;

                } else if (param->work_queue->empty()) {
                    LOG_DEBUG("worker[{}] loop-break graceful", param->id);
                    break;
                }
            }

            wq_item = std::move(param->work_queue->front());
            param->work_queue->pop_front();
        }

        LOG_DEBUG("worker[{}] PROCESS entry_path={}", param->id, wq_item->entry_path);

        bool ok = false;

        try {
            std::ifstream ifs{ wq_item->entry_path };
            if (ifs) {
                ifs.seekg(sizeof(fbjqutil::request_file_header_t));

                ok = parse_and_execute(ifs, &wq_item->rfhdr);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR("worker[{}] catch exception what={}", param->id, ex.what());

        } catch (...) {
            LOG_ERROR("worker[{}] catch exception unknown", param->id);
        }

        const auto newpath{ (ok ? archive_dir : dead_dir) / wq_item->entry_path.filename() };
        LOG_INFO("worker[{}] move to newpath={}", param->id, newpath);
        std::filesystem::rename(wq_item->entry_path, newpath);

        LOG_DEBUG("worker[{}] release worker_slots", param->id);

        uint64_t val = 1;
        const auto s = TEMP_FAILURE_RETRY(::write(param->sem_fd, &val, sizeof(val)));

        if (s == -1) {
            throw std::runtime_error("write");
        }

        if (s != sizeof(val)) {
            throw std::runtime_error("short write");
        }
    }

    LOG_DEBUG("worker[{}] leave", param->id);

    return nullptr;
}

} // namespace

void* worker(void* param_) noexcept {
    ENTER_FUNCTION();

    try {
        return worker_(param_);

    } catch (const std::exception& ex) {
        LOG_ERROR("catch exception what={}", ex.what());

    } catch (...) {
        LOG_ERROR("catch exception unknown");
    }

    ::kill(getpid(), SIGUSR1);

    return nullptr;
}
