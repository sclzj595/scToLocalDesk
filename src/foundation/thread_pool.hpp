// ============================================================
// scToLocalDesk | src/foundation/thread_pool.hpp
// 固定线程池：加密 / PBKDF2 / 文件 IO 长任务专用（Doc12 §3）
// 红线：池内禁止再 submit 并同步等待自身结果（死锁）
// ============================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace sc::localdesk::foundation {

class ThreadPool final {
public:
    explicit ThreadPool(size_t threads = std::max(4u, std::thread::hardware_concurrency())) {
        threads = std::max<size_t>(threads, 1);
        workers_.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// 提交任务，返回 future（任务抛异常则在 future.get() 重抛）
    template <typename F, typename... Args>
    [[nodiscard]] auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
    {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stopping_) {
                // 池已停：任务直接在提交线程执行，保证不丢任务
                (*task)();
            }
            else {
                queue_.emplace([task] { (*task)(); });
            }
        }
        cv_.notify_one();
        return fut;
    }

    /// 等待已入队任务执行完毕后退出线程（析构自动调用）
    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
        workers_.clear();
    }

    [[nodiscard]] size_t pending_count() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return queue_.size();
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) {
                    return;
                }
                job = std::move(queue_.front());
                queue_.pop();
            }
            job();  // 异常已在 packaged_task 内捕获进 future
        }
    }

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};

}  // namespace sc::localdesk::foundation
