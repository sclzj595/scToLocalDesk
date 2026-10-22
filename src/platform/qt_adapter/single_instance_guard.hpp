// ============================================================
// scToLocalDesk | src/platform/qt_adapter/single_instance_guard.hpp
// 单实例守卫（Doc02 §5）：QLocalServer/QLocalSocket 实现
// Windows = 命名管道 / Unix = domain socket，均免残留清理，跨平台一致
// key 按 exe 名派生：正式单 exe 全局单实例；QML/Widgets 对照构建可并存
// 二次启动行为：连上首实例 → 投递 raise 请求 → 退出（首实例回调唤起主窗）
// ============================================================
#pragma once

#include <functional>

#include <QString>

class QLocalServer;

namespace sc::localdesk::platform {

class SingleInstanceGuard final {
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    /// true = 首实例（已开始监听）；false = 已有实例在跑（已投递唤起请求，调用方应退出）
    [[nodiscard]] bool acquire(const QString& key);

    /// 二次启动到达时唤起主窗口（首实例内、主线程事件循环中回调）
    void set_raise_callback(std::function<void()> cb);

private:
    void on_new_connection();

    QLocalServer* server_{nullptr};
    std::function<void()> raise_cb_;
};

}  // namespace sc::localdesk::platform
