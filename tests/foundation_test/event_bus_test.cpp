/// EventBus 单测：类型安全发布订阅、退订、多线程发布（Doc12 §2）
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/events.hpp"
#include "foundation/event_bus.hpp"

using sc::localdesk::Evt_DeviceOnline;
using sc::localdesk::Evt_StreamLag;
using sc::localdesk::foundation::EventBus;

TEST(EventBus, PublishSubscribeTyped) {
    EventBus bus;
    int hit = 0;
    const auto id = bus.subscribe<Evt_DeviceOnline>([&hit](const Evt_DeviceOnline& e) {
        EXPECT_EQ(e.device_alias, "IT-01");
        ++hit;
    });
    Evt_DeviceOnline e;
    e.device_alias = "IT-01";
    bus.publish(e);
    EXPECT_EQ(hit, 1);
    bus.unsubscribe<Evt_DeviceOnline>(id);
    bus.publish(e);
    EXPECT_EQ(hit, 1);  // 退订后不再触达
}

TEST(EventBus, TypeIsolation) {
    EventBus bus;
    int online_hit = 0;
    int lag_hit = 0;
    bus.subscribe<Evt_DeviceOnline>([&online_hit](const Evt_DeviceOnline&) { ++online_hit; });
    bus.subscribe<Evt_StreamLag>([&lag_hit](const Evt_StreamLag&) { ++lag_hit; });

    Evt_StreamLag lag;
    bus.publish(lag);
    EXPECT_EQ(online_hit, 0);
    EXPECT_EQ(lag_hit, 1);
}

TEST(EventBus, MultithreadedPublishNoDeadlock) {
    EventBus bus;
    std::atomic<int> total{0};
    bus.subscribe<Evt_DeviceOnline>([&total](const Evt_DeviceOnline&) {
        ++total;
    });

    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&bus] {
            for (int i = 0; i < kPerThread; ++i) {
                Evt_DeviceOnline e;
                e.device_alias = "dev";
                bus.publish(e);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(total.load(), kThreads * kPerThread);
}
