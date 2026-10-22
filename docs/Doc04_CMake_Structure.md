# Doc04 项目目录结构 & CMake 工程方案

> 版本：V2.0（2026-10-21 优化：目录补充 auth 鉴权模块、Doc16/17 文档位、config.ini 示例）

## 目录结构

```
scToLocalDesk/
├── CMakeLists.txt                 # 主CMake，支持双编译目标：core静态库 / qt exe
├── .clang-format
├── .clang-tidy
├── config/
│   ├── project_config.h.in
│   └── config.ini.example         # 全量配置示例，契约见 Doc17
├── 3rdparty/
│   ├── spdlog/
│   ├── fmt/
│   └── ffmpeg/
├── src/
│   ├── foundation/                # 基础层，纯C++，无Qt
│   │   ├── buffer.hpp
│   │   ├── event_bus.hpp
│   │   ├── thread_pool.hpp
│   │   ├── config_parser.hpp
│   │   └── logger/
│   │       ├── ilogger.hpp
│   │       └── spdlog_logger.cpp
│   ├── core/                      # 核心抽象、协议、业务模型，纯C++无Qt
│   │   ├── interface/             # ITransport IDiscovery IAuthenticator 等全部抽象头文件
│   │   ├── protocol/
│   │   ├── auth/                  # 手动确认 / 无人值守密码鉴权策略（Doc09、Doc16）
│   │   ├── crypto/
│   │   ├── discovery/
│   │   ├── session/
│   │   └── filetransfer/
│   ├── service/
│   │   ├── alias_service.hpp
│   │   ├── remote_session_service.hpp
│   │   └── file_service.hpp
│   ├── implement/
│   │   ├── transport/
│   │   │   ├── ws_transport.hpp
│   │   │   └── tcp_transport.hpp
│   │   ├── capture/
│   │   │   ├── dxgi_capture.hpp
│   │   │   └── wgc_capture.hpp
│   │   └── encoder/
│   │       ├── mjpeg_encoder.hpp
│   │       └── h264_encoder.hpp
│   ├── platform/
│   │   ├── qt_adapter/
│   │   │   ├── qt_event_bridge.cpp
│   │   │   ├── qt_udp_discovery.cpp
│   │   │   ├── qt_tray.cpp
│   │   │   └── qml_bridge.cpp
│   │   └── win32/
│   │       ├── single_instance.cpp
│   │       └── auto_start.cpp
│   ├── ui/
│   │   ├── qml/
│   │   │   ├── MainPage.qml
│   │   │   ├── ControlView.qml
│   │   │   ├── ConfirmDialog.qml
│   │   │   ├── FileTransferPanel.qml
│   │   │   └── SettingPage.qml    # 设置页，布局与配置契约见 Doc17
│   │   └── ui_main.cpp
│   └── main.cpp
├── assets/
├── tests/
│   ├── core_test/
│   ├── protocol_test/
│   └── transport_mock_test/
└── docs/
    ├── README.md
    ├── Doc01_Overview.md
    ├── Doc02_PRD.md
    ├── Doc03_Architecture.md
    ├── Doc04_CMake_Structure.md
    ├── Doc05_Protocol.md
    ├── Doc06_Interface.md
    ├── Doc07_Discovery.md
    ├── Doc08_Transport.md
    ├── Doc09_Session.md
    ├── Doc10_ScreenCapture.md
    ├── Doc11_FileTransfer.md
    ├── Doc12_Foundation.md
    ├── Doc13_UI_QML.md
    ├── Doc14_TestPlan.md
    ├── Doc15_Deploy_Guide.md
    ├── Doc16_Unattended_Mode.md   # 无人值守模式设计
    └── Doc17_Settings_Page.md     # 设置页配置规范
```

## CMake 编译开关

| 开关 | 说明 |
| --- | --- |
| `SCTOLOCALDESK_BUILD_CORE_ONLY=ON` | 只编译 `sc_localdesk_core.lib`，无 Qt，可给其他 C++ 项目复用 |
| `SCTOLOCALDESK_BUILD_QT_APP=ON` | 编译带 QML 界面的 `scToLocalDesk.exe` |

## 编译约束

1. `src/core/`、`src/foundation/`、`src/service/` 三个目录的 target 不得链接 Qt，CMake 层用 target_link_libraries 白名单保证
2. FFmpeg 为可选依赖：`SCTOLOCALDESK_WITH_FFMPEG=ON` 才编入 h264_encoder，默认仅 MJPG
3. 单元测试 target 仅依赖 core + foundation + gtest，保证无网络、无 Qt 可跑
