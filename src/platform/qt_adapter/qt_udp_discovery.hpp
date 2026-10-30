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
#include <vector>

class QTcpSocket;  // 全局 Qt 前置声明（探活在途连接句柄，Doc19 §6.3）

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
        uint32_t self_code{0};        ///< 9 位连接码（心跳可选 TLV 0x10，Doc18 §3.4）
    };

    /// 手动对端（Doc19 §6.3）：跨 VLAN 广播不可达场景的直连表
    struct ManualPeer
    {
        std::string alias;
        std::string ip;
        uint16_t port{0};
        QTcpSocket* sock{nullptr};  ///< 在途探活连接（无 = 空闲）
    };

    UdpDiscoveryAdapter(Options opt, AliasService* aliases, foundation::ILogger* logger,
                        QObject* parent = nullptr);

    /// 启动：join 组播组 + 周期心跳；返回 false = 组播加入失败（上层切静态映射）
    bool start();
    void stop();

    void update_self(const std::string& alias, uint16_t ws_port);
    /// 心跳间隔即时生效（设置页接线，Doc18 §3.3）；下限 500ms 防误配打爆网络
    void set_heartbeat_interval(int ms);
    void set_status_busy(bool busy);  ///< 会话中：status=0x01（Doc05 §2）
    /// 主动广播一轮心跳（设备列表刷新按钮：立即触发在线表更新）
    void send_heartbeat();

    /// 注入手动对端（Doc19 §6.3，配置装配/UI 添加共用）：去重（同别名更新地址），
    /// 立即以心跳同款条目进在线表（manual 标记），并按心跳周期对 ip:port 做
    /// TCP 探活——成功续住条目，失败主动标记离线。IP 非法/别名空返回 false
    bool add_manual_peer(const std::string& alias, const std::string& ip, uint16_t port);

private slots:
    void on_ready_read();

private:
    QByteArray build_heartbeat() const;
    /// 全量手动对端发起一轮探活（心跳周期驱动 + 添加即探一次）
    void probe_manual_peers();
    /// 探活裁决：ok=续住条目（同款在线注入）；fail=标记离线（Doc19 §6.3）
    void on_probe_result(const std::string& alias, QTcpSocket* sock, bool ok);

    Options opt_;
    AliasService* aliases_;
    foundation::ILogger* log_;
    QUdpSocket socket_;
    QTimer timer_;
    QTimer probe_timer_;             ///< 手动对端探活周期（随心跳周期）
    std::vector<ManualPeer> manual_;  ///< 手动对端表（alias 唯一）
    bool running_{false};
};

}  // namespace platform
}  // namespace sc::localdesk
