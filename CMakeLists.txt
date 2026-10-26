# ============================================================
# scToLocalDesk（SC 邻桌）主 CMake
# 双编译目标：
#   sc_localdesk_core  : 核心静态库，纯 C++17，零 Qt 依赖（默认 ON）
#   scToLocalDesk.exe  : QML 桌面应用（SCTOLOCALDESK_BUILD_QT_APP=ON，需 Qt 6.5.3）
# ============================================================
cmake_minimum_required(VERSION 3.20)

project(scToLocalDesk
    VERSION 0.1.0
    DESCRIPTION "SC 邻桌 - 企业内网局域网远程协助"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源码统一 UTF-8（无 BOM），MSVC 在 CP936 下必须显式声明，
# 否则中文注释/字符串被按 GBK 误读产生 C2001/C4819
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(SCTOLOCALDESK_BUILD_CORE_ONLY "仅编译核心静态库 + headless 冒烟入口（无 Qt）" ON)
option(SCTOLOCALDESK_BUILD_QT_APP    "编译 QML 桌面应用（需 Qt 6.5.3 + MSVC2019）" OFF)
option(SCTOLOCALDESK_BUILD_TESTS     "编译单元测试（doctest，header-only vendored）" OFF)
option(SCTOLOCALDESK_WITH_SPDLOG     "启用 spdlog 日志实现（关闭时使用内置 ConsoleLogger）" OFF)

if(NOT SCTOLOCALDESK_BUILD_CORE_ONLY AND NOT SCTOLOCALDESK_BUILD_QT_APP)
    message(FATAL_ERROR "至少启用 SCTOLOCALDESK_BUILD_CORE_ONLY 或 SCTOLOCALDESK_BUILD_QT_APP 之一")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/project_config.h.in
    ${CMAKE_BINARY_DIR}/generated/project_config.h
)

# ------------------------------------------------------------
# 1) 核心静态库 sc_localdesk_core —— 唯一红线：不得链接 Qt
# ------------------------------------------------------------
set(SCLD_CORE_SOURCES
    src/core/protocol/tlv.cpp
    src/core/auth/unattended_authenticator.cpp
    src/service/alias_service.cpp
    src/service/app_config.cpp
    src/service/id_service.cpp
    src/service/remote_session_service.cpp
    src/service/screen_stream_service.cpp
    src/service/file_service.cpp
    src/service/trust_store.cpp
)

# 加密后端（PRD 会话加密）：Windows 走 OS 内建 CNG，其它走 OpenSSL（头探测），
# 缺失自动降级显式失败桩。编入 core 使单测可直接驱动 ICrypto
set(SCLD_CRYPTO_SOURCES
    src/implement/crypto/crypto_factory.cpp
    src/implement/crypto/openssl_crypto.cpp
)
if(WIN32)
    list(APPEND SCLD_CRYPTO_SOURCES src/implement/crypto/bcrypt_crypto.cpp)
endif()

add_library(sc_localdesk_core STATIC ${SCLD_CORE_SOURCES})
target_sources(sc_localdesk_core PRIVATE ${SCLD_CRYPTO_SOURCES})

if(WIN32)
    target_link_libraries(sc_localdesk_core PUBLIC bcrypt)
endif()

target_include_directories(sc_localdesk_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
)

target_compile_features(sc_localdesk_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(sc_localdesk_core PRIVATE
        /W4 /permissive- /Zc:__cplusplus /WX /utf-8)
    target_compile_definitions(sc_localdesk_core PUBLIC
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(sc_localdesk_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 日志后端：vendored spdlog（header-only 平铺树）优先；缺失时按旧选项
# find_package 兜底；都没有则回落 FileLogger（SCLD_HAS_SPDLOG 不定义）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/spdlog.h")
    target_include_directories(sc_localdesk_core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    target_compile_definitions(sc_localdesk_core PUBLIC SCLD_HAS_SPDLOG=1)
elseif(SCTOLOCALDESK_WITH_SPDLOG)
    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(sc_localdesk_core PUBLIC spdlog::spdlog)
    target_compile_definitions(sc_localdesk_core PUBLIC SCLD_HAS_SPDLOG=1)
endif()

# ---------------------------------------------------------------
# third_party 头库注册中心（全部 header-only，存在即启用，缺失自动降级）
#   nlohmann/json   3.11.3  jsonx.hpp   协议信令/结构化配置（V1.0）
#   xxHash          0.8.2   hashx.hpp  分片校验/指纹（FILE_CHUNK checksum）
#   magic_enum      0.9.6   enumx.hpp  枚举反射（__has_include 双保险守卫）
#   asio standalone 1.30.2  （预留）   TCP 直连/打洞二号线（未落位）
# 约定：业务层只 include foundation/*x.hpp 封装头，禁止直接引库名
# ---------------------------------------------------------------
set(SCLD_TP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if(EXISTS "${SCLD_TP_DIR}/nlohmann/json.hpp")
    target_include_directories(sc_localdesk_core PUBLIC "${SCLD_TP_DIR}/nlohmann")
endif()
if(EXISTS "${SCLD_TP_DIR}/xxhash/xxhash.h")
    target_include_directories(sc_localdesk_core PUBLIC "${SCLD_TP_DIR}/xxhash")
endif()
if(EXISTS "${SCLD_TP_DIR}/magic_enum/include/magic_enum/magic_enum_all.hpp")
    target_include_directories(sc_localdesk_core PUBLIC "${SCLD_TP_DIR}/magic_enum/include")
endif()
if(EXISTS "${SCLD_TP_DIR}/asio/include/asio.hpp")
    target_include_directories(sc_localdesk_core PUBLIC "${SCLD_TP_DIR}/asio/include")
    target_compile_definitions(sc_localdesk_core PUBLIC ASIO_STANDALONE)
endif()
# FFmpeg LGPL shared（动态链接合规）：vendored 于 third_party/ffmpeg，
# 存在即启用 MJPEG 编码（sws 色彩转换）；缺失自动降级 RawEncoder。
# Linux 侧（P6：Ubuntu 22.04 基准，Debian 系）走 pkg-config 探测
# 系统源 libavcodec-dev（FFmpeg 4.4，本编码器仅用稳定 API，可编译）
if(EXISTS "${SCLD_TP_DIR}/ffmpeg/include/libavcodec/avcodec.h")
    target_include_directories(sc_localdesk_core PUBLIC "${SCLD_TP_DIR}/ffmpeg/include")
    target_link_directories(sc_localdesk_core PUBLIC "${SCLD_TP_DIR}/ffmpeg/lib")
    target_link_libraries(sc_localdesk_core PUBLIC avcodec avutil swscale)
    target_compile_definitions(sc_localdesk_core PUBLIC SCLD_HAS_FFMPEG=1)
    target_sources(sc_localdesk_core PRIVATE
        src/implement/encoder/ffmpeg_encoder.cpp
        src/implement/decoder/ffmpeg_decoder.cpp)
    # FFmpeg 头内联窄化转换触发 C4244（/WX 下致命）：相关 TU 单独降 /W3
    if(MSVC)
        set_source_files_properties(src/implement/encoder/ffmpeg_encoder.cpp
            src/implement/decoder/ffmpeg_decoder.cpp
            PROPERTIES COMPILE_OPTIONS "/W3")
    endif()
    # 运行时 dll 自动跟产：构建后拷 third_party/ffmpeg/bin/*.dll 到每个
    # 链接 core 的 exe 输出目录（avcodec 隐性依赖 swresample——手工拷
    # exe 出去跑必踩，统一由构建期保证；部署清单 = exe + bin/*.dll）
    file(GLOB SCLD_FFMPEG_DLLS "${SCLD_TP_DIR}/ffmpeg/bin/*.dll")
    if(SCLD_FFMPEG_DLLS)
        add_custom_command(TARGET sc_localdesk_core POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SCLD_FFMPEG_DLLS} $<TARGET_FILE_DIR:sc_localdesk_core>)
    endif()
endif()

# Qt 依赖红线静态断言：任何人往 core 里塞 Qt 头，链接/编译期直接失败
if(TARGET sc_localdesk_core)
    add_custom_command(TARGET sc_localdesk_core POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_qt_in_core.cmake
        COMMENT "检查 core/foundation/service 目录无 Qt 头依赖")
endif()

# ------------------------------------------------------------
# 2) headless 冒烟入口（core-only 模式产物）
# ------------------------------------------------------------
if(SCTOLOCALDESK_BUILD_CORE_ONLY)
    add_executable(scToLocalDeskHeadless src/main_headless.cpp)
    target_link_libraries(scToLocalDeskHeadless PRIVATE sc_localdesk_core)
endif()

# ------------------------------------------------------------
# 3) QML 桌面应用 scToLocalDesk.exe
#    Qt 基线 6.2（远程 Linux Qt 6.2.4 可编译）；MultiEffect 玻璃
#    模糊为 6.5+ 增强特性，6.2 自动退化为半透底（Doc13 §13.2）
# ------------------------------------------------------------
if(SCTOLOCALDESK_BUILD_QT_APP)
    # WebSocket 为可选组件：未安装 QtWebSockets 时自动降级本地回环桩
    # （UI 预览模式），补装模块后无需改代码自动切回真实现
    find_package(Qt6 6.2 QUIET
        COMPONENTS Core Gui Quick QuickControls2 Network Widgets
        OPTIONAL_COMPONENTS WebSockets)
    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)

        # Qt 6.5+ 才有 QtQuick.Effects/MultiEffect（iOS16 毛玻璃真模糊）
        if(Qt6_VERSION VERSION_GREATER_EQUAL "6.5")
            set(SCLD_HAS_MULTIEFFECT 1)
        else()
            set(SCLD_HAS_MULTIEFFECT 0)
        endif()

        set(SCLD_QML_SOURCES
            src/main.cpp
            src/ui/qml.qrc
            src/platform/qt_adapter/qt_event_bridge.cpp
            src/platform/qt_adapter/device_list_model.cpp
            src/platform/qt_adapter/qt_udp_discovery.cpp
            src/platform/qt_adapter/video_pipeline.cpp
            src/platform/qt_adapter/video_surface.cpp
            src/platform/qt_adapter/remote_input_item.cpp
            src/platform/qt_adapter/qt_tray.cpp
            src/platform/qt_adapter/watermark_painter.cpp
            src/platform/qt_adapter/single_instance_guard.cpp
        )
        set(SCLD_QML_LIBS
            sc_localdesk_core
            Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2
            Qt6::Network Qt6::Widgets)
        if(TARGET Qt6::WebSockets)   # FIX: 曾误拼 WebSocket，恒走本地回环桩（监听必败假象）
            list(APPEND SCLD_QML_SOURCES src/implement/transport/ws_transport.cpp)
            list(APPEND SCLD_QML_LIBS Qt6::WebSockets)
            set(SCLD_HAS_WEBSOCKET 1)
        else()
            set(SCLD_HAS_WEBSOCKET 0)
            message(WARNING "未安装 QtWebSockets 模块：使用本地回环桩（仅 UI 预览），"
                "请用 Qt Maintenance Tool 补装 QtWebSockets")
        endif()

        add_executable(scToLocalDesk WIN32 ${SCLD_QML_SOURCES})
        if(WIN32)
            target_sources(scToLocalDesk PRIVATE
                src/platform/win32/single_instance.cpp
                src/platform/win32/auto_start.cpp
                src/platform/win32/input_injector.cpp
                src/platform/win32/system_info.cpp
                src/platform/win32/crash_reporter.cpp
            )
        else()
            # Linux（Doc18 P6）：system_info 走 POSIX 实现；注入器/自启动为占位
            target_sources(scToLocalDesk PRIVATE
                src/platform/linux/system_info_linux.cpp
                src/platform/linux/input_injector_linux.cpp
                src/platform/linux/auto_start_linux.cpp
            )
        endif()
        target_compile_definitions(scToLocalDesk PRIVATE
            SCLD_QT_APP=1 SCLD_HAS_MULTIEFFECT=${SCLD_HAS_MULTIEFFECT}
            SCLD_HAS_WEBSOCKET=${SCLD_HAS_WEBSOCKET})
        if(MSVC)
            target_compile_options(scToLocalDesk PRIVATE /utf-8)
        endif()
        # system_info：GetAdaptersAddresses/GetUserNameW 所需系统库
        if(WIN32)
            target_link_libraries(scToLocalDesk PRIVATE ${SCLD_QML_LIBS} iphlpapi advapi32 dbghelp)
        else()
            # Linux：OpenSSL 链接随 sc_localdesk_core PUBLIC 传递
            target_link_libraries(scToLocalDesk PRIVATE ${SCLD_QML_LIBS})
        endif()
    else()
        message(WARNING "未找到 Qt 6.2+，跳过 QT_APP 目标（请检查 CMAKE_PREFIX_PATH 指向 Qt 安装目录）")
    endif()
endif()

# ------------------------------------------------------------
# 3.5) QWidget 对照应用 scToLocalDeskWidgets.exe（Doc13 §13.2）
#      C++ + QSS 实现同套视觉规范，供与 QML 版效果比对
# ------------------------------------------------------------
option(SCTOLOCALDESK_BUILD_WIDGETS_APP "编译 QWidget 对照应用（需 Qt 6.2+ Widgets）" OFF)
if(SCTOLOCALDESK_BUILD_WIDGETS_APP)
    find_package(Qt6 6.2 QUIET
        COMPONENTS Core Gui Widgets Network
        OPTIONAL_COMPONENTS WebSocket)
    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)

        set(SCLD_WGT_SOURCES
            src/ui/qwidget/widgets_main.cpp
            src/ui/qwidget/glass_theme.cpp
            src/ui/qwidget/glass_panel.cpp
            src/ui/qwidget/main_window.cpp
            src/ui/qwidget/pages/pages.h           # AUTOMOC：pages.cpp 拆分后需显式列入以生成 moc
            src/ui/qwidget/pages/toggle_switch.cpp
            src/ui/qwidget/pages/device_list_page.cpp
            src/ui/qwidget/pages/connect_code_page.cpp
            src/ui/qwidget/pages/remote_view_page.cpp
            src/ui/qwidget/pages/file_transfer_page.cpp
            src/ui/qwidget/pages/settings_page.cpp
            src/ui/qwidget/popups/connect_confirm_dialog.cpp
            src/platform/qt_adapter/qt_event_bridge.cpp
            src/platform/qt_adapter/qt_udp_discovery.cpp
            src/platform/qt_adapter/video_pipeline.cpp
            src/platform/qt_adapter/qt_tray.cpp
            src/platform/qt_adapter/watermark_painter.cpp
            src/platform/qt_adapter/single_instance_guard.cpp
        )
        set(SCLD_WGT_LIBS
            sc_localdesk_core
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
        if(TARGET Qt6::WebSockets)   # FIX: 曾误拼 WebSocket，恒走本地回环桩（同 QML 版）
            list(APPEND SCLD_WGT_SOURCES src/implement/transport/ws_transport.cpp)
            list(APPEND SCLD_WGT_LIBS Qt6::WebSockets)
        endif()

        add_executable(scToLocalDeskWidgets WIN32 ${SCLD_WGT_SOURCES})
        if(WIN32)
            target_sources(scToLocalDeskWidgets PRIVATE
                src/platform/win32/single_instance.cpp
                src/platform/win32/auto_start.cpp
                src/platform/win32/input_injector.cpp
                src/platform/win32/system_info.cpp
                src/platform/win32/crash_reporter.cpp
                src/platform/qt_adapter/qt_udp_discovery.cpp
            )
        endif()
        target_compile_definitions(scToLocalDeskWidgets PRIVATE
            SCLD_QT_APP=1 SCLD_QT_WIDGETS_UI=1)
        if(MSVC)
            target_compile_options(scToLocalDeskWidgets PRIVATE /utf-8)
        endif()
        target_link_libraries(scToLocalDeskWidgets PRIVATE
            ${SCLD_WGT_LIBS} iphlpapi advapi32 dbghelp)
    else()
        message(WARNING "未找到 Qt 6.2+ Widgets，跳过 WIDGETS_APP 目标")
    endif()
endif()

# ------------------------------------------------------------
# 4) 单元测试
# ------------------------------------------------------------
if(SCTOLOCALDESK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------
# 5) i18n（Doc18 §7.3）：zh-CN 为源语言，en.ts 骨架已入 translations/；
#    翻译后补时先重扫描再填译文。手动手令：
#    lupdate src/ui/qml src/ui/qwidget -extensions qml,cpp,h -ts translations/sc_localdesk_en.ts
# ------------------------------------------------------------
find_program(SCLD_LUPDATE NAMES lupdate HINTS "${QT_DIR}/../../../bin" "${CMAKE_PREFIX_PATH}/bin")
if(SCLD_LUPDATE)
    add_custom_target(scld_lupdate
        COMMAND "${SCLD_LUPDATE}" src/ui/qml src/ui/qwidget
                -extensions qml,cpp,h
                -ts translations/sc_localdesk_en.ts
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "i18n: rescanning qsTr/tr into sc_localdesk_en.ts")
endif()
