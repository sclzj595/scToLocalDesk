// ============================================================
// scToLocalDesk | src/ui/qwidget/widgets_main.cpp
// QWidget 对照版入口（Doc13 §13.2）：与 QML 版同套视觉规范
// P1.4：发现栈（EventBus+AliasService+UdpDiscovery）+ IdService 本机卡
// ============================================================
#ifdef SCLD_QT_APP
    #ifdef SCLD_QT_WIDGETS_UI

        #include <QApplication>
        #include <QClipboard>
        #include <QCoreApplication>
        #include <QFile>
        #include <QFileInfo>
        #include <QMessageBox>
        #include <QPointer>
        #include <QRandomGenerator>
        #include <QTimer>

        #include "foundation/config_parser.hpp"
        #include "foundation/event_bus.hpp"
        #include "foundation/logger/file_logger.hpp"
        #include "foundation/thread_pool.hpp"
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
        #include "platform/win32/crash_reporter.hpp"
        #include "platform/win32/system_info.hpp"
        #include "service/alias_service.hpp"
        #include "service/app_config.hpp"
        #include "service/file_service.hpp"
        #include "service/id_service.hpp"
        #include "service/input_service.hpp"
        #include "service/remote_session_service.hpp"
        #include "service/screen_stream_service.hpp"
        #include "service/trust_store.hpp"

        #include "platform/qt_adapter/qt_tray.hpp"
        #include "platform/qt_adapter/watermark_painter.hpp"
        #include "ui/qwidget/glass_theme.h"
        #include "ui/qwidget/main_window.h"
        #include "ui/qwidget/pages/pages.h"

        #include <QMessageBox>
        #include <QMetaObject>

namespace {
sc::localdesk::foundation::LogLevel parse_log_level(const std::string& s) {
    using sc::localdesk::foundation::LogLevel;
    if (s == "debug") return LogLevel::Debug;
    if (s == "warn") return sc::localdesk::foundation::LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    if (s == "trace") return LogLevel::Trace;
    return sc::localdesk::foundation::LogLevel::Info;  // 缺省/非法回退（与 QML 版 main 同口径）
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 崩溃捕获（Doc18 §6.2）：exe 旁 dumps/，越早装越好
    sc::localdesk::platform::CrashReporter::install(
        QCoreApplication::applicationDirPath().toStdWString() + L"\\dumps");
    if (qEnvironmentVariable("SCLD_CRASH_TEST") == "1") {
        sc::localdesk::platform::CrashReporter::crash_for_test();
    }

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
    aliases->set_self_alias(cfg.alias);  // 本机自滤（Doc07 §5），漏掉则回环心跳入列表
    // 静态映射（Doc07 §7）：组播不可达场景的跨机直连表
    if (!cfg.static_map.empty()) {
        const auto statics = sc::localdesk::parse_static_map(cfg.static_map);
        if (!statics.empty()) {
            aliases->set_static_map(statics);
            SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "discovery",
                     "静态映射已加载 n=" + std::to_string(statics.size()));
        }
    }
    sc::localdesk::platform::UdpDiscoveryAdapter::Options opt;
    opt.multicast_addr = cfg.multicast_addr;
    opt.self_alias = cfg.alias;
    opt.ws_port = static_cast<uint16_t>(cfg.ws_port);
    opt.heartbeat_ms = cfg.heartbeat_interval_s * 1000;
    opt.self_code = id_service.code_number();
    opt.log_tag = "discovery.widgets";  // 与 QML 版共用组播组，日志区分实例
    sc::localdesk::platform::UdpDiscoveryAdapter discovery(opt, aliases.get(),
                                                           logger.get());
    // 组播开关（Doc07 §7）：关闭即静态映射模式（仅查表，不发心跳不收组播）
    if (cfg.multicast_enabled) {
        if (!discovery.start()) {
            SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "discovery",
                     "组播发现启动失败（设备列表将为空，不影响本机功能）");
        }
    }
    else {
        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "discovery",
                 "组播发现已关闭（静态映射模式，仅查表解析）");
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
    // PBKDF2 异步收尾回投主线程（服务层 Qt-free，投递能力装配层注入；
    // 校验后的状态迁移/事件发布必须在主线程——bus 订阅方含 Qt 模型）
    sessions->set_main_poster([](std::function<void()> fn) {
        QMetaObject::invokeMethod(
            qApp, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
    });
    sessions->set_stream_params(cfg.fps, cfg.jpeg_quality);
    sessions->set_trust_store(&trust_store);  // 已信任设备免确认直连（Doc18 §3.2）
    // 自连拦截（PRD 连接黑名单）：本机网卡 IP 集合 + 本机监听端口双条件
    const std::vector<std::string> local_ips = sys.ips;
    const uint16_t local_port = static_cast<uint16_t>(cfg.ws_port);
    sessions->set_self_ip_checker(
        [local_ips, local_port](const std::string& ip, uint16_t port) {
            if (port != local_port) {
                return false;  // 同机双实例（端口不同）放行，保 E2E 回环
            }
            for (const auto& own : local_ips) {
                if (own == ip) {
                    return true;
                }
            }
            return false;
        });
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
    // 文件传输栈（Doc18 §5）：分片收发 + 前置确认 + 线程池
    auto pool = std::make_shared<sc::localdesk::foundation::ThreadPool>(4);
    auto files = std::make_shared<FileService>(transport, bus, logger);
    files->set_thread_pool(pool);
    files->set_chunk_size(static_cast<uint32_t>(cfg.chunk_size_kb));
    files->set_confirm_enabled(cfg.confirm_before_transfer);
    files->set_safe_path(cfg.file_safe_path);
    files->set_overwrite_policy(cfg.overwrite);
    files->set_save_dir(cfg.save_path.empty()
                            ? (QCoreApplication::applicationDirPath().toStdString()
                               + "/received")
                            : cfg.save_path);
    sessions->set_file_service(files);
    const bool listening = sessions->start_listening(static_cast<uint16_t>(cfg.ws_port));
    if (!listening) {
        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "session",
                 "被控监听失败 port=" + std::to_string(cfg.ws_port));
    }

    // 全局 QSS 一次性挂载（主题切换时重建，Doc13 §7）
    app.setStyleSheet(sc::localdesk::ui::GlassTheme::instance().buildQss());

    sc::localdesk::ui::MainWindow win;

    // 端口占用 UI 提示（Doc18 风险清单 P1）：非模态弹窗带具体端口与改法，
    // 不阻塞装配；主线程投递（start_listening 在装配线程）
    if (!listening) {
        const int bad_port = cfg.ws_port;
        QMetaObject::invokeMethod(
            &win,
            [&win, bad_port] {
                QMessageBox box(QMessageBox::Warning,
                                QObject::tr("被控监听失败"),
                                QObject::tr("会话端口 %1 被占用或无法监听，"
                                               "对方将无法连接本机。\n请在 "
                                               "设置→网络 修改 WS 端口后重启。")
                                    .arg(bad_port),
                                QMessageBox::Ok, &win);
                box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
                box.show();  // 非模态：不阻塞主窗口交互
            },
            Qt::QueuedConnection);
    }

    // 自连拦截提示（Evt_ConnectDenied → 主线程弹窗，不发网络包）
    bus->subscribe<Evt_ConnectDenied>([&win, &logger](const Evt_ConnectDenied& e) {
        QMetaObject::invokeMethod(
            &win,
            [&win, &logger, e] {
                SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn,
                         "smoke", "connect denied " + e.reason);
                QMessageBox::warning(&win, QObject::tr("无法发起协助"),
                                     QString::fromStdString(e.reason));
            },
            Qt::QueuedConnection);
    });

    // 入站连接申请 → 主线程确认（暂用 QMessageBox，玻璃化随 P4 UI 专项；
    // SCLD_SMOKE_APPROVE=1 冒烟钩子：自动放行，双实例 E2E 用）
    // PRD 确认三件套：非模态置顶（模态 exec 无法被 30s 超时回调关闭）+
    // 请求方 IP 文案 + 超时信号远程关弹窗
    const bool smoke_approve = qEnvironmentVariable("SCLD_SMOKE_APPROVE") == "1";
    QPointer<QMessageBox> active_confirm;  // 当前确认弹窗（超时自动拒时关闭）
    bus->subscribe<Evt_ConnectRequest>(
        [&win, sessions, smoke_approve, &trust_store, aliases,
         &logger, &active_confirm](const Evt_ConnectRequest& e) {
            QMetaObject::invokeMethod(
                &win,
                [&win, sessions, smoke_approve, &trust_store, aliases, &logger,
                 e, &active_confirm] {
                    if (e.auto_approved) {
                        return;  // 无人值守已放行，无需确认
                    }
                    if (smoke_approve) {
                        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info,
                                 "smoke", "auto approve");
                        // 模拟"同意+记住"：在线表反查对端 code 写入信任缓存
                        //（E2E 第二轮免确认直连的前置，Doc18 §3.2）
                        for (const auto& dev : aliases->online_devices()) {
                            if (dev.alias == e.device_alias && dev.code != 0) {
                                trust_store.trust(std::to_string(dev.code),
                                                  e.device_alias);
                                SCLD_LOG(logger,
                                         sc::localdesk::foundation::LogLevel::Info,
                                         "smoke",
                                         "auto trust code="
                                             + std::to_string(dev.code));
                                break;
                            }
                        }
                        sessions->approve();
                        return;
                    }
                    auto* box = new QMessageBox(&win);
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->setWindowTitle(QObject::tr("连接请求"));
                    box->setText(e.peer_ip.empty()
                                     ? QObject::tr("对方请求远程协助本机，是否同意？")
                                     : QObject::tr("对方（%1）请求远程协助本机，是否同意？")
                                           .arg(QString::fromStdString(e.peer_ip)));
                    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    box->setWindowFlag(Qt::WindowStaysOnTopHint, true);  // PRD 确认三件套：置顶
                    active_confirm = box;
                    QObject::connect(box, &QMessageBox::finished, box,
                                     [sessions, box](int r) {
                                         if (box == nullptr) {
                                             return;
                                         }
                                         if (r == QMessageBox::Yes) {
                                             sessions->approve();
                                         }
                                         else {
                                             // 含超时自动拒后的收尾 close()：
                                             // state 已被看门狗迁移，reject() 幂等
                                             sessions->reject();
                                         }
                                     });
                    box->show();
                    QApplication::alert(box);  // 任务栏闪烁（被控端可能无人盯屏幕）
                },
                Qt::QueuedConnection);
        });
    bus->subscribe<Evt_ConnectRequestTimeout>(
        [&win, &active_confirm](const Evt_ConnectRequestTimeout&) {
            QMetaObject::invokeMethod(
                &win,
                [&win, &active_confirm] {
                    if (active_confirm != nullptr) {
                        active_confirm->close();  // finished(非Yes) → reject() 兜底
                    }
                },
                Qt::QueuedConnection);
        });
    // 别名冲突（PRD P5）：组播发现同别名异 IP 时引导改名
    bus->subscribe<Evt_AliasConflict>(
        [&win, &logger](const Evt_AliasConflict& e) {
            QMetaObject::invokeMethod(
                &win,
                [&win, &logger, e] {
                    SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn,
                             "alias", "别名冲突 alias=" + e.device_alias
                                          + " ip=" + e.conflict_ip);
                    QMessageBox::warning(
                        &win, QObject::tr("别名冲突"),
                        QObject::tr("检测到其他设备使用别名「%1」（%2），"
                                       "请在设置中修改本机连接名")
                            .arg(QString::fromStdString(e.device_alias),
                                 QString::fromStdString(e.conflict_ip)));
                },
                Qt::QueuedConnection);
        });
    win.setTrustStore(&trust_store);
    win.set_config_store(&ini, &cfg);
    win.settingsPage()->set_alias_service(aliases.get());  // 静态映射热生效
    win.setSessionService(sessions.get());  // PRD P7：确认后真正发起拨号
    // 设备列表刷新按钮（Doc02 §5.4）：主动广播一轮心跳，在线表立即更新
    win.set_refresh_devices([&discovery] { discovery.send_heartbeat(); });
    win.show();

    // 被控端知情权（PRD P4）：Widgets 端补托盘——会话状态项 + 一键断开 +
    // 推流水印（"访客别名 · 时间"，60s 刷新；渲染主线程、混合采集线程）
    sc::localdesk::platform::TrayController tray;
    tray.show();
    tray.set_unattended_badge(cfg.unattended_enabled);  // 启动装载（Doc16 §8）
    // 无人值守开关变更 → 托盘徽标热更新（Doc16 §7）
    QObject::connect(
        win.settingsPage(),
        &sc::localdesk::ui::SettingsPage::unattEnabledChanged, &tray,
        [&tray](bool on) { tray.set_unattended_badge(on); });
    // 同名覆盖策略变更 → FileService 热生效（Doc11 §4.2）
    QObject::connect(
        win.settingsPage(),
        &sc::localdesk::ui::SettingsPage::overwritePolicyChanged, &win,
        [files](const QString& policy) {
            files->set_overwrite_policy(policy.toStdString());
        });
    // 托盘三键接线（Doc02 §5.4）：显示主窗 / 停止服务 toggle / 退出程序
    // 显示主窗必须先清 minimized 位再 show：show() 对最小化窗口不还原
    //（经典 Qt 缺陷），保留 maximized/fullscreen 其余状态位
    QObject::connect(
        &tray, &sc::localdesk::platform::TrayController::showMainRequested, &tray,
        [&win] {
            if ((win.windowState() & Qt::WindowMinimized) != 0) {
                win.setWindowState(win.windowState() & ~Qt::WindowMinimized);
            }
            win.show();
            win.raise();
            win.activateWindow();
        });
    bool service_stopped = false;
    QObject::connect(
        &tray, &sc::localdesk::platform::TrayController::stopServiceRequested, &tray,
        [&] {
            if (!service_stopped) {
                sessions->close_session(
                    sc::localdesk::protocol::CloseReason::Normal);  // Idle 直返
                transport->close();
                discovery.stop();
                service_stopped = true;
                tray.set_service_stopped(true);
                SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "tray",
                         "服务已停止（监听+心跳）");
            }
            else if (transport->start_server(
                         static_cast<uint16_t>(cfg.ws_port))) {
                service_stopped = false;
                if (cfg.multicast_enabled) {
                    (void)discovery.start();  // 恢复心跳（停止时一并关停）
                }
                tray.set_service_stopped(false);
                SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "tray",
                         "服务已恢复");
            }
            else {
                SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Error, "tray",
                         "服务恢复失败（端口被占用？）");
            }
        });
    QObject::connect(&tray, &sc::localdesk::platform::TrayController::quitRequested,
                     &tray, [] { QApplication::quit(); });
    // 无人值守接入气泡（Doc16 §2.5 全程审计可见；审计 Warn 由服务层落盘）
    bus->subscribe<sc::localdesk::Evt_UnattendedLogin>(
        [&tray](const sc::localdesk::Evt_UnattendedLogin& e) {
            QMetaObject::invokeMethod(
                &tray,
                [&tray, e] {
                    tray.notify(QObject::tr("无人值守接入"),
                                QObject::tr("【%1】已通过密码接入")
                                    .arg(QString::fromStdString(e.device_alias)));
                },
                Qt::QueuedConnection);
        });
    {
        auto current_peer = std::make_shared<QString>();
        auto wm_timer = new QTimer(&tray);
        wm_timer->setInterval(60 * 1000);
        QObject::connect(wm_timer, &QTimer::timeout,
                         [&stream, current_peer] {
                             if (current_peer->isEmpty()) {
                                 return;
                             }
                             const QImage wm = sc::localdesk::platform::
                                 render_session_watermark(*current_peer);
                             if (!wm.isNull()) {
                                 auto buf = std::make_shared<std::vector<uint8_t>>(
                                     wm.bits(), wm.bits()
                                                    + static_cast<size_t>(
                                                        wm.sizeInBytes()));
                                 stream->set_watermark(std::move(buf), wm.width(),
                                                       wm.height());
                             }
                         });
        QObject::connect(
            &tray, &sc::localdesk::platform::TrayController::disconnectRequested,
            [&sessions, &logger] {
                SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "tray",
                         "托盘一键断开会话");
                sessions->close_session(sc::localdesk::protocol::CloseReason::Normal);
            });
        bus->subscribe<Evt_SessionStart>(
            [&tray, &stream, wm_timer, current_peer](const Evt_SessionStart& e) {
                QMetaObject::invokeMethod(
                    &tray,
                    [&tray, &stream, wm_timer, current_peer, e] {
                        *current_peer = QString::fromStdString(e.device_alias);
                        tray.set_session_active(true, *current_peer);
                        const QImage wm = sc::localdesk::platform::
                            render_session_watermark(*current_peer);
                        if (!wm.isNull()) {
                            // 深拷贝自持缓冲（QImage 析构后混合线程仍引用）
                            auto buf = std::make_shared<std::vector<uint8_t>>(
                                wm.bits(),
                                wm.bits() + static_cast<size_t>(wm.sizeInBytes()));
                            stream->set_watermark(std::move(buf), wm.width(),
                                                  wm.height());
                        }
                        wm_timer->start();
                    },
                    Qt::QueuedConnection);
            });
        bus->subscribe<Evt_SessionClosed>(
            [&tray, &stream, wm_timer, current_peer](const Evt_SessionClosed&) {
                QMetaObject::invokeMethod(
                    &tray,
                    [&tray, &stream, wm_timer, current_peer] {
                        current_peer->clear();
                        wm_timer->stop();
                        tray.set_session_active(false, {});
                        stream->set_watermark(nullptr, 0, 0);
                    },
                    Qt::QueuedConnection);
            });
    }

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
        rv->set_session(sessions.get());  // 键鼠透传出口（Doc18 §4.4）
        // 会话关闭复位：清帧回空态、隐藏工具条（无会话不可点断开）
        bus->subscribe<Evt_SessionClosed>([rv](const Evt_SessionClosed&) {
            QMetaObject::invokeMethod(rv, &sc::localdesk::ui::RemoteViewPage::on_session_closed,
                                      Qt::QueuedConnection);
        });
        // 会话建立：角色/对端快照 + 工具条恢复（无帧会话断开可用，实战踩坑）
        bus->subscribe<Evt_SessionStart>([rv](const Evt_SessionStart& e) {
            QMetaObject::invokeMethod(
                rv,
                [rv, alias = QString::fromStdString(e.device_alias)] {
                    rv->on_session_started(alias);
                },
                Qt::QueuedConnection);
        });
    }

    // 文件传输接线（Doc18 §5）：Evt_File* → 行卡 + 发送入口 + 接收确认
    if (auto* fp = win.fileTransferPage()) {
        QObject::connect(fp, &sc::localdesk::ui::FileTransferPage::filesPicked, fp,
                         [files, fp, &logger](const QStringList& paths) {
                             for (const QString& p : paths) {
                                 if (!files->send_file(p.toStdString())) {
                                     SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn, "file",
                                              "send_file 失败 " + p.toStdString());
                                 }
                             }
                         });
        bus->subscribe<Evt_FileProgress>([fp](const Evt_FileProgress& e) {
            QMetaObject::invokeMethod(fp, [fp, e] {
                fp->on_file_progress(QString::fromStdString(e.file_id),
                                     QString::fromStdString(e.name),
                                     static_cast<qint64>(e.transferred),
                                     static_cast<qint64>(e.total),
                                     e.dir == FileDir::Recv ? 1 : 0);
            }, Qt::QueuedConnection);
        });
        bus->subscribe<Evt_FileDone>([fp](const Evt_FileDone& e) {
            QMetaObject::invokeMethod(fp, [fp, e] {
                fp->on_file_done(QString::fromStdString(e.file_id),
                                 QString::fromStdString(e.name),
                                 e.dir == FileDir::Recv ? 1 : 0);
            }, Qt::QueuedConnection);
        });
        bus->subscribe<Evt_FileFailed>([fp](const Evt_FileFailed& e) {
            QMetaObject::invokeMethod(fp, [fp, e] {
                fp->on_file_failed(QString::fromStdString(e.file_id),
                                   QString::fromStdString(e.name),
                                   e.dir == FileDir::Recv ? 1 : 0);
            }, Qt::QueuedConnection);
        });
        // 接收前置确认（Doc11 §4.3）：QMessageBox 问询（玻璃化随 P4 专项）；
        // SCLD_SMOKE_FILE=1 冒烟钩子自动同意（E2E 回环收发）
        const bool smoke_file = qEnvironmentVariable("SCLD_SMOKE_FILE") == "1";
        bus->subscribe<Evt_FileMetaReceived>([&win, files, &logger,
                                              smoke_file](const Evt_FileMetaReceived& e) {
            QMetaObject::invokeMethod(
                &win,
                [files, &logger, smoke_file, e] {
                    if (smoke_file) {
                        (void)files->accept_pending(e.file_id);
                        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "smoke",
                                 "auto accept file_id=" + e.file_id);
                        return;
                    }
                    const QString alias = QString::fromStdString(e.device_alias);
                    const QString name = QString::fromStdString(e.name);
                    auto ret = QMessageBox::question(
                        nullptr, QObject::tr("文件接收请求"),
                        QObject::tr("%1 向本机发送文件：\n%2（%3 字节）\n是否接收？")
                            .arg(alias.isEmpty() ? QObject::tr("对方") : alias, name)
                            .arg(e.size),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                    if (ret == QMessageBox::Yes) {
                        (void)files->accept_pending(e.file_id);
                        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "file",
                                 "接收确认 file_id=" + e.file_id);
                    } else {
                        files->reject_pending(e.file_id);
                        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "file",
                                 "接收拒绝 file_id=" + e.file_id);
                    }
                },
                Qt::QueuedConnection);
        });
    }

    // 冒烟钩子（Doc18 §5 E2E）：SCLD_SMOKE_FILE=1 时 Running 后 3s 自动生成
    // 随机文件发送（大小 SCLD_SMOKE_FILE_MB，默认 8MB；接收端 confirm 钩子自动同意）
    if (qEnvironmentVariable("SCLD_SMOKE_FILE") == "1") {
        bus->subscribe<Evt_SessionStart>([files, &logger](const Evt_SessionStart&) {
            QTimer::singleShot(3000, [files, &logger] {
                const QString path =
                    QCoreApplication::applicationDirPath() + "/smoke_file.bin";
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) {
                    qint64 mb =
                        qEnvironmentVariable("SCLD_SMOKE_FILE_MB").toLongLong();
                    if (mb <= 0) {
                        mb = 8;
                    }
                    const qint64 kSize = mb * 1024 * 1024;
                    QByteArray blk(1024 * 1024, 0);
                    for (int i = 0; i < blk.size(); ++i) {
                        blk[i] =
                            static_cast<char>(QRandomGenerator::global()->generate());
                    }
                    for (qint64 off = 0; off < kSize; off += blk.size()) {
                        f.write(blk.constData(),
                                static_cast<qint64>(
                                    qMin<qint64>(blk.size(), kSize - off)));
                    }
                    f.close();
                    SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info,
                             "smoke",
                             "smoke file gen " + path.toStdString() + " size="
                                 + std::to_string(f.size()));
                } else {
                    SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn,
                             "smoke",
                             "smoke file gen open 失败 " + path.toStdString()
                                 + " err=" + f.errorString().toStdString());
                }
                if (files->send_file(path.toStdString())) {
                    SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Info, "smoke",
                             "smoke file send begin " + path.toStdString());
                } else {
                    SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Error,
                             "smoke", "smoke send_file 失败");
                }
            });
        });
    }

    // 剪贴板文本同步（PRD P9）：装配同 main.cpp（内容三重去重防回环）
    {
        QClipboard* clip = QApplication::clipboard();
        auto echo_mtx = std::make_shared<std::mutex>();
        auto last_remote = std::make_shared<QString>();
        auto last_sent = std::make_shared<QString>();
        bus->subscribe<Evt_ClipboardReceived>(
            [&logger, clip, echo_mtx, last_remote](const Evt_ClipboardReceived& e) {
                {
                    std::lock_guard<std::mutex> lk(*echo_mtx);
                    *last_remote = QString::fromUtf8(QByteArray::fromStdString(e.text));
                }
                QMetaObject::invokeMethod(
                    qApp,
                    [&logger, clip, e] {
                        const QString text =
                            QString::fromUtf8(QByteArray::fromStdString(e.text));
                        if (text.isEmpty()) {
                            return;
                        }
                        clip->setText(text);
                        SCLD_LOG(logger, sc::localdesk::foundation::LogLevel::Warn,
                                 "clipboard",
                                 "远端文本已写入本机剪贴板 len="
                                     + std::to_string(e.text.size())
                                     + " head=" + e.text.substr(0, 24));
                    });
            });
        QObject::connect(clip, &QClipboard::dataChanged, qApp,
                         [clip, &sessions, &logger, echo_mtx, last_remote, last_sent] {
                             const QString text = clip->text();
                             if (text.isEmpty()) {
                                 return;
                             }
                             {
                                 std::lock_guard<std::mutex> lk(*echo_mtx);
                                 if (text == *last_remote || text == *last_sent) {
                                     return;  // 回声：远端写入 / 本端已发，不再同步
                                 }
                                 *last_sent = text;
                             }
                             if (sessions->send_clipboard(text.toStdString())) {
                                 SCLD_LOG(logger,
                                          sc::localdesk::foundation::LogLevel::Info,
                                          "clipboard",
                                          "本机剪贴板已同步 len="
                                              + std::to_string(text.toStdString().size()));
                             }
                         });
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
