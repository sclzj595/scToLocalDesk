# Doc05 通信协议规范（TLV 二进制协议、心跳、会话、文件传输）

> 版本：V2.0（2026-10-21 优化：补全 SESSION_CTRL 子类型表、文件分片字段表，新增无人值守鉴权扩展）

## 1. 协议总则

TLV 二进制协议，和底层传输无关（WebSocket / RawTcp 都可以承载）；payload 载荷 AES-GCM 加密。

```
报文头部：
[ 1 Byte MsgType ][ 4 Byte Big-Endian BodyLen ][ Body ]
```

MsgType 枚举定义：

```cpp
enum class MsgType : uint8_t
{
    MOUSE_KEY = 0x01,       // 键鼠事件
    SCREEN_FRAME = 0x02,    // 屏幕编码帧
    FILE_CHUNK = 0x03,      // 文件分片
    SESSION_CTRL = 0x04,    // 会话控制：申请、同意、拒绝、断开、保活
    CONFIG_SYNC = 0x05,     // 配置同步
};
```

## 2. UDP 心跳包（明文，不加密）

心跳包结构体：

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| magic | 2 B | 固定 0x534C（"SL"），过滤无关组播报文 |
| alias_len | 1 B | 别名字节数（UTF-8） |
| alias | ≤60 B | 设备别名 |
| ipv4 | 4 B | 本机内网 IP |
| port | 2 B | WebSocket 监听端口 |
| version | 2 B | 协议版本（高 8 位主 / 低 8 位次） |
| status | 1 B | 0x00 空闲可协助，0x01 会话中 |

- 心跳周期：3 s（可配置 `network.heartbeat_interval_s`，见 Doc17）
- 离线判定：15 s 无心跳（可配置 `network.offline_timeout_s`）
- status=0x01 时 IT 设备列表显示"会话中"，控制端发起前可提示

## 3. SESSION_CTRL 子类型（Body 首字节 ctrl_subtype）

| 子类型 | 值 | 方向 | Body 内容 |
| --- | --- | --- | --- |
| REQUEST_CONNECT | 0x01 | 控 → 被 | alias_len + alias + flags(1B) + [proof_len(2B) + proof] |
| APPROVED | 0x02 | 被 → 控 | 空（会话建立） |
| REJECT | 0x03 | 被 → 控 | reason(1B)：0x00 人工拒绝，0x01 密码校验失败，0x02 会话占用，0x03 其他 |
| CLOSE | 0x04 | 双向 | reason(1B)：0x00 正常，0x01 异常 |
| KEEPALIVE | 0x05 | 双向 | 空（WebSocket 层之上保活） |
| AUTH_CHALLENGE | 0x06 | 被 → 控 | challenge(16B 随机数) |
| AUTH_PROOF | 0x07 | 控 → 被 | proof(32B HMAC) |

REQUEST_CONNECT 的 flags 位定义：bit0 = unattended（控制端请求走无人值守自动同意通道）；bit1~bit7 保留。

## 4. 会话流程报文顺序

1. 控制端发送 `SESSION_CTRL::REQUEST_CONNECT`
2. 被控端收到，弹出确认窗口（人工模式）或进入无人值守鉴权（flags.unattended=1 且本机开关开启，见 §6）
3. 用户同意：被控回复 `SESSION_CTRL::APPROVED`；拒绝：`SESSION_CTRL::REJECT`
4. 会话建立后，双向收发键鼠、画面帧、文件分片
5. 任意一方发送 `SESSION_CTRL::CLOSE` 关闭会话

补充规则：

- PendingApprove 状态等待人工响应超时 `security.session_request_timeout_s`（默认 30 s）自动回复 REJECT(0x03)
- 同一被控端存在活跃会话时，新 REQUEST_CONNECT 直接回复 REJECT(0x02)

## 5. 文件分片协议

FILE_CHUNK Body 字段：

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| file_id | 16 B | 每个文件传输的唯一 ID（随机生成） |
| chunk_index | 4 B | 分片序号，从 0 开始 |
| total_chunks | 4 B | 总分片数 |
| is_last | 1 B | 最后一片置 1 |
| data_len | 4 B | 本片数据长度 |
| data | 变长 | 分片数据（默认 256 KB，可配置 `file.chunk_size_kb`） |

- 文件名、大小等元信息随首个分片前的 FILE_META 字段（file_id + name + size）下发；接收端重组、按 total_chunks 与总大小校验完整性，上报进度事件
- 接收端校验失败：丢弃该 file_id 已收分片，请求重传

## 6. 无人值守鉴权扩展（V1.1，详见 Doc16）

控制端在 REQUEST_CONNECT 中置 flags.bit0=unattended 时，被控端不弹确认框，改走挑战应答：

```
控制端                              被控端（无人值守已开启）
   │ REQUEST_CONNECT(unattended) ──────▶│
   │◀───── AUTH_CHALLENGE(16B 随机) ────│  随机数一次性，10s 内有效
   │ AUTH_PROOF(HMAC-SHA256) ──────────▶│  proof = HMAC(challenge, K)
   │◀───── APPROVED / REJECT(0x01) ─────│  校验通过自动同意
```

- K = PBKDF2-HMAC-SHA256(password, salt)，salt 与哈希落盘 `security.unattended_salt` / `security.unattended_password_hash`，明文密码不落盘、不上网
- 挑战一次性防重放；密码错误 REJECT(0x01) 并失败计数（V1.2 引入锁定）
- 本机无人值守开关关闭时收到 flags.unattended=1：直接 REJECT(0x03)，不进入挑战流程

## 7. CONFIG_SYNC（预留）

会话内画质协商帧率 / 编码参数使用；V1 仅支持会话前静态配置，报文格式预留。
