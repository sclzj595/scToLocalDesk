# Doc08 模块详细设计：传输层（可插拔 ITransport，WebSocket 默认实现）

> 版本：V2.0（2026-10-21 优化：补全状态机、流控水位、重连语义、Mock 测试）

## 1. 默认实现

WsTransport：内部封装 QWebSocketServer / QWebSocket（**仅适配器依赖 Qt**，Core 只见 ITransport）。
预留 RawTcpTransport 实现：切换只需更换注入实例，业务代码零改动（Doc03 §6）。

## 2. 状态机

```
Idle ──start_server()──▶ Listening ──client接入──▶ Connected ──close()/异常──▶ Idle
Idle ──connect_client()─▶ Connecting ──握手成功──▶ Connected
任意态 ──网络错误──▶ Error ──close()──▶ Idle
```

状态变化通过 `OnStateCb` 回调 + `Evt_TransportState` 事件双通道上抛。

## 3. 能力边界

- 二进制消息收发、状态回调；上层不关心 WebSocket 帧、分片、掩码细节
- 一条连接承载全部业务消息（TLV 多路复用，见 Doc05），V1 不做多连接并行
- 心跳：协议层 KEEPALIVE（Doc05 §3）+ WebSocket ping 双保险

## 4. 流控与背压（核心）

| 机制 | 规则 |
| --- | --- |
| 发送队列水位 | 队列 > 8 MB 或积压 > 64 帧：丢弃最旧的 SCREEN_FRAME（只留最新帧），键鼠 / 会话控制 / 文件消息永不丢弃 |
| 文件分片限速 | 单文件传输期间与屏幕流共享带宽提示，进度事件照常上报 |
| 内存红线 | 队列总内存 > 32 MB：主动 CLOSE 会话并 error 日志，防止内存暴涨（Doc02 §7.3） |

## 5. 断线语义

- 网络闪断：Transport 上抛 Error 状态；是否重连由 SessionService 决策（V1 单次会话不自动重连，UI 友好提示"网络中断，会话已断开"）
- 对方退出 / 崩溃：WebSocket close/error → 同上
- 监听端（被控）Listening 态不受单条会话断开影响，继续接受下一次连接

## 6. 配置项（契约见 Doc17）

| key | 默认 | 说明 |
| --- | --- | --- |
| `network.ws_port` | 8899 | 监听端口，启动时占用检测 |
| `network.heartbeat_interval_s` | 3 | KEEPALIVE 周期 |
| `network.send_queue_max_mb` | 8 | 画面帧队列水位 |

## 7. 线程模型

- Qt IO 线程收发；适配器内部用队列 + 信号跨线程投递到业务线程
- `send_binary()` 线程安全，可从编码线程直接调用；内部仅入队，序列化在 IO 线程

## 8. 测试要点

1. Mock ITransport 驱动 SessionService 全状态机（不依赖网络）
2. 大量帧注入 + 慢速对端：队列水位触发丢帧、内存不越红线
3. 端口占用：start_server 失败上抛，UI 提示修改端口
4. 收发 TLV 半包 / 粘包（WebSocket 是消息边界，RawTcp 实现必须处理，见用户风险清单）
