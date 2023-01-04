#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

template <typename T>
class UnboundedBlockingQueue {
public:
    bool Put(std::shared_ptr<T> task) {
        auto guard = std::lock_guard{mutex_};

        if (stopped_) {
            return false;
        }
        buffer_.push_back(std::move(task));
        not_empty_.notify_one();
        return true;
    }

    std::shared_ptr<T> Take() {
        auto guard = std::unique_lock{mutex_};

        not_empty_.wait(guard, [this] { return stopped_ || !buffer_.empty(); });
        if (stopped_ && buffer_.empty()) {
            return nullptr;
        }

        std::shared_ptr<T> result = std::move(buffer_.front());
        buffer_.pop_front();
        return result;
    }

    void Close() {
        CloseImpl(false);
    }

    void Cancel() {
        CloseImpl(true);
    }

    bool IsClosed() {
        auto guard = std::lock_guard{mutex_};
        return stopped_;
    }

private:
    void CloseImpl(bool clear) {
        auto guard = std::lock_guard{mutex_};

        stopped_ = true;
        if (clear) {
            buffer_.clear();
        }
        not_empty_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;

    bool stopped_{false};
    std::deque<std::shared_ptr<T>> buffer_;
};