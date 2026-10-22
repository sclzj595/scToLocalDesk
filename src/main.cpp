// ============================================================
// scToLocalDesk | src/main.cpp
// 桌面应用入口：单实例锁 → DI 组装 → QML 挂载（仅 SCTOLOCALDESK_BUILD_QT_APP 编入）
// ============================================================
#include <memory>

#ifdef SCLD_QT_APP

    #include <QApplication>
    #include <QFileInfo>
    #include <QQmlApplicationEngine>
    #include <QQmlContext>
    #include <QQuickStyle>
    #include <QQuickWindow>
    #include <QStandardPaths>

    #include "core/auth/manual_confirm_authenticator.hpp"
    #include "core/auth/unattended_authenticator.hpp"
    #include "core/interface/icrypto.hpp"
    #include "foundation/config_parser.hpp"
    #include "foundation/event_bus.hpp"
#if SCLD_HAS_SPDLOG
    // 日志后端：vendored spdlog（rotating + flush_on(Warn)），兜底见 #else
    #include "foundation/logger/spdlog_logger.hpp"
#else
    #include "foundation/logger/file_logger.hpp"
#endif
    #include "foundation/thread_pool.hpp"
    #include "implement/capture/gdi_capture.hpp"
    #include "implement/crypto/crypto_factory.hpp"
    #include "implement/encoder/raw_encoder.hpp"
#if SCLD_HAS_WEBSOCKET
    #include "implement/transport/ws_transport.hpp"
#else
    // Qt 缺 QtWebSockets：本地回环桩（UI 预览），类名一致调用方零改动
    #include "implement/transport/ws_transport_stub.hpp"
#endif
    #include "platform/qt_adapter/qt_event_bridge.hpp"
    #include "platform/qt_adapter/qt_tray.hpp"
    #include "platform/qt_adapter/qt_udp_discovery.hpp"
    #include "platform/qt_adapter/single_instance_guard.hpp"
    #include "service/alias_service.hpp"
    #include "service/app_config.hpp"
    #include "service/file_service.hpp"
    #include "service/input_service.hpp"
    #include "service/remote_session_service.hpp"
    #include "service/screen_stream_service.hpp"

using namespace sc::localdesk;
#if SCLD_HAS_SPDLOG
using sc::localdesk::foundation::SpdlogLogger;
#else
using sc::localdesk::foundation::FileLogger;
#endif
using sc::localdesk::foundation::EventBus;
using sc::localdesk::foundation::IniConfig;
using sc::localdesk::foundation::LogLevel;
using sc::localdesk::foundation::ThreadPool;

namespace {

LogLevel parse_log_level(const std::string& s) {
    if (s == "trace") {
        return LogLevel::Trace;
    }
    if (s == "debug") {
        return LogLevel::Debug;
    }
    if (s == "warn") {
        return LogLevel::Warn;
    }
    if (s == "error") {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

}  // namespace

int main(int argc, char* argv[]) {
    // QApplication（非 QGui）：托盘 QMenu 需要 Widgets 事件栈
    QApplication app(argc, argv);
    // Qt 消息（QML 错误等）统一落 stderr，便于冒烟诊断
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&,
                              const QString& msg) {
        const char* lv = type == QtFatalMsg ? "FATAL"
            : type == QtCriticalMsg ? "CRIT"
            : type == QtWarningMsg ? "WARN"
            : type == QtDebugMsg ? "DBG" : "INFO";
        fprintf(stderr, "[qt:%s] %s\n", lv, msg.toLocal8Bit().constData());
        fflush(stderr);
    });
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // ---------------- 单实例锁（exe 名作 key：对照构建可并存，正式单 exe 即全局） ----------------
    platform::SingleInstanceGuard guard;
    if (!guard.acquire(QFileInfo(QApplication::applicationFilePath()).fileName())) {
        fprintf(stderr, "[main] 已有实例在运行，已投递唤起请求，本实例退出\n");
        fflush(stderr);
        return 0;
    }

    // ---------------- 配置加载（Doc17：exe 旁 config.ini，缺失则默认值落盘） ----------------
    IniConfig ini(QApplication::applicationDirPath().toStdWString() + L"\\config.ini");
    AppConfig cfg;
    if (!ini.load()) {
        cfg.save(ini);
        (void)ini.save();
    }
    else {
        cfg.load(ini);
    }

    // ---------------- DI 组装（Doc03 §5） ----------------
    // 日志：exe 旁 logs/（Doc17），5MB×5 滚动；spdlog 后端 flush_on(Warn)，
    // 兜底 FileLogger 每行 flush。二者均含 stderr 回显口径（spdlog 版经 wincolor 控制台，演示期文件为准）
    const std::filesystem::path log_file =
        std::filesystem::path(QApplication::applicationDirPath().toStdU16String())
        / "logs" / "scToLocalDesk.log";
#if SCLD_HAS_SPDLOG
    auto logger = std::make_shared<SpdlogLogger>(log_file.u8string());
#else
    auto logger = std::make_shared<FileLogger>(log_file, 5 * 1024 * 1024, 5, true);
#endif
    logger->set_level(parse_log_level(cfg.log_level));
    auto bus = std::make_shared<EventBus>();
    auto crypto = make_default_crypto();
    auto pool = std::make_shared<ThreadPool>(4);

    // 无人值守凭据：config.ini [security] 持久化的 K 等价凭据（Doc16 §6）
    UnattendedSecret secret;
    secret.salt_hex = cfg.unattended_salt;
    secret.key_hex = cfg.unattended_key;
    secret.verify_hash_hex = cfg.unattended_verify_hash;
    auto unattendedAuth = std::make_shared<UnattendedPasswordAuthenticator>(
        crypto, secret, cfg.unattended_enabled);
    auto manualAuth = std::make_shared<ManualConfirmAuthenticator>();

    auto aliases = std::make_shared<AliasService>(bus, logger);
    aliases->set_self_alias(cfg.alias);
    auto transport = std::make_shared<WsTransport>(QStringLiteral("scToLocalDesk"));
    auto sessions = std::make_shared<RemoteSessionService>(
        transport, manualAuth, unattendedAuth, crypto, aliases, bus, logger);
    sessions->set_stream_params(cfg.fps, cfg.jpeg_quality);

    auto files = std::make_shared<FileService>(transport, bus, logger);
    files->set_thread_pool(pool);
    files->set_chunk_size(static_cast<uint32_t>(cfg.chunk_size_kb));
    files->set_confirm_enabled(cfg.confirm_before_transfer);
    const QString save_dir = cfg.save_path.empty()
        ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        : QString::fromStdString(cfg.save_path);
    files->set_save_dir(save_dir.toStdString());
    sessions->set_file_service(files);

    // 被控端推流管线（骨架期 GDI 采集 + RAW 直通；MJPG/DXGI 为 V1.0 任务，Doc10 §1）
    auto stream = std::make_shared<ScreenStreamService>(bus, logger);
    stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<RawEncoder>());
    sessions->set_stream_service(stream);

    // 被控端键鼠注入（SendInput，普通用户权限可用）
    auto input = std::make_shared<InputService>();
    input->set_injector(make_input_injector());
    sessions->set_input_service(input);

    // 被控监听（network.ws_port，Doc17 §3.2）
    (void)sessions->start_listening(static_cast<uint16_t>(cfg.ws_port));

    // UDP 心跳（Qt 适配器，Doc07 §8；组播失败自动切静态映射由适配器上报）
    platform::UdpDiscoveryAdapter::Options opt;
    opt.multicast_addr = cfg.multicast_addr;
    opt.self_alias = cfg.alias;
    opt.ws_port = static_cast<uint16_t>(cfg.ws_port);
    opt.heartbeat_ms = cfg.heartbeat_interval_s * 1000;
    platform::UdpDiscoveryAdapter discovery(opt, aliases.get(), logger.get());

    // QML 桥（EventBus → Qt 信号，Doc13 §4）
    platform::QtEventBridge bridge;
    bridge.init(bus.get(), aliases.get(), sessions.get(), files.get(), logger.get());
    qmlRegisterSingletonInstance("ScLocaldesk", 1, 0, "ScLocaldesk", &bridge);

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/scToLocalDesk/qml/MainWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "[main] QML 加载失败 rootObjects 为空\n");
        fflush(stderr);
        return -1;
    }

    // 单实例唤起：二次启动把首实例主窗带到前台（可能收拢在托盘）
    if (auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        guard.set_raise_callback([win] {
            win->show();
            win->raise();
            win->requestActivate();
        });
    }

    // 托盘（显示窗口 / 停止服务 / 退出，Doc02 §5.4）
    platform::TrayController tray;
    tray.show();

    (void)discovery.start();
    return app.exec();
}

#else
int main() {
    return 0;  // 非 Qt 构建使用 main_headless 入口
}
#endif
