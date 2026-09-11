// executor/worker.cpp
#include "local.hpp"
#include <fstream>

namespace {

void* worker_(void* param_) {
    worker_param_t* param = static_cast<worker_param_t*>(param_);
    const auto worker_no = param->id + 1;

    LOG_DEBUG("worker[{}] enter", worker_no);

    while (1) {
        std::unique_ptr<work_queue_item_t> wq_item;

        {
            critical_section cs_{ param->mutex };

            while (param->work_queue->empty() && ! *param->terminate) {
                LOG_DEBUG("worker[{}] wait for event ...", worker_no);
                ::pthread_cond_wait(param->cond, param->mutex);
                LOG_DEBUG("worker[{}] receive event", worker_no);
            }

            if (*param->terminate) {
                LOG_DEBUG("worker[{}] event is terminate", worker_no);
                break;
            }

            wq_item = std::move(param->work_queue->front());
            param->work_queue->pop_front();
        }

        LOG_DEBUG("worker[{}] PROCESS entry_path={}", worker_no, wq_item->entry_path);

        LOG_DEBUG("worker[{}] release worker_slots", worker_no);
        ::sem_post(param->worker_slots);
    }

    LOG_DEBUG("worker[{}] leave", worker_no);

    return nullptr;
}

} // namespace

void* worker(void* param_) {
    try {
        return worker_(param_);

    } catch (const std::exception& ex) {
        LOG_ERROR("catch ex={}", ex.what());

    } catch (...) {
        LOG_ERROR("catch unknown error");
    }

    worker_param_t* param = static_cast<worker_param_t*>(param_);
    LOG_DEBUG("release worker_slots");
    ::sem_post(param->worker_slots);

    return nullptr;
}
