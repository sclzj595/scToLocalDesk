// ============================================================
// scToLocalDesk | src/platform/win32/single_instance.cpp
// Win32 单实例锁：命名互斥体，二次启动唤起已有窗口（Doc02 §5.4）
// ============================================================
#include <windows.h>

#include <string>

namespace sc::localdesk::platform {

class SingleInstanceGuard final {
public:
    explicit SingleInstanceGuard(std::wstring mutex_name)
        : mutex_name_(std::move(mutex_name)) {
        handle_ = CreateMutexW(nullptr, TRUE, mutex_name_.c_str());
        already_running_ = (handle_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);
    }

    ~SingleInstanceGuard() {
        if (handle_ != nullptr) {
            ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] bool already_running() const { return already_running_; }

    /// 唤起已运行实例的主窗口（广播自定义消息）
    void activate_existing() const {
        // TODO(V1.0): FindWindowW + PostMessage(SC_SHOW_MAIN)
        // 配合 Qt 主窗口 nativeEvent 处理还原/置顶
    }

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

private:
    std::wstring mutex_name_;
    HANDLE handle_{nullptr};
    bool already_running_{false};
};

}  // namespace sc::localdesk::platform
