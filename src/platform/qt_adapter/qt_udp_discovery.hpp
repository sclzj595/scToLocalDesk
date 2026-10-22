// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_udp_discovery.hpp
// UDP 心跳收发适配器（QUdpSocket，Doc07 §8）：Qt 网络依赖收敛于此
// 参考 YD/NetComm/Multicast.h 的收发模型（发送线程+组播组），以 Qt 定时器实现
// ============================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <string>

#include "core/interface/idiscovery.hpp"

namespace sc::localdesk {

class AliasService;

namespace foundation {
class ILogger;
}

namespace platform {

class UdpDiscoveryAdapter final : public QObject {
    Q_OBJECT

public:
    struct Options
    {
        std::string multicast_addr{"239.255.88.99"};  // Doc07 §4
        uint16_t multicast_port{5388};
        std::string self_alias;
        uint16_t ws_port{8899};       // 心跳里告知对端会话端口
        int heartbeat_ms{3000};       // Doc17 network.heartbeat_interval_s
        std::string log_tag{"discovery"};
        bool self_status_busy{false}; ///< 0x01 会话中（Doc05 §2）
    };

    UdpDiscoveryAdapter(Options opt, AliasService* aliases, foundation::ILogger* logger,
                        QObject* parent = nullptr);

    /// 启动：join 组播组 + 周期心跳；返回 false = 组播加入失败（上层切静态映射）
    [[nodiscard]] bool start();
    void stop();

    void update_self(const std::string& alias, uint16_t ws_port);
    void set_status_busy(bool busy);  ///< 会话中：status=0x01（Doc05 §2）

private slots:
    void send_heartbeat();
    void on_ready_read();

private:
    [[nodiscard]] QByteArray build_heartbeat() const;

    Options opt_;
    AliasService* aliases_;
    foundation::ILogger* log_;
    QUdpSocket socket_;
    QTimer timer_;
    bool running_{false};
};

}  // namespace platform
}  // namespace sc::localdesk
