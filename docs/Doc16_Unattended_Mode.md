# Doc16 模块详细设计：无人值守模式（Unattended，V1.1）

> 版本：V1.0（2026-10-21 新增。回应"IT 需要在被控端无人时接入"的运维场景，在不破坏"默认人工确认"安全基线的前提下提供受控的自动同意能力）

## 1. 定位

无人值守模式 = 被控端**显式开启**的自动同意机制：控制端凭**独立访问密码**通过挑战应答鉴权后，被控端跳过人工确认弹窗直接建立会话。

适用场景：下班后服务器 / 展台电脑 / 工位无人时，IT 远程接入处理故障。

## 2. 安全红线（不可妥协）

1. **默认关闭**：全新安装必须人工在设置页开启，不存在出厂即自动同意
2. **独立密码**：访问密码与 Windows 账户无关，仅存本机；开启模式时强制设置，长度 ≥ 8 且含字母 + 数字
3. **明文不落盘**：只存 PBKDF2-HMAC-SHA256(密码, salt) 派生密钥派生的哈希 + 随机盐；salt 每次设置密码时重新生成（16 B）
4. **明文不上网**：密码本身永不在网络传输；采用挑战应答（Doc05 §6），proof = HMAC-SHA256(challenge, K)，K = PBKDF2(password, salt)，challenge 单次有效 10 s，防重放
5. **全程审计**：每次自动同意产生 Toast 轻提示 + 托盘徽标 + 结构化日志（`Evt_UnattendedLogin`：对方别名、时间、会话时长）
6. **人工始终优先**：任何无人值守会话中被控端悬浮断开按钮可用，一键切断
7. **无找回后门**：忘记密码只能本机在位人员进设置页重设（Doc15 §5）
8. **失败计数**：密码错误拒绝并计数（V1.1 记录日志；V1.2 引入 5 次失败锁 10 分钟 + 应答延迟）

## 3. 鉴权流程（时序）

```
控制端                                被控端
   │ REQUEST_CONNECT(flags.unattended=1)
   │ ─────────────────────────────────▶│ 开关开启？
   │                                   │  否 → REJECT(0x03)，不挑战
   │◀──────── AUTH_CHALLENGE(16B) ─────│ 是 → 生成一次性 challenge（10s 有效）
   │ proof = HMAC(challenge, K)        │
   │ AUTH_PROOF(32B) ─────────────────▶│ verify：PBKDF2 重算 K + HMAC 比对（常数时间比较）
   │◀──────── APPROVED ────────────────│ 校验通过 → Toast + 审计日志 + 会话建立
   │◀──────── REJECT(0x01) ────────────│ 校验失败 → 失败计数 +1，可重新发起（重新挑战）
```

- 与人工模式互斥分支：flags.unattended=0 一律走 ConfirmDialog（Doc09 §3）
- 被控端**正在会话中**：无论模式一律 REJECT(0x02)（单会话红线不变）
- 挑战超时 / proof 长度错误：判失败，REJECT(0x01)

## 4. 与会话状态机的融合（Doc09）

PendingApprove 增加两个子状态：

```
PendingApprove
 ├─ WaitForUser        人工模式：等 ConfirmDialog
 └─ WaitForUnattended  无人值守：Challenge 已发，等 PROOF（10s 超时）
```

关闭无人值守开关：仅影响**新连接**的鉴权路由，进行中会话不中断（warn 日志记录）。

## 5. 核心类设计

```cpp
// src/core/auth/unattended_authenticator.hpp（实现 IAuthenticator，Doc06 §7）
class UnattendedPasswordAuthenticator : public IAuthenticator
{
public:
    UnattendedPasswordAuthenticator(std::shared_ptr<ICrypto> crypto,
                                    std::shared_ptr<IConfigRepository> repo);
    AuthResult decide(const AuthRequest& req) override;      // 开关判定 → StartChallenge
    bool verify_proof(std::string_view challenge,
                      std::string_view proof) override;      // 常数时间比较
    std::array<uint8_t,16> issue_challenge();                // 随机数 + 时间窗
    void set_password(const std::string& pwd);               // 触发 PBKDF2 + 落盘（线程池执行）
};
```

- SessionService 通过 `AuthPolicyRouter` 按请求 flags 与本机开关路由到 ManualConfirm / Unattended 实现（Doc03 §7）
- 常数时间比较防时序侧信道；PBKDF2 迭代 ≥ 10 万，放线程池避免卡 UI

## 6. 配置契约（唯一权威见 Doc17）

| key | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `security.unattended_enabled` | bool | false | 总开关；false 时 flags.unattended 请求直接 REJECT(0x03) |
| `security.unattended_salt` | hex string | 空 | 16 B 随机盐，设置密码时生成 |
| `security.unattended_password_hash` | hex string | 空 | 派生密钥哈希（PBKDF2 输出再 SHA256），不存明文 |
| `security.unattended_fail_count` | int | 0 | 失败计数（V1.1 仅记录） |
| `security.session_request_timeout_s` | int | 30 | 人工模式等待超时（与 Doc09 共用） |

## 7. UI 元素（Doc13 §3.2）

- 设置页 → 安全页签：无人值守卡片（开关 + 密码设置对话框：两次输入 + 强度提示）
- 开启中：托盘图标加徽标；控制端连接入口显示"对方已开启免确认接入（需访问密码）"提示与密码输入框
- 会话内：被控 Toast"【别名】已通过密码接入"，悬浮断开按钮照常

## 8. 风险评估与对策

| 风险 | 对策 |
| --- | --- |
| 密码暴力尝试 | 挑战一次性 + 10s 窗口天然限速；失败计数；V1.2 锁定 + 应答延迟 |
| 离线字典攻击 config.ini | PBKDF2 高迭代成本；建议员工设置强密码（强度提示） |
| 内网嗅探重放 | challenge 一次性 + HMAC；整链路另有 AES-GCM（Doc02 §6.2） |
| 开关忘关导致长期裸奔 | 托盘徽标常显 + 每次接入 Toast；V1.2 评估"自动过期"策略 |
| 时序侧信道 | 常数时间比较 |

## 9. 测试要点（准入用例，Doc14 §2.6）

1. 开关关闭 + unattended 请求 → REJECT(0x03)，无挑战报文
2. 正确密码 → APPROVED，`Evt_UnattendedLogin` 日志字段完整（alias、时间、session_id）
3. 错误密码 → REJECT(0x01)，fail_count 累加；重设密码后旧密码失效
4. 挑战超时 / proof 篡改 1 bit → 失败
5. 会话中人工断开立即生效；关闭开关不影响进行中会话
6. PBKDF2 在线程池执行，UI 不卡顿；config.ini 中无明文密码（安全扫描项）
