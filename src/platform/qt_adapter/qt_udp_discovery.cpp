// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_udp_discovery.cpp
// UDP 心跳实现：magic 0x534C 报文（Doc05 §2），收发均在主线程事件循环
// ============================================================
#include "platform/qt_adapter/qt_udp_discovery.hpp"

#include <QHostAddress>
#include <QNetworkInterface>

#include <foundation/logger/ilogger.hpp>

#include <service/alias_service.hpp>

namespace sc::localdesk::platform {

namespace {
constexpr uint16_t kHeartbeatMagic = 0x534C;  // "SL"
constexpr uint8_t kTlvCode = 0x10;            // 可选体类型：9 位连接码（4B 大端）

void append_tlv_u32(QByteArray& out, uint8_t type, uint32_t v) {
    out.append(static_cast<char>(type));
    out.append(static_cast<char>(4));
    for (int s = 24; s >= 0; s -= 8) {
        out.append(static_cast<char>((v >> s) & 0xFF));
    }
}

/// 组播出口网卡选择：第一个 Up+Running+CanBroadcast 且带 IPv4 的网卡，
/// 排除点对点 TUN（WireGuard/clash 等带 CanPointToPoint 标志）——这类
/// 接口常以低 metric 抢走默认组播出口，同机双端互相收不到心跳、跨机
/// 场景组播进隧道直接丢失。设备发现语义上只属于局域网广播域，故显式
/// 绑定广播型非隧道网卡
QNetworkInterface pick_lan_iface() {
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& i : ifaces) {
        const auto f = i.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning)
            || !(f & QNetworkInterface::CanBroadcast)
            || (f & QNetworkInterface::IsLoopBack)
            || (f & QNetworkInterface::IsPointToPoint)) {
            continue;
        }
        for (const auto& e : i.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                return i;
            }
        }
    }
    return {};
}
}  // namespace

UdpDiscoveryAdapter::UdpDiscoveryAdapter(Options opt, AliasService* aliases,
                                         foundation::ILogger* logger, QObject* parent)
    : QObject(parent),
      opt_(std::move(opt)),
      aliases_(aliases),
      log_(logger) {
    connect(&timer_, &QTimer::timeout, this, &UdpDiscoveryAdapter::send_heartbeat);
    connect(&socket_, &QUdpSocket::readyRead, this, &UdpDiscoveryAdapter::on_ready_read);
}

bool UdpDiscoveryAdapter::start() {
    const QHostAddress group(QString::fromStdString(opt_.multicast_addr));
    if (!socket_.bind(QHostAddress::AnyIPv4, opt_.multicast_port,
                      QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        SCLD_LOG(log_, foundation::LogLevel::Error, opt_.log_tag,
                 "组播端口绑定失败 port=" + std::to_string(opt_.multicast_port));
        return false;
    }
    const QNetworkInterface iface = pick_lan_iface();
    const bool joined = iface.isValid()
                            ? socket_.joinMulticastGroup(group, iface)
                            : socket_.joinMulticastGroup(group);
    if (!joined) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, opt_.log_tag,
                 "加入组播组失败（跨网段？切静态映射模式，Doc07 §7）");
        return false;
    }
    if (iface.isValid()) {
        socket_.setMulticastInterface(iface);
    }
    timer_.start(opt_.heartbeat_ms);
    running_ = true;
    send_heartbeat();  // 启动即报一次，缩短发现延迟
    SCLD_LOG(log_, foundation::LogLevel::Info, opt_.log_tag,
             "组播发现已启动 group=" + opt_.multicast_addr
                 + " port=" + std::to_string(opt_.multicast_port)
                 + " iface=" + (iface.isValid()
                                    ? iface.humanReadableName().toStdString()
                                    : "default"));
    return true;
}

void UdpDiscoveryAdapter::stop() {
    timer_.stop();
    socket_.leaveMulticastGroup(QHostAddress(QString::fromStdString(opt_.multicast_addr)));
    socket_.close();  // 关闭套接字：恢复服务 start() 需干净重 bind
    running_ = false;
}

void UdpDiscoveryAdapter::update_self(const std::string& alias, uint16_t ws_port) {
    opt_.self_alias = alias;
    opt_.ws_port = ws_port;
}

void UdpDiscoveryAdapter::set_heartbeat_interval(int ms) {
    opt_.heartbeat_ms = ms;
    if (running_) {
        timer_.start(ms > 500 ? ms : 500);  // 下限 500ms 防误配打爆网络
    }
}

void UdpDiscoveryAdapter::set_status_busy(bool busy) {
    opt_.self_status_busy = busy;
}

QByteArray UdpDiscoveryAdapter::build_heartbeat() const {
    // [magic 2B][alias_len 1B][alias][ipv4 4B][port 2B][version 2B][status 1B]
    //  + 可选 TLV（旧端解析器只读固定字段，尾部多余字节天然向后兼容，Doc18 §3.4）
    QByteArray out;
    const std::string alias = opt_.self_alias;
    out.append(static_cast<char>(kHeartbeatMagic >> 8));
    out.append(static_cast<char>(kHeartbeatMagic & 0xFF));
    out.append(static_cast<char>(alias.size()));
    out.append(alias.data(), static_cast<qsizetype>(alias.size()));

    // 取第一个非回环 IPv4
    QHostAddress local;
    for (const auto& addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol
            && !addr.isLoopback() && !addr.isLinkLocal()) {
            local = addr;
            break;
        }
    }
    const quint32 ip = local.protocol() == QAbstractSocket::IPv4Protocol
                           ? local.toIPv4Address()
                           : 0;
    for (int s = 24; s >= 0; s -= 8) {
        out.append(static_cast<char>((ip >> s) & 0xFF));
    }
    out.append(static_cast<char>(opt_.ws_port >> 8));
    out.append(static_cast<char>(opt_.ws_port & 0xFF));
    // version 2B（高 8 位主/低 8 位次，Doc05 §2；此前漏发次版本致 TLV 错位——FIX）
    out.append(static_cast<char>(0));  // major 1.0
    out.append(static_cast<char>(0));  // minor
    out.append(static_cast<char>(opt_.self_status_busy ? 0x01 : 0x00));
    if (opt_.self_code != 0) {
        append_tlv_u32(out, kTlvCode, opt_.self_code);
    }
    return out;
}

void UdpDiscoveryAdapter::send_heartbeat() {
    const auto packet = build_heartbeat();
    socket_.writeDatagram(packet,
                          QHostAddress(QString::fromStdString(opt_.multicast_addr)),
                          opt_.multicast_port);
}

void UdpDiscoveryAdapter::on_ready_read() {
    while (socket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<qsizetype>(socket_.pendingDatagramSize()));
        QHostAddress sender;
        socket_.readDatagram(datagram.data(), datagram.size(), &sender);
        if (datagram.size() < 3) {
            continue;
        }
        // 解析（畸形丢弃，Doc07 §9.4）
        const auto* p = reinterpret_cast<const uint8_t*>(datagram.constData());
        const uint16_t magic = static_cast<uint16_t>((p[0] << 8) | p[1]);
        if (magic != kHeartbeatMagic) {
            continue;
        }
        const uint8_t alias_len = p[2];
        if (alias_len == 0 || datagram.size() < 3 + alias_len + 4 + 2 + 2 + 1) {
            continue;
        }
        DeviceInfo dev;
        dev.alias.assign(reinterpret_cast<const char*>(p) + 3, alias_len);
        size_t pos = 3 + alias_len;
        dev.ip = QString("%1.%2.%3.%4")
                     .arg(p[pos])
                     .arg(p[pos + 1])
                     .arg(p[pos + 2])
                     .arg(p[pos + 3])
                     .toStdString();
        pos += 4;
        dev.port = static_cast<uint16_t>((p[pos] << 8) | p[pos + 1]);
        pos += 2;
        dev.version = static_cast<uint16_t>((p[pos] << 8) | p[pos + 1]);
        pos += 2;
        dev.status = p[pos];
        ++pos;

        // 可选 TLV 序列：[type 1B][len 1B][value]，未知类型跳过（向前兼容）
        while (pos + 2 <= static_cast<size_t>(datagram.size())) {
            const uint8_t type = p[pos];
            const uint8_t len = p[pos + 1];
            if (pos + 2 + len > static_cast<size_t>(datagram.size())) {
                break;  // 半截 TLV：丢弃剩余
            }
            if (type == kTlvCode && len == 4) {
                dev.code = (static_cast<uint32_t>(p[pos + 2]) << 24)
                         | (static_cast<uint32_t>(p[pos + 3]) << 16)
                         | (static_cast<uint32_t>(p[pos + 4]) << 8)
                         | static_cast<uint32_t>(p[pos + 5]);
            }
            pos += 2 + len;
        }

        if (aliases_ != nullptr) {
            aliases_->upsert_device(dev);  // 冲突/上/下线事件由服务发布（Doc07 §5）
        }
    }
}

}  // namespace sc::localdesk::platform
