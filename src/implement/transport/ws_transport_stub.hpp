// ============================================================
// scToLocalDesk | src/implement/transport/ws_transport_stub.hpp
// WsTransport 本地回环桩：Qt 安装缺 QtWebSockets 模块时的
// 编译降级（SCTOLOCALDESK_UI_PREVIEW）。类名/构造签名与真实现
// 一致，调用方（main.cpp）零改动；用 Qt Maintenance Tool 补装
// QtWebSockets 后 CMake 自动切回 ws_transport.hpp 真实现
// ============================================================
#pragma once

#include <QString>

#include <string>
#include <vector>

#include "core/interface/itransport.hpp"

namespace sc::localdesk {

class WsTransport final : public ITransport {
public:
    explicit WsTransport(const QString&) {
    }

    [[nodiscard]] bool start_server(uint16_t) override {
        return false;
    }
    void connect_client(const std::string&, uint16_t) override {
    }
    void send_binary(const std::vector<uint8_t>&) override {
    }
    void close() override {
    }
    void set_message_callback(OnMessageCb) override {
    }
    void set_state_callback(OnStateCb) override {
    }
};

}  // namespace sc::localdesk
