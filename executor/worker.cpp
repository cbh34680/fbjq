// executor/worker.cpp
#include "local.hpp"
#include <fstream>

namespace {

bool parse_and_execute(std::istream& is, const fbjqutil::request_file_header_t*)
{
    std::string line;

    while (std::getline(is, line)) {
        std::cout << line << std::endl;
    }

    return true;
}

void* worker_(void* param_)
{
    worker_param_t* param = static_cast<worker_param_t*>(param_);
    const auto worker_no = param->id + 1;

    LOG_DEBUG("worker[{}] enter", worker_no);

    while (1) {
        std::unique_ptr<work_queue_item_t> wq_item;

        {
            critical_section cs_{ param->mutex };

            while (param->work_queue->empty() && ! *param->term_requested) {
                LOG_DEBUG("worker[{}] wait for event ...", worker_no);
                ::pthread_cond_wait(param->cond, param->mutex);
                LOG_DEBUG("worker[{}] receive event", worker_no);
            }

            if (*param->term_requested) {
                LOG_DEBUG("worker[{}] event is terminate", worker_no);
                break;
            }

            wq_item = std::move(param->work_queue->front());
            param->work_queue->pop_front();
        }

        LOG_DEBUG("worker[{}] PROCESS entry_path={}", worker_no, wq_item->entry_path);

        bool ok = false;

        try {
            std::ifstream ifs{ wq_item->entry_path };
            if (ifs) {
                ifs.seekg(sizeof(fbjqutil::request_file_header_t));

                ok = parse_and_execute(ifs, &wq_item->rfhdr);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR("worker[{}] catch exception what={}", worker_no, ex.what());

        } catch (...) {
            LOG_ERROR("worker[{}] catch exception unknown", worker_no);
        }

        const auto newpath{ (ok ? wq_item->archive_dir : wq_item->dead_dir) / wq_item->entry_path.filename() };
        LOG_INFO("worker[{}] move to newpath={}", worker_no, newpath);
        std::filesystem::rename(wq_item->entry_path, newpath);

        LOG_DEBUG("worker[{}] release worker_slots", worker_no);
        ::sem_post(param->worker_slots);
    }

    LOG_DEBUG("worker[{}] leave", worker_no);

    return nullptr;
}

} // namespace

void* worker(void* param_) noexcept {
    try {
        return worker_(param_);

    } catch (const std::exception& ex) {
        LOG_ERROR("catch exception what={}", ex.what());

    } catch (...) {
        LOG_ERROR("catch exception unknown");
    }

    worker_param_t* param = static_cast<worker_param_t*>(param_);
    LOG_DEBUG("release worker_slots");
    ::sem_post(param->worker_slots);

    return nullptr;
}
