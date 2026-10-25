// ============================================================
// scToLocalDesk | tests/test_bounded_queue.cpp
// 有界队列用例（Doc08 §4 流控红线）：双入队语义 / 关闭语义 / 超时
// ============================================================
#include "doctest.h"

#include <string>
#include <thread>

#include "foundation/bounded_queue.hpp"

using sc::localdesk::foundation::BoundedQueue;

/// 跨线程用例规模（文件级常量：MSVC lambda 内引用局部 constexpr 需显式捕获）
static constexpr int kStressCount = 1000;

TEST_CASE("push_overwrite 满则丢最旧")
{
    BoundedQueue<std::string> q(2);
    CHECK(q.push_overwrite(std::string("a")) == 0);
    CHECK(q.push_overwrite(std::string("b")) == 0);
    // 第三次入队挤掉 "a"，返回丢弃数 1
    CHECK(q.push_overwrite(std::string("c")) == 1);
    CHECK(q.size() == 2);

    // 只留最新：弹出顺序 b, c
    std::string out;
    REQUIRE(q.pop_wait(out, 0));
    CHECK(out == "b");
    REQUIRE(q.pop_wait(out, 0));
    CHECK(out == "c");
    CHECK(q.empty());
}

TEST_CASE("push_wait_for 队满限时失败")
{
    BoundedQueue<int> q(1);
    CHECK(q.push_wait_for(1, 0));
    // 队满：50ms 内无消费者，入队失败且原值不丢
    int v = 2;
    CHECK_FALSE(q.push_wait_for(std::move(v), 50));
    CHECK(q.size() == 1);
}

TEST_CASE("pop_wait 空队列超时返回 false")
{
    BoundedQueue<int> q(2);
    int out = 0;
    CHECK_FALSE(q.pop_wait(out, 30));
}

TEST_CASE("close 语义：push 拒绝 / pop 排空后拒绝")
{
    BoundedQueue<int> q(4);
    REQUIRE(q.push_wait_for(7, 0));
    q.close();
    CHECK(q.closed());

    // 已关闭：入队即拒
    int v = 8;
    CHECK_FALSE(q.push_wait_for(std::move(v), 0));

    // 已有数据仍可排空
    int out = 0;
    REQUIRE(q.pop_wait(out, 0));
    CHECK(out == 7);
    // 排空后：pop 返回 false
    CHECK_FALSE(q.pop_wait(out, 0));
}

TEST_CASE("reopen 后可复用")
{
    BoundedQueue<int> q(2);
    q.close();
    q.reopen();
    CHECK_FALSE(q.closed());
    CHECK(q.push_wait_for(1, 0));
    int out = 0;
    REQUIRE(q.pop_wait(out, 0));
    CHECK(out == 1);
}

TEST_CASE("跨线程生产消费不丢不重")
{
    BoundedQueue<int> q(8);
    std::thread prod([&q] {
        for (int i = 0; i < kStressCount; ++i) {
            // 循环变量入队即 move（下一轮重新赋值）
            while (!q.push_wait_for(std::move(i), 5)) {
                if (q.closed()) {
                    return;
                }
            }
        }
    });

    int sum = 0;
    int got = 0;
    int out = 0;
    while (got < kStressCount) {
        if (q.pop_wait(out, 100)) {
            sum += out;
            ++got;
        }
    }
    prod.join();
    CHECK(got == kStressCount);
    CHECK(sum == kStressCount * (kStressCount - 1) / 2);
}
