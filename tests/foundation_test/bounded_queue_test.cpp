/// BoundedQueue 单测：overwrite 丢帧语义 / wait 阻塞语义 / 关闭唤醒（Doc08 §4）
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "foundation/bounded_queue.hpp"

using sc::localdesk::foundation::BoundedQueue;

TEST(BoundedQueue, OverwriteDropsOldest) {
    BoundedQueue<int> q(3);
    for (int i = 0; i < 5; ++i) {
        q.push_overwrite(std::move(i));
    }
    // 只留最新 3 个（画面帧语义，Doc10 §3）
    int v = 0;
    ASSERT_TRUE(q.pop_wait(v, 0));
    EXPECT_EQ(v, 2);
    ASSERT_TRUE(q.pop_wait(v, 0));
    EXPECT_EQ(v, 3);
    ASSERT_TRUE(q.pop_wait(v, 0));
    EXPECT_EQ(v, 4);
    EXPECT_FALSE(q.pop_wait(v, 0));
}

TEST(BoundedQueue, WaitBlocksUntilSpace) {
    BoundedQueue<int> q(1);
    q.push_wait(1);
    std::atomic<bool> pushed{false};
    std::thread producer([&] {
        pushed = q.push_wait(2);  // 阻塞直到消费
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(pushed.load());
    int v = 0;
    ASSERT_TRUE(q.pop_wait(v));
    EXPECT_EQ(v, 1);
    producer.join();
    EXPECT_TRUE(pushed.load());  // 文件分片语义：不丢（Doc11 §6）
}

TEST(BoundedQueue, CloseWakesWaiters) {
    BoundedQueue<int> q(1);
    q.push_wait(1);
    std::atomic<bool> result{true};
    std::thread producer([&] { result = q.push_wait(2); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.close();
    producer.join();
    EXPECT_FALSE(result.load());  // 关闭后 push_wait 返回 false
    int v = 0;
    EXPECT_TRUE(q.pop_wait(v, 0));  // 已入队数据仍可排空
    EXPECT_FALSE(q.pop_wait(v, 0));
}
