#include "local.hpp"
#include <fstream>

namespace {

void* worker_(void* param_) {
    worker_param_t* param = static_cast<worker_param_t*>(param_);
    const auto worker_no = param->id + 1;

    LOG_DEBUG("worker[{}] enter", worker_no);

    while (1) {
        {
            lock_mutex lock{ param->mutex };

            while (param->data_queue->empty() && ! *param->terminate) {
                LOG_DEBUG("worker[{}] wait for event ...", worker_no);
                ::pthread_cond_wait(param->cond, param->mutex);
                LOG_DEBUG("worker[{}] receive event", worker_no);
            }

            if (*param->terminate) {
                LOG_DEBUG("worker[{}] event is terminate", worker_no);
                break;
            }

            //data = std::move(param->data_queue->front());
            //param->data_queue->pop_front();
        }
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

    return nullptr;
}
