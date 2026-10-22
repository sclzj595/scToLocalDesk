// ============================================================
// scToLocalDesk | src/foundation/bounded_queue.hpp
// 有界队列（参考 YD/NetComm/BoundedBuffer.h，现代化重写：std + C++17）
// 两种入队语义（Doc08 §4 流控红线）：
//   push_overwrite : 队列满丢弃最旧（画面帧：只留最新）
//   push_wait      : 阻塞等待空位（文件分片：不可丢）
// ============================================================
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace sc::localdesk::foundation {

template <typename T>
class BoundedQueue final {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity < 1 ? 1 : capacity) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /// 画面帧语义：满则丢最旧、入最新；返回被丢弃的条数（丢帧统计，Doc10 §3）
    size_t push_overwrite(T&& value) {
        size_t dropped = 0;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            while (queue_.size() >= capacity_) {
                queue_.pop_front();
                ++dropped;
            }
            queue_.push_back(std::move(value));
        }
        cv_not_empty_.notify_one();
        return dropped;
    }

    /// 文件分片语义：满则阻塞等待（关闭时返回 false，Doc11 §3）
    bool push_wait(T&& value) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_not_full_.wait(lk, [this] { return closed_ || queue_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        queue_.push_back(std::move(value));
        lk.unlock();
        cv_not_empty_.notify_one();
        return true;
    }

    /// 限时等待版本：超时/关闭返回 false（非画面帧入队用，Doc08 §4）
    bool push_wait_for(T&& value, int timeout_ms) {
        std::unique_lock<std::mutex> lk(mtx_);
        const auto ok = cv_not_full_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                              [this] {
                                                  return closed_ || queue_.size() < capacity_;
                                              });
        if (!ok || closed_) {
            return false;
        }
        queue_.push_back(std::move(value));
        lk.unlock();
        cv_not_empty_.notify_one();
        return true;
    }

    /// 出队；timeout_ms < 0 永久等待。返回 false = 超时或已关闭且空
    bool pop_wait(T& out, int timeout_ms = -1) {
        std::unique_lock<std::mutex> lk(mtx_);
        const auto pred = [this] { return closed_ || !queue_.empty(); };
        bool ok = false;
        if (timeout_ms < 0) {
            cv_not_empty_.wait(lk, pred);
            ok = !queue_.empty();
        } else {
            ok = cv_not_empty_.wait_for(lk, std::chrono::milliseconds(timeout_ms), pred) &&
                 !queue_.empty();
        }
        if (!ok) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        lk.unlock();
        cv_not_full_.notify_one();
        return true;
    }

    /// 关闭：唤醒全部等待者；push_wait 返回 false，pop 排空后返回 false
    void close() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            closed_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.clear();
    }

    /// 重新开放入队（close 后复用队列，新会话场景）
    void reopen() {
        std::lock_guard<std::mutex> lk(mtx_);
        closed_ = false;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return queue_.size();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return closed_;
    }

private:
    const size_t capacity_;
    std::deque<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    bool closed_{false};
};

}  // namespace sc::localdesk::foundation
