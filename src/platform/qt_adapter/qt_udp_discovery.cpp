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
    if (!socket_.joinMulticastGroup(group)) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, opt_.log_tag,
                 "加入组播组失败（跨网段？切静态映射模式，Doc07 §7）");
        return false;
    }
    timer_.start(opt_.heartbeat_ms);
    running_ = true;
    send_heartbeat();  // 启动即报一次，缩短发现延迟
    SCLD_LOG(log_, foundation::LogLevel::Info, opt_.log_tag,
             "组播发现已启动 group=" + opt_.multicast_addr
                 + " port=" + std::to_string(opt_.multicast_port));
    return true;
}

void UdpDiscoveryAdapter::stop() {
    timer_.stop();
    socket_.leaveMulticastGroup(QHostAddress(QString::fromStdString(opt_.multicast_addr)));
    running_ = false;
}

void UdpDiscoveryAdapter::update_self(const std::string& alias, uint16_t ws_port) {
    opt_.self_alias = alias;
    opt_.ws_port = ws_port;
}

void UdpDiscoveryAdapter::set_status_busy(bool busy) {
    opt_.self_status_busy = busy;
}

QByteArray UdpDiscoveryAdapter::build_heartbeat() const {
    // [magic 2B][alias_len 1B][alias][ipv4 4B][port 2B][version 2B][status 1B]
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
    out.append(static_cast<char>(0));  // version 1.0
    out.append(static_cast<char>(opt_.self_status_busy ? 0x01 : 0x00));
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

        if (aliases_ != nullptr) {
            aliases_->upsert_device(dev);  // 冲突/上/下线事件由服务发布（Doc07 §5）
        }
    }
}

}  // namespace sc::localdesk::platform
