// ============================================================
// scToLocalDesk | src/core/interface/itransport.hpp
// 传输层抽象契约：WebSocket 默认实现 / RawTcp 预留（Doc06 §1 / Doc08）
// ============================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/interface/types.hpp"

namespace sc::localdesk {

class ITransport {
public:
    virtual ~ITransport() = default;

    /// 被控端：启动监听。端口占用返回 false（Doc08 §2）
    [[nodiscard]] virtual bool start_server(uint16_t port) = 0;

    /// 控制端：向对端发起连接（异步，结果经状态回调上抛）
    virtual void connect_client(const std::string& host, uint16_t port) = 0;

    /// 线程安全：可从编码线程直接调用；内部仅入队（Doc08 §7）
    virtual void send_binary(const std::vector<uint8_t>& data) = 0;

    virtual void close() = 0;

    /// 零拷贝消息缓冲（asio buffer 思想）：shared_ptr 所有权转移代替按值
    /// 拷贝——画面帧 ~1MB/帧 × 30fps 下省出一整条内存带宽；订阅方只读
    using OnMessageCb = std::function<void(std::shared_ptr<const std::vector<uint8_t>>)>;
    using OnStateCb   = std::function<void(TransportState)>;

    /// 回调在 IO 线程触发，实现方保证线程安全；跨线程投递统一走 EventBus
    virtual void set_message_callback(OnMessageCb cb) = 0;
    virtual void set_state_callback(OnStateCb cb) = 0;
};

}  // namespace sc::localdesk
