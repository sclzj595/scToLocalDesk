# scToLocalDesk（SC 邻桌）文档中心

> 一句话定位：企业内网局域网远程协助软件，单 EXE 一体端（控制 / 被控二合一），设备别名寻址、连接需被控同意，绿色免安装。

## 项目基础信息（锁定）

| 项 | 内容 |
| --- | --- |
| 项目名称 | scToLocalDesk |
| 中文产品名 | SC 邻桌 |
| 命名空间 | `sc::localdesk` |
| 产物 | `scToLocalDesk.exe`（绿色免安装） |
| 技术栈 | C++17、Qt 6.5.3 + MSVC2019、QML UI、spdlog、CMake |
| 架构原则 | 面向接口、可插拔组件、Core 层剥离 Qt 依赖、纯内网、UDP 别名发现 + WebSocket 会话传输、连接默认人工同意（预留密码保护的无人值守模式）、双向文件传输 |

## 文档清单（Doc01 ~ Doc17）

| 编号 | 文档 | 主题 |
| --- | --- | --- |
| Doc01 | [项目总览 & 立项说明书](./Doc01_Overview.md) | 背景、目标、版本规划、运行基线 |
| Doc02 | [产品需求文档 PRD V2.0](./Doc02_PRD.md) | 角色、业务流程、功能 / 安全 / 非功能需求 |
| Doc03 | [系统总体架构设计](./Doc03_Architecture.md) | 分层、通信模型、线程模型、DI、扩展点 |
| Doc04 | [目录结构 & CMake 工程方案](./Doc04_CMake_Structure.md) | 工程骨架、编译开关 |
| Doc05 | [通信协议规范](./Doc05_Protocol.md) | TLV 报文、会话信令、无人值守鉴权扩展 |
| Doc06 | [核心抽象接口定义](./Doc06_Interface.md) | 全部头文件契约（纯 C++） |
| Doc07 | [设备别名发现服务](./Doc07_Discovery.md) | UDP 心跳、别名映射、静态映射模式 |
| Doc08 | [传输层 ITransport](./Doc08_Transport.md) | WsTransport、状态机、流控 |
| Doc09 | [会话管理、权限与连接鉴权](./Doc09_Session.md) | 会话状态机、鉴权策略、资源回收 |
| Doc10 | [屏幕采集 + 图像编码子系统](./Doc10_ScreenCapture.md) | DXGI/WGC、MJPG/H264、丢帧策略 |
| Doc11 | [双向文件传输子系统](./Doc11_FileTransfer.md) | 分片协议、进度、重试 |
| Doc12 | [基础设施设计](./Doc12_Foundation.md) | spdlog、EventBus、线程池、Buffer、配置 |
| Doc13 | [UI & QML 交互设计](./Doc13_UI_QML.md) | 页面清单、数据流转、小白友好 |
| Doc14 | [测试方案与质量准入](./Doc14_TestPlan.md) | 分层测试、核心用例、准入标准 |
| Doc15 | [部署、打包、运维与排障](./Doc15_Deploy_Guide.md) | 打包、端口放行、排障手册 |
| Doc16 | [无人值守模式设计（V1.1，新增）](./Doc16_Unattended_Mode.md) | 密码鉴权、挑战应答、审计与红线 |
| Doc17 | [设置页配置规范（新增）](./Doc17_Settings_Page.md) | 全量配置项、ini 契约、UI 布局 |

## 阅读顺序

- 决策 / 立项：Doc01 → Doc02
- 架构设计：Doc03 → Doc04 → Doc05 → Doc06
- 模块开发：Doc07 ~ Doc12
- 前端 UI：Doc13 + Doc17
- 质量保障：Doc14
- 部署运维：Doc15 + Doc16

## 文档约定

1. 所有配置项的 key、类型、默认值以 [Doc17](./Doc17_Settings_Page.md) 的 `config.ini` 契约为唯一基准，其余文档引用不得冲突。
2. 协议字段与 MsgType 以 [Doc05](./Doc05_Protocol.md) 为唯一基准；新增能力（如无人值守）必须先在 Doc05 登记，再在各模块文档展开。
3. 术语以 Doc02 术语表为准：DeviceAlias / Discovery / Transport / Session / EventBus / PlatformAdapter。
4. 每份模块文档统一章节：职责 → 状态机 → 配置项 → 事件 → 线程模型 → 边界与异常 → 测试要点。
