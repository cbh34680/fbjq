// delivery/local.hpp
#pragma once
#include "fbjq-common.hpp"

#include <queue>
#include <mutex>
#include <condition_variable>

class ThreadSafeQueue
{
private:
    std::queue<std::filesystem::path> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

public:
    // 【生産者側】キューにデータを入れる
    void push(std::filesystem::path value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }

        // データが入ったので、待機中のワーカーを1つだけ起こす
        cv_.notify_one();
    }

    // 【ワーカー側】安全にデータを取り出す（空なら待機）
    std::filesystem::path pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // キューが空の間はロックを解放して安全にスリープ待機
        // データが入ると自動でロックを再獲得して抜ける
        cv_.wait(lock, [this] { return !queue_.empty(); });

        auto value = std::move(queue_.front());
        queue_.pop();
        return value;
    }
};

int for_each_queue_file(const libconfig::Config* app_cfg, const char* q_name,
    const std::function<bool(const std::filesystem::directory_entry&, const int)>& should_continue);
