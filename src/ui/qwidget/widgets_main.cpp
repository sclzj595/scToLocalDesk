// ============================================================
// scToLocalDesk | src/ui/qwidget/widgets_main.cpp
// QWidget 对照版入口（Doc13 §13.2）：与 QML 版同套视觉规范
// P1.4：发现栈（EventBus+AliasService+UdpDiscovery）+ IdService 本机卡
// ============================================================
#ifdef SCLD_QT_APP
    #ifdef SCLD_QT_WIDGETS_UI

        #include <QApplication>
        #include <QCoreApplication>
        #include <QFileInfo>
        #include <QTimer>

        #include "foundation/config_parser.hpp"
        #include "foundation/event_bus.hpp"
        #include "foundation/logger/file_logger.hpp"
        #include "core/auth/manual_confirm_authenticator.hpp"
        #include "core/auth/unattended_authenticator.hpp"
        #include "core/events.hpp"
        #include "implement/capture/gdi_capture.hpp"
        #include "implement/crypto/crypto_factory.hpp"
        #include "implement/encoder/raw_encoder.hpp"
#if SCLD_HAS_FFMPEG
        #include "implement/encoder/ffmpeg_encoder.hpp"
#endif
        #include "implement/transport/ws_transport.hpp"
        #include "platform/qt_adapter/qt_udp_discovery.hpp"
        #include "platform/qt_adapter/single_instance_guard.hpp"
        #include "platform/qt_adapter/video_pipeline.hpp"
        #include "platform/win32/system_info.hpp"
        #include "service/alias_service.hpp"
        #include "service/app_config.hpp"
        #include "service/id_service.hpp"
        #include "service/input_service.hpp"
        #include "service/remote_session_service.hpp"
        #include "service/screen_stream_service.hpp"
        #include "service/trust_store.hpp"
        #include "ui/qwidget/glass_theme.h"
        #include "ui/qwidget/main_window.h"
        #include "ui/qwidget/pages/pages.h"

        #include <QMessageBox>
        #include <QMetaObject>

namespace {
sc::localdesk::foundation::LogLevel parse_log_level(const std::string& s) {
    using sc::localdesk::foundation::LogLevel;
    if (s == "debug") return LogLevel::Debug;
    if (s == "warn") return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    if (s == "trace") return LogLevel::Trace;
    return LogLevel::Info;  // 缺省/非法回退（与 QML 版 main 同口径）
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 单实例锁（exe 名作 key：与 QML 版互不排斥，对照构建可并存）
    sc::localdesk::platform::SingleInstanceGuard guard;
    if (!guard.acquire(QFileInfo(QApplication::applicationFilePath()).fileName())) {
        return 0;   // 已有实例：唤起请求已投递
    }

    // 配置加载（Doc17：exe 旁 config.ini，缺失则默认值落盘；Doc18 §3.3 设置页生命周期）
    sc::localdesk::foundation::IniConfig ini(
        QCoreApplication::applicationDirPath().toStdWString() + L"\\config.ini");
    sc::localdesk::AppConfig cfg;
    if (!ini.load()) {
        cfg.save(ini);
        (void)ini.save();
    }
    else {
        cfg.load(ini);
    }

    // 日志：exe 旁 logs/（兜底 FileLogger，5MB×5；与 QML 版口径一致）
    const std::filesystem::path log_file =
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String())
        / "logs" / "scToLocalDeskWidgets.log";
    auto logger = std::make_shared<sc::localdesk::foundation::FileLogger>(
        log_file, 5 * 1024 * 1024, 5, true);
    logger->set_level(parse_log_level(cfg.log_level));

    // 机器身份（P1.1）：连接码派生 + 连接名
    const sc::localdesk::platform::SystemInfo sys =
        sc::localdesk::platform::query_system_info();
    const sc::localdesk::IdService id_service(sys.hostname, sys.username, sys.mac,
                                              QCoreApplication::applicationDirPath()
                                                  .toStdString());
    if (sys.mac.empty()) {
        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "id",
                 "未发现在线非虚拟网卡，指纹缺 MAC 项（连接码弱化）");
    }
    SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "id",
             "self_code=" + id_service.self_code()
                 + " self_name=" + id_service.self_name());

    // 信任缓存：exe 旁 trust.json（Doc18 §3.2，确认弹窗勾选记忆落盘）
    sc::localdesk::TrustStore trust_store(
        QCoreApplication::applicationDirPath().toStdWString() + L"\\trust.json",
        logger.get());

    // 发现栈（Doc18 §3.4）：组播心跳 → AliasService 在线表 → 设备页快照对账
    auto bus = std::make_shared<sc::localdesk::foundation::EventBus>();
    auto aliases = std::make_shared<sc::localdesk::AliasService>(bus, logger);
    sc::localdesk::platform::UdpDiscoveryAdapter::Options opt;
    opt.multicast_addr = cfg.multicast_addr;
    opt.self_alias = cfg.alias;
    opt.ws_port = static_cast<uint16_t>(cfg.ws_port);
    opt.heartbeat_ms = cfg.heartbeat_interval_s * 1000;
    opt.self_code = id_service.code_number();
    opt.log_tag = "discovery.widgets";  // 与 QML 版共用组播组，日志区分实例
    sc::localdesk::platform::UdpDiscoveryAdapter discovery(opt, aliases.get(),
                                                           logger.get());
    if (!discovery.start()) {
        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "discovery",
                 "组播发现启动失败（设备列表将为空，不影响本机功能）");
    }

    // 会话栈（Doc18 §4.3，与 QML 版同构）：WS 监听 + 鉴权三路 + 推流/键鼠
    using namespace sc::localdesk;
    auto crypto = make_default_crypto();
    auto transport = std::make_shared<WsTransport>(QStringLiteral("scToLocalDesk"));
    UnattendedSecret secret;
    secret.salt_hex = cfg.unattended_salt;
    secret.key_hex = cfg.unattended_key;
    secret.verify_hash_hex = cfg.unattended_verify_hash;
    auto unattended_auth = std::make_shared<UnattendedPasswordAuthenticator>(
        crypto, secret, cfg.unattended_enabled);
    auto manual_auth = std::make_shared<ManualConfirmAuthenticator>();
    auto sessions = std::make_shared<RemoteSessionService>(
        transport, manual_auth, unattended_auth, crypto, aliases, bus, logger);
    sessions->set_stream_params(cfg.fps, cfg.jpeg_quality);
    auto stream = std::make_shared<ScreenStreamService>(bus, logger);
#if SCLD_HAS_FFMPEG
    stream->set_source(std::make_shared<GdiCapturer>(),
                       cfg.encoder == "raw" ? std::shared_ptr<IEncoder>(std::make_shared<RawEncoder>())
                                            : std::make_shared<FfmpegEncoder>());
#else
    stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<RawEncoder>());
#endif
    auto input = std::make_shared<InputService>();
    input->set_injector(make_input_injector());
    sessions->set_stream_service(stream);
    sessions->set_input_service(input);
    const bool listening = sessions->start_listening(static_cast<uint16_t>(cfg.ws_port));
    if (!listening) {
        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "session",
                 "被控监听失败 port=" + std::to_string(cfg.ws_port));
    }

    // 全局 QSS 一次性挂载（主题切换时重建，Doc13 §7）
    app.setStyleSheet(sc::localdesk::ui::GlassTheme::instance().buildQss());

    sc::localdesk::ui::MainWindow win;

    // 入站连接申请 → 主线程确认（暂用 QMessageBox，玻璃化随 P4 UI 专项；
    // SCLD_SMOKE_APPROVE=1 冒烟钩子：自动放行，双实例 E2E 用）
    const bool smoke_approve = qEnvironmentVariable("SCLD_SMOKE_APPROVE") == "1";
    bus->subscribe<Evt_ConnectRequest>(
        [&win, sessions, smoke_approve, &logger](const Evt_ConnectRequest& e) {
            QMetaObject::invokeMethod(
                &win,
                [&win, sessions, smoke_approve, &logger, e] {
                    if (e.auto_approved) {
                        return;  // 无人值守已放行，无需确认
                    }
                    if (smoke_approve) {
                        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info,
                                 "smoke", "auto approve");
                        sessions->approve();
                        return;
                    }
                    const auto r = QMessageBox::question(
                        &win, QStringLiteral("连接请求"),
                        QStringLiteral("对方请求远程协助本机，是否同意？"));
                    if (r == QMessageBox::Yes) {
                        sessions->approve();
                    }
                    else {
                        sessions->reject();
                    }
                },
                Qt::QueuedConnection);
        });
    win.setTrustStore(&trust_store);
    win.set_config_store(&ini, &cfg);
    win.show();

    // 设备页接真数据：本机卡真值 + 1s 快照对账（增量刷新，Doc18 §3.4）
    if (auto* dev_page = win.devicePage()) {
        dev_page->bind_services(aliases.get(), &trust_store);
        dev_page->set_self(QString::fromStdString(cfg.alias),
                           QString::fromStdString(id_service.self_code()));
        auto* tick = new QTimer(&win);
        QObject::connect(tick, &QTimer::timeout, dev_page,
                         &sc::localdesk::ui::DeviceListPage::apply_snapshot);
        tick->start(1000);
        dev_page->apply_snapshot();
    }

    // 控制端解码管线（Doc18 §4.2）：常驻启动（无帧零开销），帧达即渲染
    sc::localdesk::platform::VideoPipeline video(bus, logger, &app);
    video.start();
    if (auto* rv = win.remoteViewPage()) {
        QObject::connect(&video,
                         &sc::localdesk::platform::VideoPipeline::frameReady,
                         rv, &sc::localdesk::ui::RemoteViewPage::on_frame);
    }

    // 单实例唤起：二次启动把首实例主窗带到前台
    guard.set_raise_callback([&win] {
        win.showNormal();
        win.raise();
        win.activateWindow();
    });

    // TODO(V1.0): 复用 main.cpp 的会话/文件/推流服务全量接线（演示期仅发现闭环）
    return app.exec();
}

    #endif
#endif
