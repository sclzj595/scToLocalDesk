// ============================================================
// scToLocalDesk | src/foundation/event_bus.hpp
// 线程安全类型安全发布订阅总线（Doc12 §2）
// - publish/subscribe 任意线程安全；回调在发布线程同步执行
// - 回调必须快速返回，重活转线程池（契约见调用方）
// - publish 时先拷贝 handler 列表再执行，避免持锁回调死锁
// ============================================================
#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sc::localdesk::foundation {

class EventBus final {
public:
    using HandlerId = uint64_t;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /// 订阅某类型事件；返回句柄用于退订
    template <typename E>
    HandlerId subscribe(std::function<void(const E&)> handler) {
        static_assert(std::is_class_v<E>, "事件必须是 class/struct（继承 IEvent）");
        const auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
        auto any_h = std::make_pair(id, std::any(std::move(handler)));
        {
            std::lock_guard<std::mutex> lk(mtx_);
            handlers_[std::type_index(typeid(E))].push_back(std::move(any_h));
        }
        return id;
    }

    /// 发布事件：按值拷贝投递（小对象语义，Doc12 §2）
    template <typename E>
    void publish(const E& event) {
        static_assert(std::is_class_v<E>, "事件必须是 class/struct（继承 IEvent）");
        // 第一段锁：取出快照；第二段执行：无锁回调，防死锁
        std::vector<std::any> snapshot;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            const auto it = handlers_.find(std::type_index(typeid(E)));
            if (it == handlers_.end()) {
                return;
            }
            snapshot.reserve(it->second.size());
            for (const auto& [id, h] : it->second) {
                snapshot.push_back(h);
            }
        }
        for (const auto& h : snapshot) {
            const auto& fn = std::any_cast<const std::function<void(const E&)>&>(h);
            fn(event);
        }
    }

    /// 退订；幂等
    template <typename E>
    void unsubscribe(HandlerId id) {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto it = handlers_.find(std::type_index(typeid(E)));
        if (it == handlers_.end()) {
            return;
        }
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [id](const std::pair<HandlerId, std::any>& p) {
                               return p.first == id;
                           }),
            vec.end());
    }

    /// 清空全部订阅（测试辅助）
    void clear_all() {
        std::lock_guard<std::mutex> lk(mtx_);
        handlers_.clear();
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::type_index, std::vector<std::pair<HandlerId, std::any>>> handlers_;
    std::atomic<uint64_t> next_id_{1};
};

}  // namespace sc::localdesk::foundation
