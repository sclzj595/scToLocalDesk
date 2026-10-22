# Doc09 模块详细设计：会话管理、权限与连接鉴权

> 版本：V2.0（2026-10-21 优化：补全会话状态机、鉴权策略路由（含无人值守分支）、超时与资源回收）

## 1. 职责

- 管理远程会话全生命周期：连接请求 → 鉴权 → 数据通道开启 → 关闭回收
- 鉴权策略路由：手动确认（默认）/ 无人值守密码鉴权（V1.1，[Doc16](./Doc16_Unattended_Mode.md)）
- 画面推流、键鼠、文件通道的统一开关（鉴权通过前全部关闭）

## 2. 会话状态机

```
                 ┌────────────────────────────────────────────┐
                 │  REQUEST_CONNECT                            │
Idle ───────────▶│ PendingApprove                              │
                 │  ├─ 人工模式：弹窗等待用户同意/拒绝          │
                 │  └─ 无人值守模式：AUTH_CHALLENGE→PROOF 校验  │
                 └───────┬──────────────┬──────────────────────┘
                  APPROVED│              │ REJECT / 超时
                          ▼              ▼
                      Running         Closing ──▶ Idle
                          │   CLOSE(任一方)/断线/崩溃
                          └──────────────▶ Closing ──▶ Idle
```

状态事件：`Evt_ConnectRequest`、`Evt_SessionStart`、`Evt_SessionClosed`（携带 session_id、对端别名、结束原因、是否无人值守）。

## 3. 鉴权规则（红线）

1. **握手成功 ≠ 会话可用**：WebSocket 连上只是 PendingApprove，未 APPROVED 前不推画面、不收键鼠、不收文件
2. 人工模式：确认弹窗置顶不可遮挡（Doc13），等待超时 `security.session_request_timeout_s`（默认 30 s）自动 REJECT
3. 无人值守模式：仅当本机开关开启且控制端 flags.unattended=1 时走挑战应答；开关关闭直接 REJECT(0x03)；校验失败 REJECT(0x01) 并计数
4. 一个被控端同一时间**只允许一个活跃会话**；占用中收到新请求直接 REJECT(0x02)
5. 策略可插拔：`IAuthenticator` 实现 ManualConfirmAuthenticator / UnattendedPasswordAuthenticator（Doc03 §6），SessionService 只依赖抽象

## 4. 会话内职责

- 为会话分配全局唯一 session_id，注入所有结构化日志（Doc12）
- 启动采集编码管线（Doc10）、键鼠指令分发、文件传输服务（Doc11）
- 桥接 CONFIG_SYNC 预留（画质协商）

## 5. 资源回收（红线）

会话进入 Closing 时，无论正常 / 异常 / 崩溃恢复：

1. 停止采集器、编码器，清空发送队列
2. 终止进行中的文件传输（进度事件置失败）
3. 释放 ITransport 连接；内存归零
4. 发出 `Evt_SessionClosed`，UI 恢复等待态
   采用 RAII 守卫对象保证异常路径不泄漏。

## 6. 线程模型

- 状态机迁移单线程串行（业务线程），EventBus 投递 UI
- 画面帧路径零锁：编码线程 → Transport 发送队列（Doc08）

## 7. 测试要点

1. 全路径状态机：同意、拒绝、超时、占用、断线、双方同时 CLOSE
2. 未 APPROVED 前注入键鼠 / FILE_CHUNK：必须被丢弃并 warn 日志（安全用例，Doc14 §7）
3. 无人值守分支见 Doc16 §9
4. 会话中杀进程重启：无残留端口占用、无泄漏（连续运行 2h 准入，Doc14）
