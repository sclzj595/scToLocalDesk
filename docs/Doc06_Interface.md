# Doc06 核心抽象接口定义文档（头文件契约）

> 版本：V2.0（2026-10-21 优化：补全 IDiscovery / IScreenCapturer / IEncoder / ICrypto / IFileTransfer / ILogger / IAuthenticator 全套契约）
>
> 全部头文件放在 `src/core/interface/`，纯 C++，无 Qt 依赖，契约式设计；统一范式：接口纯虚 + 虚析构、回调 `std::function`、错误返回 bool / 枚举、不抛异常。

## 1. ITransport

```cpp
namespace sc::localdesk {
class ITransport
{
public:
    virtual ~ITransport() = default;
    virtual bool start_server(uint16_t port) = 0;
    virtual bool connect_client(const std::string& host, uint16_t port) = 0;
    virtual void send_binary(const std::vector<uint8_t>& data) = 0;
    virtual void close() = 0;
    using OnMessageCb = std::function<void(std::vector<uint8_t>)>;
    using OnStateCb = std::function<void(int state)>;
    virtual void set_message_callback(OnMessageCb cb) = 0;
    virtual void set_state_callback(OnStateCb cb) = 0;
};
}
```

## 2. IDiscovery

```cpp
namespace sc::localdesk {
struct DeviceInfo
{
    std::string alias;
    std::string ip;
    uint16_t    port{0};
    uint16_t    version{0};
    uint8_t     status{0};   // 0x00 空闲，0x01 会话中
};

class IDiscovery
{
public:
    virtual ~IDiscovery() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual std::optional<DeviceInfo> resolve(const std::string& alias) = 0;
    virtual std::vector<DeviceInfo> online_devices() const = 0;
    virtual void set_static_map(const std::vector<DeviceInfo>& table) = 0;
};
}
```

## 3. IScreenCapturer / IEncoder

```cpp
namespace sc::localdesk {
struct VideoFrame
{
    const uint8_t* data{nullptr};
    size_t         size{0};
    uint32_t       width{0};
    uint32_t       height{0};
    int64_t        timestamp_us{0};
};

class IScreenCapturer
{
public:
    virtual ~IScreenCapturer() = default;
    virtual bool init() = 0;
    virtual bool grab(VideoFrame& out) = 0;   // 无新帧返回 false，不阻塞
    virtual void shutdown() = 0;
};

struct EncoderConfig
{
    std::string codec{"mjpeg"};   // mjpeg | h264
    int         fps{15};
    int         quality{80};      // MJPG 质量 1~100 / H264 CRP 映射
};

class IEncoder
{
public:
    virtual ~IEncoder() = default;
    virtual bool open(const EncoderConfig& cfg) = 0;
    virtual bool encode(const VideoFrame& in, std::vector<uint8_t>& out) = 0;
    virtual void close() = 0;
};
}
```

## 4. ICrypto

```cpp
namespace sc::localdesk {
class ICrypto
{
public:
    virtual ~ICrypto() = default;
    virtual bool encrypt(const std::vector<uint8_t>& plain,
                         std::vector<uint8_t>& cipher) = 0;
    virtual bool decrypt(const std::vector<uint8_t>& cipher,
                         std::vector<uint8_t>& plain) = 0;
    // 口令派生：PBKDF2-HMAC-SHA256，用于无人值守密码（Doc16），迭代次数 ≥ 10 万
    virtual bool derive_key(const std::string& password,
                            const std::string& salt,
                            std::vector<uint8_t>& key) = 0;
    // HMAC-SHA256：无人值守挑战应答 proof 计算（Doc05 §6）
    virtual bool hmac_sha256(std::string_view key,
                             std::string_view data,
                             std::vector<uint8_t>& mac) = 0;
    // SHA-256 摘要：口令校验值 / 文件完整性
    virtual bool sha256(std::string_view data,
                        std::vector<uint8_t>& digest) = 0;
};
}
```

## 5. IFileTransfer

```cpp
namespace sc::localdesk {
struct FileMeta
{
    std::string file_id;      // 16B 随机 ID 的 hex 表示
    std::string name;         // 文件名（不含路径）
    uint64_t    size{0};
    uint32_t    total_chunks{0};
};

class IFileTransfer
{
public:
    virtual ~IFileTransfer() = default;
    virtual bool send_file(const std::string& abs_path) = 0;
    virtual bool accept_file(const FileMeta& meta, const std::string& save_dir) = 0;
    virtual bool reject_file(const std::string& file_id) = 0;
    virtual void cancel(const std::string& file_id) = 0;
};
}
```

## 6. ILogger

```cpp
namespace sc::localdesk {
enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical };

class ILogger
{
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel lv, std::string_view tag, std::string_view msg) = 0;
    virtual void set_level(LogLevel lv) = 0;
};
}
```

## 7. IAuthenticator（鉴权策略，支撑手动确认与无人值守）

```cpp
namespace sc::localdesk {
struct AuthRequest
{
    std::string peer_alias;
    bool        unattended{false};   // 控制端请求无人值守通道
    // 无人值守挑战应答上下文由 SessionService 持有，Authenticator 只做校验决策
};

enum class AuthResult
{
    NeedManualConfirm,   // 抛给 UI 确认弹窗
    StartChallenge,      // 进入无人值守挑战应答（Doc16）
    Approved,
    Rejected,
};

class IAuthenticator
{
public:
    virtual ~IAuthenticator() = default;
    virtual AuthResult decide(const AuthRequest& req) = 0;
    virtual bool verify_proof(std::string_view challenge,
                              std::string_view proof) = 0;
};
}
```

## 8. 契约红线

1. 接口头文件只允许标准库，禁止 include 任何 Qt / Windows 头
2. 回调可能在内部线程触发，实现方负责线程安全；跨线程投递统一走 EventBus
3. 所有实现必须可在单元测试中以 Mock 替换（gtest），不得隐式依赖全局状态
