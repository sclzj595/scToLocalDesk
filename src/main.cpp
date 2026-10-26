// ============================================================
// scToLocalDesk | src/main.cpp
// 桌面应用入口：单实例锁 → DI 组装 → QML 挂载（仅 SCTOLOCALDESK_BUILD_QT_APP 编入）
// ============================================================
#include <chrono>
#include <memory>

#ifdef SCLD_QT_APP

    #include <QApplication>
#include <QClipboard>
    #include <QFile>
    #include <QFileInfo>
    #include <QQmlApplicationEngine>
    #include <QQmlContext>
    #include <QQuickItem>
    #include <QQuickStyle>
    #include <QQuickWindow>
    #include <QRandomGenerator>
    #include <QStandardPaths>
    #include <QTimer>

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
#ifdef _WIN32
    #include "implement/capture/gdi_capture.hpp"
#endif
    #include "implement/crypto/crypto_factory.hpp"
    #include "implement/encoder/raw_encoder.hpp"
#if SCLD_HAS_FFMPEG && defined(_WIN32)
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
        #include "platform/qt_adapter/watermark_painter.hpp"
    #include "platform/qt_adapter/qt_udp_discovery.hpp"
    #include "platform/qt_adapter/single_instance_guard.hpp"
    #include "platform/qt_adapter/video_pipeline.hpp"
    #include "platform/qt_adapter/video_surface.hpp"
    #include "platform/qt_adapter/remote_input_item.hpp"
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
    // ---------------- 崩溃捕获（Doc18 §6.2）：exe 旁 dumps/，越早装越好 ----------------
    platform::CrashReporter::install(
        std::filesystem::path(QApplication::applicationDirPath().toStdU16String())
        / "dumps");
    if (qEnvironmentVariable("SCLD_CRASH_TEST") == "1") {
        platform::CrashReporter::crash_for_test();
    }
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
        fprintf(stderr, "[main] another instance running, wake request sent\n");
        fflush(stderr);
        return 0;
    }

    // ---------------- 配置加载（Doc17：exe 旁 config.ini，缺失则默认值落盘） ----------------
    IniConfig ini(QApplication::applicationDirPath().toStdString() + "/config.ini");
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
        foundation::json j;
        const bool j_ok = foundation::json_parse(
            foundation::json_dump(
                foundation::json{{"app", "scToLocalDesk"}, {"ver", 1}}),
            j);
        SCLD_LOG(logger, LogLevel::Info, "main",
                 "third_party ready: xxh3=" + std::to_string(h)
                     + " json_ok=" + (j_ok && j.at("ver") == 1 ? "1" : "0")
                     + " magic_enum=" + std::string(foundation::enum_name(LogLevel::Info)));
    }
    // FFmpeg 编码自检：真实抓一帧桌面走 MJPEG 全程（压缩比/耗时落日志，
    // 性能基线用；SCLD_HAS_FFMPEG 缺失时编译期整体剔除；Linux 采集器 P6 落位）
#if SCLD_HAS_FFMPEG && defined(_WIN32)
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
    TrustStore trust_store(QApplication::applicationDirPath().toStdString()
                               + "/trust.json",
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
    // 静态映射（Doc07 §7）：alias=ip:port;... —— 组播不可达（NAT/跨段）时直连
    if (!cfg.static_map.empty()) {
        const std::vector<DeviceInfo> statics = parse_static_map(cfg.static_map);
        if (!statics.empty()) {
            aliases->set_static_map(statics);
            SCLD_LOG(logger, LogLevel::Info, "discovery",
                     "静态映射已加载 n=" + std::to_string(statics.size()));
        }
    }
    auto transport = std::make_shared<WsTransport>(QStringLiteral("scToLocalDesk"));
    auto sessions = std::make_shared<RemoteSessionService>(
        transport, manualAuth, unattendedAuth, crypto, aliases, bus, logger);
    sessions->set_stream_params(cfg.fps, cfg.jpeg_quality);
    sessions->set_trust_store(&trust_store);  // 已信任设备免确认直连（Doc18 §3.2）
    // 自连拦截（PRD 连接黑名单）：本机网卡 IP 集合 + 本机监听端口双条件
    {
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
    }

    auto files = std::make_shared<FileService>(transport, bus, logger);
    files->set_thread_pool(pool);
    files->set_chunk_size(static_cast<uint32_t>(cfg.chunk_size_kb));
    files->set_confirm_enabled(cfg.confirm_before_transfer);
    files->set_safe_path(cfg.file_safe_path);
    const QString save_dir = cfg.save_path.empty()
        ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        : QString::fromStdString(cfg.save_path);
    files->set_save_dir(save_dir.toStdString());
    sessions->set_file_service(files);

    // 被控端推流管线（编码器按 session.encoder 选择：ffmpeg | raw，
    // FFmpeg 未编入时自动回落 raw 直通；采集器 Linux 侧 P6 落位，暂不装配）
#if defined(_WIN32)
    auto stream = std::make_shared<ScreenStreamService>(bus, logger);
#   if SCLD_HAS_FFMPEG
    if (cfg.encoder == "raw") {
        stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<RawEncoder>());
    }
    else {
        stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<FfmpegEncoder>());
    }
#   else
    stream->set_source(std::make_shared<GdiCapturer>(), std::make_shared<RawEncoder>());
#   endif
    sessions->set_stream_service(stream);
#endif

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
    // PBKDF2 异步收尾回投主线程（服务层 Qt-free，投递能力装配层注入；
    // 校验后的状态迁移/事件发布必须在主线程——bus 订阅方含 Qt 模型）
    sessions->set_main_poster([&app](std::function<void()> fn) {
        QMetaObject::invokeMethod(
            &app, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
    });
    qmlRegisterSingletonInstance("ScLocaldesk", 1, 0, "ScLocaldesk", &bridge);
    qmlRegisterType<platform::VideoSurface>("ScLocaldesk", 1, 0, "VideoSurface");
    qmlRegisterType<platform::RemoteInputItem>("ScLocaldesk", 1, 0, "RemoteInputItem");

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

    // 冒烟钩子（env 门控，默认关闭）：SCLD_SMOKE_CONNECT=<alias> 延时自动
    // 发起会话，供双实例 E2E（对端配 SCLD_SMOKE_APPROVE=1 自动放行）
    const QString smoke_alias = qEnvironmentVariable("SCLD_SMOKE_CONNECT");
    if (!smoke_alias.isEmpty()) {
        // SCLD_SMOKE_PWD 非空时走无人值守+密码链路（PBKDF2 异步 E2E，
        // 对端 config.ini [security] 预置凭据且 unattended_enabled=true）
        const QString smoke_pwd = qEnvironmentVariable("SCLD_SMOKE_PWD");
        QTimer::singleShot(3500, &bridge, [&, smoke_alias, smoke_pwd] {
            SCLD_LOG(logger, LogLevel::Info, "smoke",
                     "auto startControl alias=" + smoke_alias.toStdString()
                         + " unattended=" + (smoke_pwd.isEmpty() ? "0" : "1"));
            (void)bridge.startControl(smoke_alias, !smoke_pwd.isEmpty(),
                                      smoke_pwd);
        });
    }
    // 冒烟钩子 2（默认关）：SCLD_SMOKE_INPUT=1 时 Running 后 2s 发键鼠序列
    //（P2.4 E2E：Move→左键→HI 打字→滚轮，验证归一化+注入全类型）
    if (qEnvironmentVariableIntValue("SCLD_SMOKE_INPUT") != 0) {
        QObject::connect(&bridge, &platform::QtEventBridge::sessionStarted,
                         [&bridge, &logger] {
                             QTimer::singleShot(2000, &bridge, [&bridge, &logger] {
                                 SCLD_LOG(logger, LogLevel::Info, "smoke",
                                          "auto input sequence begin");
                                 bridge.sendMouse(0, 32768, 32768, 0);  // Move 中心
                                 bridge.sendMouse(1, 32768, 32768, 0);  // LeftDown
                                 bridge.sendMouse(2, 32768, 32768, 0);  // LeftUp
                                 bridge.sendKey(0x48, 0, true);   // H down
                                 bridge.sendKey(0x48, 0, false);  // H up
                                 bridge.sendKey(0x49, 0, true);   // I down
                                 bridge.sendKey(0x49, 0, false);  // I up
                                 bridge.sendMouse(7, 0, 0, 120);  // Wheel +1 格
                                 SCLD_LOG(logger, LogLevel::Info, "smoke",
                                          "auto input sequence end");
                             });
                         });
    }
    // 冒烟钩子 3（默认关）：SCLD_SMOKE_FILE=1 时 Running 后 3s 自动生成
    // 随机文件发送（大小 SCLD_SMOKE_FILE_MB，默认 8MB）+ 收到接收请求自动同意
    if (qEnvironmentVariable("SCLD_SMOKE_FILE") == "1") {
        QObject::connect(
            &bridge, &platform::QtEventBridge::sessionStarted, &bridge,
            [&bridge, &logger] {
                QTimer::singleShot(3000, &bridge, [&bridge, &logger] {
                    const QString path = QStandardPaths::writableLocation(
                                             QStandardPaths::TempLocation)
                                         + "/scld_smoke_file.bin";
                    QFile f(path);
                    if (f.open(QIODevice::WriteOnly)) {
                        qint64 mb = qEnvironmentVariable("SCLD_SMOKE_FILE_MB")
                                        .toLongLong();
                        if (mb <= 0) {
                            mb = 8;
                        }
                        const qint64 kSize = mb * 1024 * 1024;
                        QByteArray blk(1024 * 1024, 0);
                        for (int i = 0; i < blk.size(); ++i) {
                            blk[i] = static_cast<char>(
                                QRandomGenerator::global()->generate());
                        }
                        for (qint64 off = 0; off < kSize; off += blk.size()) {
                            f.write(blk.constData(),
                                    static_cast<qint64>(qMin<qint64>(
                                        blk.size(), kSize - off)));
                        }
                        f.close();
                        SCLD_LOG(logger, LogLevel::Info, "smoke",
                                 "smoke file gen " + path.toStdString() + " size="
                                     + std::to_string(f.size()));
                    } else {
                        SCLD_LOG(logger, LogLevel::Warn, "smoke",
                                 "smoke file gen open 失败 " + path.toStdString()
                                     + " err=" + f.errorString().toStdString());
                    }
                    if (bridge.sendFile(path)) {
                        SCLD_LOG(logger, LogLevel::Info, "smoke",
                                 "smoke file send begin " + path.toStdString());
                    } else {
                        SCLD_LOG(logger, LogLevel::Error, "smoke",
                                 "smoke sendFile 失败");
                    }
                });
            });
        QObject::connect(
            &bridge, &platform::QtEventBridge::fileMetaReceived, &bridge,
            [&bridge, &logger](const QString& fileId, const QString& name, qint64 size,
                               const QString& alias) {
                bridge.acceptFile(fileId);
                SCLD_LOG(logger, LogLevel::Info, "smoke",
                         "auto accept file_id=" + fileId.toStdString() + " name="
                             + name.toStdString() + " size=" + std::to_string(size)
                             + " alias=" + alias.toStdString());
            });
    }

    // 冒烟钩子 4（默认关）：SCLD_SMOKE_RECONNECT=<N> 断开-重连循环（PRD P8
    // 网络加固 E2E）。配合 SCLD_SMOKE_CONNECT：Running 后 2s 主动断开，
    // 1s 后重连，共 N 轮——验证快速往返无泄漏/无状态残留
    //（对端配 SCLD_SMOKE_APPROVE=1 自动放行）
    {
        const int smoke_reconnect =
            qEnvironmentVariable("SCLD_SMOKE_RECONNECT").toInt();
        if (smoke_reconnect > 0 && !smoke_alias.isEmpty()) {
            // 全部定时器在主线程触发，普通计数即可；shared_ptr 供 lambda 值捕获
            auto rounds = std::make_shared<int>(0);
            QObject::connect(
                &bridge, &platform::QtEventBridge::sessionStarted, &bridge,
                [&bridge, &logger, rounds, smoke_reconnect, smoke_alias] {
                    QTimer::singleShot(2000, &bridge, [&bridge, &logger, rounds,
                                                       smoke_reconnect] {
                        const int n = ++(*rounds);
                        SCLD_LOG(logger, LogLevel::Warn, "smoke",
                                 "reconnect round " + std::to_string(n) + "/"
                                     + std::to_string(smoke_reconnect)
                                     + " close begin");
                        bridge.disconnectSession();
                    });
                });
            QObject::connect(
                &bridge, &platform::QtEventBridge::sessionClosed, &bridge,
                [&bridge, &logger, rounds, smoke_reconnect, smoke_alias] {
                    if (*rounds >= smoke_reconnect) {
                        SCLD_LOG(logger, LogLevel::Warn, "smoke",
                                 "reconnect loop done rounds="
                                     + std::to_string(*rounds) + "/"
                                     + std::to_string(smoke_reconnect));
                        return;
                    }
                    QTimer::singleShot(1000, &bridge, [&bridge, smoke_alias] {
                        (void)bridge.startControl(smoke_alias, false, "");
                    });
                });
        }
    }

    // 剪贴板文本同步（PRD P9）：远端文本写本机剪贴板 + 本机复制变化上行。
    // Windows 剪贴板通知异步且系统全局（对端写入也触发本进程 dataChanged，
    // QSignalBlocker 无效），回环靠内容三重去重：远端刚写入/本端刚发送的
    // 文本不再同步——内容等值跳过对同步语义无损
    {
        QClipboard* clip = QApplication::clipboard();
        auto echo_mtx = std::make_shared<std::mutex>();
        auto last_remote = std::make_shared<QString>();
        auto last_sent = std::make_shared<QString>();
        bus->subscribe<Evt_ClipboardReceived>(
            [&logger, clip, echo_mtx, last_remote](const Evt_ClipboardReceived& e) {
                {
                    std::lock_guard<std::mutex> lk(*echo_mtx);
                    // 收帧即标记（subscribe 在 IO 线程）：赶在本端 dataChanged
                    // 回声触发前生效，竞态窗口缩至网络在途
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
                        SCLD_LOG(logger, LogLevel::Warn, "clipboard",
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
                                 SCLD_LOG(logger, LogLevel::Info, "clipboard",
                                          "本机剪贴板已同步 len="
                                              + std::to_string(text.toStdString().size()));
                             }
                         });
    }
    // 冒烟钩子 5（默认关）：SCLD_SMOKE_CLIP=1 时 Running 后 2s 写本机剪贴板，
    // dataChanged 自动同步对端（PRD P9 E2E）
    if (qEnvironmentVariable("SCLD_SMOKE_CLIP") == "1") {
        QObject::connect(
            &bridge, &platform::QtEventBridge::sessionStarted, &bridge,
            [&logger] {
                QTimer::singleShot(2000, qApp, [&logger] {
                    const QString text ="scld-clip-E2E-"+ QString::number(
                        QRandomGenerator::global()->bounded(100000));
                    QApplication::clipboard()->setText(text);
                    SCLD_LOG(logger, LogLevel::Warn, "smoke",
                             "smoke clip set: " + text.toStdString());
                });
            });
    }
    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "[main] QML 加载失败 rootObjects 为空\n");
        fflush(stderr);
        return -1;
    }

    // 单实例唤起：二次启动把首实例主窗带到前台（可能收拢在托盘）
    QQuickWindow* main_win =
        qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (main_win != nullptr) {
        guard.set_raise_callback([main_win] {
            main_win->show();
            main_win->raise();
            main_win->requestActivate();
        });
    }

    // 托盘（显示窗口 / 停止服务 / 退出，Doc02 §5.4）
    platform::TrayController tray;
    tray.show();
    QObject::connect(&tray, &platform::TrayController::showMainRequested, &tray,
                     [main_win] {
                         if (main_win != nullptr) {
                             main_win->show();
                             main_win->raise();
                             main_win->requestActivate();
                         }
                     });
    // 停止服务 toggle（Doc02 §5.4）：停监听+停心跳；会话中先礼貌断开
    //（close_session 内部 Idle 直返，无会话时安全）
    bool service_stopped = false;
    QObject::connect(&tray, &platform::TrayController::stopServiceRequested, &tray,
                     [&] {
                         if (!service_stopped) {
                             sessions->close_session(protocol::CloseReason::Normal);
                             transport->close();
                             discovery.stop();
                             service_stopped = true;
                             tray.set_service_stopped(true);
                             SCLD_LOG(logger, LogLevel::Warn, "tray",
                                      "服务已停止（监听+心跳）");
                         }
                         else if (transport->start_server(
                                      static_cast<uint16_t>(cfg.ws_port))) {
                             service_stopped = false;
                             tray.set_service_stopped(false);
                             SCLD_LOG(logger, LogLevel::Info, "tray", "服务已恢复");
                         }
                         else {
                             SCLD_LOG(logger, LogLevel::Error, "tray",
                                      "服务恢复失败（端口被占用？）");
                         }
                     });
    // 退出程序：exec 返回走焦点链清理 + 析构 flush（优雅退出，Doc02 §5.4）
    QObject::connect(&tray, &platform::TrayController::quitRequested, &app, [] {
        QApplication::quit();
    });

    // 被控端知情权（PRD P4）：会话中托盘状态项 + 一键断开 + 推流水印
    //（"访客别名 · 时间"，60s 刷新；水印渲染在主线程，混合在采集线程）
    {
        auto current_peer = std::make_shared<QString>();
        auto wm_timer = new QTimer(&tray);
        wm_timer->setInterval(60 * 1000);
        QObject::connect(wm_timer, &QTimer::timeout, [&tray, &stream, current_peer] {
            if (current_peer->isEmpty()) {
                return;
            }
            const QImage wm = platform::render_session_watermark(*current_peer);
            if (!wm.isNull()) {
                auto buf = std::make_shared<std::vector<uint8_t>>(
                    wm.bits(), wm.bits() + static_cast<size_t>(wm.sizeInBytes()));
                stream->set_watermark(std::move(buf), wm.width(), wm.height());
            }
        });
        QObject::connect(&tray, &platform::TrayController::disconnectRequested,
                         [&sessions, &logger] {
                             SCLD_LOG(logger, LogLevel::Warn, "tray", "托盘一键断开会话");
                             sessions->close_session(protocol::CloseReason::Normal);
                         });
        bus->subscribe<Evt_SessionStart>(
            [&tray, &stream, wm_timer, current_peer, &logger](const Evt_SessionStart& e) {
                QMetaObject::invokeMethod(
                    &tray,
                    [&tray, &stream, wm_timer, current_peer, e] {
                        *current_peer = QString::fromStdString(e.device_alias);
                        tray.set_session_active(true, *current_peer);
#if defined(_WIN32)
                        const QImage wm =
                            platform::render_session_watermark(*current_peer);
                        if (!wm.isNull()) {
                            // 深拷贝为自持缓冲（QImage 析构后混合线程仍引用）
                            auto buf = std::make_shared<std::vector<uint8_t>>(
                                wm.bits(),
                                wm.bits() + static_cast<size_t>(wm.sizeInBytes()));
                            stream->set_watermark(std::move(buf), wm.width(),
                                                  wm.height());
                        }
#endif
                        wm_timer->start();
                    },
                    Qt::QueuedConnection);
                (void)logger;
            });
        bus->subscribe<Evt_SessionClosed>(
            [&tray, &stream, wm_timer, current_peer](const Evt_SessionClosed&) {
                QMetaObject::invokeMethod(
                    &tray,
                    [&tray, &stream, wm_timer, current_peer] {
                        current_peer->clear();
                        wm_timer->stop();
                        tray.set_session_active(false, {});
#if defined(_WIN32)
                        stream->set_watermark(nullptr, 0, 0);
#endif
                    },
                    Qt::QueuedConnection);
            });
    }

    // 组播开关（Doc07 §7）：关闭即静态映射模式（仅查表，不发心跳不收组播）
    if (cfg.multicast_enabled) {
        (void)discovery.start();
    }
    else {
        SCLD_LOG(logger, LogLevel::Info, "discovery",
                 "组播发现已关闭（静态映射模式，仅查表解析）");
    }
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
