# Doc03 系统总体架构设计文档

> 版本：V2.0（2026-10-21 优化：扩展点新增 IAuthenticator 鉴权策略，支撑无人值守模式的可插拔鉴权）

## 1. 架构设计原则

依赖倒置、分层隔离、面向接口编程；Core 纯 C++ 无 Qt，Qt 作为平台适配插件；事件总线解耦模块；组件可插拔，便于单元测试、替换底层实现。

## 2. 分层结构图

```
┌─────────────────────────────────────────────┐
│ Application Entry 应用入口 main.cpp         │
├─────────────────────────────────────────────┤
│ UI Layer（Qt + QML）【适配层，可选组件】      │
├─────────────────────────────────────────────┤
│ Service Layer 业务服务层                    │
├─────────────────────────────────────────────┤
│ Core Interface Layer 核心抽象接口层         │
├─────────────────────────────────────────────┤
│ Implementation Layer 组件实现层            │
├─────────────────────────────────────────────┤
│ Foundation 基础基础设施（纯C++，无Qt）       │
└─────────────────────────────────────────────┘
```

### 分层职责

1. Foundation 基础设施：buffer、线程池、事件总线、spdlog 日志封装、配置解析、基础工具；**无 Qt**
2. Core Interface：全部抽象接口头文件 `ITransport / IDiscovery / IScreenCapturer / IEncoder / ICrypto / IFileTransfer / IAuthenticator`；**无 Qt**
3. Implementation：接口具体实现，分为平台无关实现、平台适配器实现
4. Service 业务服务层：AliasService、RemoteSessionService、FileService，依赖 Core 抽象接口；**无 Qt**
5. Platform 平台适配层：Qt 适配器、Win32 原生适配；**所有 Qt 头文件、Windows 系统 API 收敛在此**
6. UI QML 层：界面展示、弹窗、托盘交互、设置页；只订阅事件总线事件，不写业务逻辑
7. App 入口：依赖注入，组装全部组件，串联启动流程

## 3. 核心通信模型

- 发现通道：UDP（QUdpSocket，放在 Qt 适配器），组播心跳，别名 - IP 映射
- 会话通道：WebSocket 长连接（ITransport 默认 WsTransport 实现），TLV 二进制消息多路复用信令、画面、文件分片
- 跨模块通信：线程安全 EventBus，发布订阅模式，替代跨模块 Qt 信号槽耦合

## 4. 线程模型

- UI 主线程：QML 渲染、托盘交互、弹窗、设置页
- 采集编码线程：屏幕捕获、图像编码、帧生产
- Transport IO 线程：WebSocket 收发（Qt 自带 IO 线程，适配器做跨线程投递）
- 全局线程池：文件 IO、AES 加密、PBKDF2 口令计算、计算任务

## 5. 依赖注入 DI 设计

main 阶段统一构造实例，向服务注入抽象接口；单元测试时，可以注入 Mock 实现，不需要真实网络、采集设备。
伪代码示例：

```
auto logger = std::make_shared<sc::localdesk::SpdlogLogger>();
auto bus = std::make_shared<sc::localdesk::EventBus>();
auto transport = std::make_shared<sc::localdesk::WsTransport>(logger, bus);
auto discovery = std::make_shared<sc::localdesk::UdpDiscoveryImpl>(logger, bus);
auto authenticator = std::make_shared<sc::localdesk::AuthPolicyRouter>(logger);   // 手动确认 / 无人值守密码 路由
auto sessionSvc = std::make_shared<sc::localdesk::RemoteSessionService>(transport, discovery, authenticator, logger, bus);
```

## 6. 扩展点（可插拔位置）

| 接口 | 内置实现 | 备注 |
| --- | --- | --- |
| ITransport | WsTransport / RawTcpTransport | 传输层替换（Doc08） |
| IDiscovery | UdpDiscoveryImpl / StaticMapDiscovery | 组播 / 静态映射双模式（Doc07） |
| IScreenCapturer | DXGI DesktopDuplication / Windows Graphics Capture | 自动降级（Doc10） |
| IEncoder | MJpegEncoder / H264Encoder | FFmpeg 可选（Doc10） |
| ICrypto | AesGcmCrypto | Payload 加密 + 口令哈希（Doc16） |
| IAuthenticator | ManualConfirmAuthenticator / UnattendedPasswordAuthenticator | 手动确认（默认）/ 无人值守密码鉴权（Doc09、Doc16） |
| ILogger | SpdlogLogger / ConsoleMockLogger（测试） | 日志（Doc12） |

## 7. 无人值守模式在架构中的落位

- 新增能力不改分层：密码校验是 Core 内 `IAuthenticator` 的一种策略实现；挑战应答报文走既有 SESSION_CTRL 通道（Doc05 §6）
- Qt 无感知：UI 只收到 `Evt_ConnectRequest`（auto_approved 标记）或 `Evt_SessionStart`，不参与密码逻辑
- 详细设计：[Doc16 无人值守模式](./Doc16_Unattended_Mode.md)
