// ============================================================
// scToLocalDesk | src/main.cpp
// 桌面应用入口：单实例锁 → DI 组装 → QML 挂载（仅 SCTOLOCALDESK_BUILD_QT_APP 编入）
// ============================================================
#include <chrono>
#include <memory>

#ifdef SCLD_QT_APP

    #include <QApplication>
    #include <QFileInfo>
    #include <QQmlApplicationEngine>
    #include <QQmlContext>
    #include <QQuickItem>
    #include <QQuickStyle>
    #include <QQuickWindow>
    #include <QStandardPaths>

    #include "core/auth/manual_confirm_authenticator.hpp"
    #include "core/auth/unattended_authenticator.hpp"
    #include "core/interface/icrypto.hpp"
    #include "foundation/config_parser.hpp"
    #include "foundation/enumx.hpp"
    #include "foundation/event_bus.hpp"
    #include "foundation/hashx.hpp"
    #include "foundation/jsonx.hpp"
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
#if SCLD_HAS_FFMPEG
    #include "implement/decoder/ffmpeg_decoder.hpp"
    #include "implement/encoder/ffmpeg_encoder.hpp"
#endif
#if SCLD_HAS_WEBSOCKET
    #include "implement/transport/ws_transport.hpp"
#else
    // Qt 缺 QtWebSockets：本地回环桩（UI 预览），类名一致调用方零改动
    #include "implement/transport/ws_transport_stub.hpp"
#endif
    #include "platform/qt_adapter/qt_event_bridge.hpp"
    #include "platform/qt_adapter/device_list_model.hpp"
    #include "platform/qt_adapter/qt_tray.hpp"
    #include "platform/qt_adapter/qt_udp_discovery.hpp"
    #include "platform/qt_adapter/single_instance_guard.hpp"
    #include "platform/qt_adapter/video_pipeline.hpp"
    #include "platform/qt_adapter/video_surface.hpp"
    #include "platform/win32/system_info.hpp"
    #include "service/alias_service.hpp"
    #include "service/app_config.hpp"
    #include "service/file_service.hpp"
    #include "service/id_service.hpp"
    #include "service/input_service.hpp"
    #include "service/remote_session_service.hpp"
    #include "service/screen_stream_service.hpp"
    #include "service/trust_store.hpp"

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

    // third_party 头库自检：封装层链路通畅性（校验值/解析正确性）
    {
        const auto h = foundation::xxh3_64("scToLocalDesk", 0);
        const auto j = foundation::json_parse(foundation::json_dump(
            foundation::json{{"app", "scToLocalDesk"}, {"ver", 1}}));
        SCLD_LOG(logger, LogLevel::Info, "main",
                 "third_party ready: xxh3=" + std::to_string(h)
                     + " json_ok=" + (j.has_value() && j->at("ver") == 1 ? "1" : "0")
                     + " magic_enum=" + std::string(foundation::enum_name(LogLevel::Info)));
    }
    // FFmpeg 编码自检：真实抓一帧桌面走 MJPEG 全程（压缩比/耗时落日志，
    // 性能基线用；SCLD_HAS_FFMPEG 缺失时编译期整体剔除）
#if SCLD_HAS_FFMPEG
    {
        GdiCapturer cap_probe;
        if (cap_probe.init()) {
            VideoFrame f_probe;
            if (cap_probe.grab(f_probe)) {
                FfmpegEncoder enc_probe;
                const auto t0 = std::chrono::steady_clock::now();
                std::vector<uint8_t> jpg;
                const bool ok = enc_probe.open(EncoderConfig{"mjpeg", cfg.fps,
                                                             cfg.jpeg_quality})
                             && enc_probe.encode(f_probe, jpg);
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0)
                                      .count();
                if (ok && !jpg.empty()) {
                    SCLD_LOG(logger, LogLevel::Info, "encoder",
                             "ffmpeg 自检 " + std::to_string(f_probe.width) + "x"
                                 + std::to_string(f_probe.height) + " raw="
                                 + std::to_string(f_probe.size) + "B jpg="
                                 + std::to_string(jpg.size()) + "B 压缩比 "
                                 + std::to_string(f_probe.size / jpg.size())
                                 + "x 耗时 " + std::to_string(ms) + "ms");
                }
                else {
                    SCLD_LOG(logger, LogLevel::Warn, "encoder", "ffmpeg 自检失败，推流期将回退");
                }
            }
            cap_probe.shutdown();
        }
    }
#endif

    auto bus = std::make_shared<EventBus>();

    // 机器身份：9 位连接码 + 连接名派生（Doc18 §3.1；P1.4 心跳/设备列表接线点）
    const platform::SystemInfo sys = platform::query_system_info();
    const IdService id_service(sys.hostname, sys.username, sys.mac,
                               QApplication::applicationDirPath().toStdString());
    if (sys.mac.empty()) {
        SCLD_LOG(logger, LogLevel::Warn, "id",
                 "未发现在线非虚拟网卡，指纹缺 MAC 项（连接码弱化）");
    }
    SCLD_LOG(logger, LogLevel::Info, "id",
             "self_code=" + id_service.self_code()
                 + " self_name=" + id_service.self_name());

    // 信任缓存：exe 旁 trust.json（Doc18 §3.2；构造即加载，变更即原子落盘）
    TrustStore trust_store(QApplication::applicationDirPath().toStdWString()
                           + L"\\trust.json",
                           logger.get());

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

    // 被控端推流管线（编码器按 session.encoder 选择：ffmpeg | raw，
    // FFmpeg 未编入时自动回落 raw 直通；MJPG/DXGI 为 V1.0 任务，Doc10 §1）
#if SCLD_HAS_FFMPEG
    auto stream = std::make_shared<ScreenStreamService>(bus, logger);
    if (cfg.encoder == "raw") {
        stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<RawEncoder>());
    }
    else {
        stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<FfmpegEncoder>());
    }
#else
    auto stream = std::make_shared<ScreenStreamService>(bus, logger);
    stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<RawEncoder>());
#endif
    sessions->set_stream_service(stream);

    // 被控端键鼠注入（SendInput，普通用户权限可用）
    auto input = std::make_shared<InputService>();
    input->set_injector(make_input_injector());
    sessions->set_input_service(input);

    // 被控监听（network.ws_port，Doc17 §3.2）；失败经桥提示 UI（Doc18 风险清单 P1 顺带）
    const bool listening = sessions->start_listening(static_cast<uint16_t>(cfg.ws_port));

    // UDP 心跳（Qt 适配器，Doc07 §8；组播失败自动切静态映射由适配器上报）
    platform::UdpDiscoveryAdapter::Options opt;
    opt.multicast_addr = cfg.multicast_addr;
    opt.self_alias = cfg.alias;
    opt.ws_port = static_cast<uint16_t>(cfg.ws_port);
    opt.heartbeat_ms = cfg.heartbeat_interval_s * 1000;
    opt.self_code = id_service.code_number();  // 心跳可选 TLV（Doc18 §3.4）
    platform::UdpDiscoveryAdapter discovery(opt, aliases.get(), logger.get());

    // QML 桥（EventBus → Qt 信号，Doc13 §4）
    platform::QtEventBridge bridge;
    bridge.init(bus.get(), aliases.get(), sessions.get(), files.get(), logger.get());
    bridge.set_trust_store(&trust_store);
    bridge.set_config_store(&ini, &cfg);
    bridge.set_discovery(&discovery);
    bridge.set_self_code(id_service.code_number());
    bridge.set_listening(listening);
    qmlRegisterSingletonInstance("ScLocaldesk", 1, 0, "ScLocaldesk", &bridge);
    qmlRegisterType<platform::VideoSurface>("ScLocaldesk", 1, 0, "VideoSurface");

    // 设备列表模型（Doc18 §3.4：QAbstractListModel 增量刷新，防整表抖动）
    platform::DeviceListModel device_model(bus, aliases.get(), &trust_store,
                                           logger.get());

    // 控制端解码管线（Doc18 §4.2）：常驻启动（无帧零开销），
    // 页面经 VideoSurface.pipeline 消费 frameReady
    platform::VideoPipeline video_pipeline(bus, logger, &app);
    video_pipeline.start();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("VideoPipeline"),
                                             &video_pipeline);
    engine.rootContext()->setContextProperty(QStringLiteral("DeviceListModel"),
                                             &device_model);
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
    const int rc = app.exec();
    // 规避 Qt 6.5.3 窗口析构期焦点链 UAF（clearFocusInScope 触碰已释放
    // focus scope，0xC0000005 稳定复现；启动期 forceActiveFocus 的深层
    // 控件触发）：析构前对 activeFocusItem setFocus(false) 彻底退出焦点
    // 链（清 scope 内 focus 标志，仅失焦不够），再销毁根窗口
    const auto roots = engine.rootObjects();
    for (QObject* root : roots) {
        if (auto* win = qobject_cast<QQuickWindow*>(root)) {
            if (auto* fi = win->activeFocusItem()) {
                fi->setFocus(false);
            }
        }
        delete root;
    }
    return rc;
}

#else
int main() {
    return 0;  // 非 Qt 构建使用 main_headless 入口
}
#endif
