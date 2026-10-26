// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_event_bridge.cpp
// 桥实现：EventBus 订阅（Qt::QueuedConnection 跨线程投递到主线程）
// ============================================================
#include "platform/qt_adapter/qt_event_bridge.hpp"

#include <core/events.hpp>
#include <core/protocol/tlv.hpp>

#include <QApplication>
#include <QClipboard>
#include <QDate>
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_WIN
    #include <windows.h>
#endif

#include <foundation/event_bus.hpp>
#include <foundation/logger/ilogger.hpp>

#include <QApplication>
#include <QStandardPaths>

#include <foundation/config_parser.hpp>
#include <platform/win32/auto_start.hpp>
#include <platform/qt_adapter/qt_udp_discovery.hpp>
#include <service/alias_service.hpp>
#include <service/app_config.hpp>
#include <service/file_service.hpp>
#include <service/id_service.hpp>
#include <service/remote_session_service.hpp>
#include <service/trust_store.hpp>

namespace sc::localdesk::platform {

void QtEventBridge::init(foundation::EventBus* bus, AliasService* aliases,
                         RemoteSessionService* sessions, FileService* files,
                         foundation::ILogger* logger) {
    bus_ = bus;
    aliases_ = aliases;
    sessions_ = sessions;
    files_ = files;
    log_ = logger;

    if (aliases_ != nullptr) {
        self_alias_ = QString::fromStdString(aliases_->self_alias());
    }

    // 订阅：EventBus 回调在发布线程；本桥槽函数经 QueuedConnection 回主线程
    bus->subscribe<Evt_DeviceOnline>([this](const Evt_DeviceOnline& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit deviceOnline(QString::fromStdString(e.device_alias),
                              QString::fromStdString(e.ip));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_DeviceOffline>([this](const Evt_DeviceOffline& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit deviceOffline(QString::fromStdString(e.device_alias));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_AliasConflict>([this](const Evt_AliasConflict& e) {
        // PRD P5 别名治理：断头信号接通——UI 引导用户改名
        QMetaObject::invokeMethod(this, [this, e] {
            emit aliasConflict(QString::fromStdString(e.device_alias),
                               QString::fromStdString(e.conflict_ip));
        }, Qt::QueuedConnection);
    });
    // 控制端状态流转（PRD P7）：stage 直传 + busy 属性联动（防重复发起置灰）
    bus->subscribe<Evt_ConnectStateChanged>([this](const Evt_ConnectStateChanged& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit connectStateChanged(QString::fromStdString(e.stage),
                                     QString::fromStdString(e.alias));
            emit sessionBusyChanged();  // connecting/rejected 均changed
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_SessionStart>([this](const Evt_SessionStart& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit sessionBusyChanged();
            emit sessionStarted(QString::fromStdString(e.device_alias));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_SessionClosed>([this](const Evt_SessionClosed& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit sessionBusyChanged();
            static constexpr const char* kText[] = {
                "协助已结束", "对方拒绝了协助", "访问密码不正确",
                "请求等待超时", "对方正在协助中", "网络中断，协助已断开", "对方已断开协助"};
            const int idx = static_cast<int>(e.reason);
            emit sessionClosed(QString::fromUtf8(
                kText[idx >= 0 && idx < 7 ? idx : 0]));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_ConnectRequest>([this](const Evt_ConnectRequest& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            if (!e.auto_approved) {
                emit connectRequest(QString::fromStdString(e.device_alias),
                                    QString::fromStdString(e.peer_ip));
            }
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_ConnectRequestTimeout>([this](const Evt_ConnectRequestTimeout&) {
        QMetaObject::invokeMethod(this, [this] {
            emit connectRequestTimedOut();
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_ConnectDenied>([this](const Evt_ConnectDenied& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit connectDenied(QString::fromStdString(e.alias),
                               QString::fromStdString(e.reason));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileProgress>([this](const Evt_FileProgress& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileProgress(QString::fromStdString(e.file_id),
                              QString::fromStdString(e.name),
                              static_cast<qint64>(e.transferred),
                              static_cast<qint64>(e.total),
                              e.dir == FileDir::Recv ? 1 : 0);
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileDone>([this](const Evt_FileDone& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileDone(QString::fromStdString(e.file_id),
                          QString::fromStdString(e.name),
                          e.dir == FileDir::Recv ? 1 : 0);
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileFailed>([this](const Evt_FileFailed& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileFailed(QString::fromStdString(e.file_id),
                            QString::fromStdString(e.name),
                            e.dir == FileDir::Recv ? 1 : 0);
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileMetaReceived>([this](const Evt_FileMetaReceived& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileMetaReceived(QString::fromStdString(e.file_id),
                                  QString::fromStdString(e.name),
                                  static_cast<qint64>(e.size),
                                  QString::fromStdString(e.device_alias));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_UnattendedLogin>([this](const Evt_UnattendedLogin& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit unattendedLogin(QString::fromStdString(e.device_alias));
        }, Qt::QueuedConnection);
    });
}

QString QtEventBridge::selfAlias() const {
    return self_alias_;
}

QString QtEventBridge::selfCode() const {
    return self_code_ == 0
               ? QString()
               : QString::fromStdString(IdService::format_code(self_code_));
}

QString QtEventBridge::deviceAlias(const QString& code) const {
    if (aliases_ == nullptr) {
        return {};
    }
    bool ok = false;
    const uint32_t c = QString(code).remove(' ').toUInt(&ok);
    if (!ok || c == 0) {
        return {};
    }
    for (const auto& dev : aliases_->online_devices()) {
        if (dev.code == c) {
            return QString::fromStdString(dev.alias);
        }
    }
    return {};
}

QString QtEventBridge::clipboardText() const {
    const QClipboard* cb = QGuiApplication::clipboard();
    return cb != nullptr ? cb->text(QClipboard::Clipboard) : QString();
}

QString QtEventBridge::deviceIp(const QString& code) const {    if (aliases_ == nullptr) {
        return {};
    }
    bool ok = false;
    QString digits = code;
    const uint32_t c = digits.remove(' ').toUInt(&ok);
    if (!ok || c == 0) {
        return {};
    }
    for (const auto& dev : aliases_->online_devices()) {
        if (dev.code == c) {
            return QString::fromStdString(dev.ip);
        }
    }
    return {};
}

QString QtEventBridge::deviceCode(const QString& alias) const {
    if (aliases_ == nullptr) {
        return {};
    }
    for (const auto& dev : aliases_->online_devices()) {
        if (QString::fromStdString(dev.alias) == alias) {
            return QString::fromStdString(IdService::format_code(dev.code));
        }
    }
    return {};
}

bool QtEventBridge::unattendedEnabled() const {
    return unattended_;
}

void QtEventBridge::approve() {
    if (sessions_ != nullptr) {
        sessions_->approve();
    }
}

void QtEventBridge::reject() {
    if (sessions_ != nullptr) {
        sessions_->reject();
    }
}

void QtEventBridge::disconnectSession() {
    if (sessions_ != nullptr) {
        sessions_->close_session();  // 一键断开，任何模式可用（Doc16 §2.6）
    }
}

void QtEventBridge::cancelConnect() {
    if (sessions_ != nullptr) {
        sessions_->cancel_connect();  // PRD P7：拨号/等确认期取消
    }
}

QString QtEventBridge::lastPeerAlias() const {
    return sessions_ != nullptr ? QString::fromStdString(sessions_->last_peer())
                                : QString();
}

bool QtEventBridge::sessionBusy() const {
    using SessState = RemoteSessionService::SessState;
    return sessions_ != nullptr && sessions_->state() != SessState::Idle;
}

bool QtEventBridge::validAlias(const QString& alias) const {
    // PRD P5 别名治理：trim 后 3~20 字符（QString::length 按字符计，中文占 1）
    const QString t = alias.trimmed();
    return t.length() >= 3 && t.length() <= 20;
}

bool QtEventBridge::startControl(const QString& alias, bool unattended,
                                 const QString& password) {
    if (sessions_ == nullptr) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "session",
                 "发起协助失败：服务未就绪");
        emit controlStartFailed(QStringLiteral("服务未就绪"));
        return false;
    }
    // 寻址失败细分文案（PRD P5）：connect_to_peer 返回 bool 无原因，桥层
    // 按可查状态预判——自连/会话占用/设备不在线三态
    const QString self = selfAlias();
    if (!self.isEmpty() && alias == self) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "session",
                 "发起协助失败：目标为本机 [alias=" + alias.toStdString() + "]");
        emit controlStartFailed(QStringLiteral("不能发起对本机的协助"));
        return false;
    }
    using SessState = RemoteSessionService::SessState;
    if (sessions_->state() != SessState::Idle) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "session",
                 "发起协助失败：会话占用 state=" + std::to_string(
                     static_cast<int>(sessions_->state())));
        emit controlStartFailed(QStringLiteral("当前会话尚未结束，请先断开"));
        return false;
    }
    DeviceInfo peer_info;
    if (aliases_ == nullptr || !aliases_->resolve(alias.toStdString(), peer_info)) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "session",
                 "发起协助失败：未发现设备 [alias=" + alias.toStdString() + "]");
        emit controlStartFailed(QStringLiteral("未发现设备 [%1]，对方可能已下线")
                                    .arg(alias));
        return false;
    }
    return sessions_->connect_to_peer(alias.toStdString(), unattended,
                                      password.toStdString());
}

bool QtEventBridge::sendFile(const QString& absPath) {
    return files_ != nullptr && files_->send_file(absPath.toStdString());
}

bool QtEventBridge::sessionRunning() const {
    return sessions_ != nullptr
        && sessions_->state() == RemoteSessionService::SessState::Running;
}

void QtEventBridge::copyText(const QString& text) {
    QApplication::clipboard()->setText(text);
}

void QtEventBridge::setSelfWindowTopMost(bool on) {
    // PRD 确认三件套：确认弹窗依附窗口层，窗口不置顶弹窗必被盖。
    // Win32 系统级 TOPMOST（不动 flags 不重建原生窗口）；其余平台降级激活
    for (QWindow* w : QGuiApplication::topLevelWindows()) {
        if (!w->isVisible() || w->type() != Qt::Window) {
            continue;
        }
#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(w->winId());
        SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
#endif
        if (on) {
            w->raise();
            w->requestActivate();
        }
    }
}

void QtEventBridge::sendMouse(int action, int x, int y, int wheel) {
    if (sessions_ == nullptr) {
        return;
    }
    protocol::MouseKeyBody mk;
    mk.kind = protocol::MouseKeyKind::Mouse;
    mk.action = static_cast<uint8_t>(action);
    mk.x = static_cast<uint16_t>(x);
    mk.y = static_cast<uint16_t>(y);
    mk.wheel = static_cast<int16_t>(wheel);
    (void)sessions_->send_input_event(mk);
}

void QtEventBridge::sendKey(int vk, int scan, bool down) {
    if (sessions_ == nullptr) {
        return;
    }
    protocol::MouseKeyBody mk;
    mk.kind = protocol::MouseKeyKind::Keyboard;
    mk.vk = static_cast<uint16_t>(vk);
    mk.scan = static_cast<uint16_t>(scan);
    mk.down = down ? 1 : 0;
    (void)sessions_->send_input_event(mk);
}

bool QtEventBridge::acceptFile(const QString& fileId) {
    return files_ != nullptr && files_->accept_pending(fileId.toStdString());
}

bool QtEventBridge::rejectFile(const QString& fileId) {
    return files_ != nullptr && files_->reject_pending(fileId.toStdString());
}

bool QtEventBridge::setUnattendedEnabled(bool enabled, const QString& password) {
    // TODO(V1.0): 经 UnattendedAuthenticator::set_password 走线程池 PBKDF2 →
    //   写 config.ini [security]（Doc16 §6）；骨架期仅切开关
    unattended_ = enabled;
    emit unattendedChanged();
    return true;
}

void QtEventBridge::set_trust_store(TrustStore* ts) {
    trust_ = ts;
}

bool QtEventBridge::isTrusted(const QString& code) {
    return trust_ != nullptr && trust_->is_trusted(code.toStdString());
}

void QtEventBridge::trust(const QString& code, const QString& name) {
    if (trust_ != nullptr) {
        trust_->trust(code.toStdString(), name.toStdString());
    }
}

void QtEventBridge::clearTrust() {
    if (trust_ != nullptr) {
        trust_->clear();
    }
}

QVariantList QtEventBridge::listTrusted() const {
    QVariantList out;
    if (trust_ == nullptr) {
        return out;
    }
    // trusted_at "2026-10-25 14:32:05" → 相对时间文案（今天/昨天/同年/更早）
    const QDate today = QDate::currentDate();
    for (const auto& e : trust_->list()) {
        const QDateTime t = QDateTime::fromString(
            QString::fromStdString(e.trusted_at), "yyyy-MM-dd HH:mm:ss");
        QString time_text;
        if (t.date() == today) {
            time_text = t.toString("'今天' HH:mm");
        } else if (t.date() == today.addDays(-1)) {
            time_text = t.toString("'昨天' HH:mm");
        } else if (t.date().year() == today.year()) {
            time_text = t.toString("MM-dd HH:mm");
        } else {
            time_text = t.toString("yyyy-MM-dd");
        }
        // 9 位裸码 → "xxx xxx xxx" 三段展示
        const QString raw = QString::fromStdString(e.code);
        QString grouped;
        for (int i = 0; i < raw.size(); ++i) {
            if (i > 0 && i % 3 == 0) {
                grouped += QLatin1Char(' ');
            }
            grouped += raw.at(i);
        }
        out.append(QVariantMap{
            {"code", grouped},
            {"name", QString::fromStdString(e.name)},
            {"note", QString::fromStdString(e.note)},
            {"time", time_text}});
    }
    return out;
}

void QtEventBridge::untrust(const QString& code) {
    if (trust_ == nullptr) {
        return;
    }
    // UI 传三段展示码，TrustStore 归一化去空格
    trust_->untrust(code.toStdString());
    SCLD_LOG(log_, foundation::LogLevel::Warn, "trust",
             "删除信任 code=" + code.toStdString());  // 审计：Warn 即时落盘
}

bool QtEventBridge::setTrustNote(const QString& code, const QString& note) {
    if (trust_ == nullptr) {
        return false;
    }
    const bool ok = trust_->set_note(code.toStdString(), note.toStdString());
    if (ok) {
        SCLD_LOG(log_, foundation::LogLevel::Info, "trust",
                 "设置备注 code=" + code.toStdString());
    }
    return ok;
}

// ---------------- 设置页接线（Doc18 §3.3） ----------------

namespace {

bool parse_level(const std::string& s, foundation::LogLevel& out) {
    if (s == "trace") {
        out = foundation::LogLevel::Trace;
    }
    else if (s == "debug") {
        out = foundation::LogLevel::Debug;
    }
    else if (s == "info") {
        out = foundation::LogLevel::Info;
    }
    else if (s == "warn") {
        out = foundation::LogLevel::Warn;
    }
    else if (s == "error") {
        out = foundation::LogLevel::Error;
    }
    else {
        return false;
    }
    return true;
}

}  // namespace

void QtEventBridge::set_config_store(foundation::IniConfig* ini, AppConfig* cfg) {
    ini_ = ini;
    cfg_ = cfg;
}

void QtEventBridge::set_discovery(UdpDiscoveryAdapter* d) {
    discovery_ = d;
}

QString QtEventBridge::settingsValue(const QString& key) const {
    if (cfg_ == nullptr) {
        return {};
    }
    const std::string k = key.toStdString();
    if (k == "general.alias") {
        return QString::fromStdString(cfg_->alias);
    }
    if (k == "app.auto_start") {
        return cfg_->auto_start ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (k == "app.minimize_to_tray") {
        return cfg_->minimize_to_tray ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (k == "appearance.theme") {
        return QString::fromStdString(cfg_->appearance_theme);
    }
    if (k == "appearance.reduce_transparency") {
        return cfg_->appearance_reduce_transparency ? QStringLiteral("true")
                                                    : QStringLiteral("false");
    }
    if (k == "network.ws_port") {
        return QString::number(cfg_->ws_port);
    }
    if (k == "network.multicast_addr") {
        return QString::fromStdString(cfg_->multicast_addr);
    }
    if (k == "network.heartbeat_interval_s") {
        return QString::number(cfg_->heartbeat_interval_s);
    }
    if (k == "session.fps") {
        return QString::number(cfg_->fps);
    }
    if (k == "session.jpeg_quality") {
        return QString::number(cfg_->jpeg_quality);
    }
    if (k == "file.save_path") {
        return QString::fromStdString(cfg_->save_path);
    }
    if (k == "file.chunk_size_kb") {
        return QString::number(cfg_->chunk_size_kb);
    }
    if (k == "file.confirm_before_transfer") {
        return cfg_->confirm_before_transfer ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (k == "file.safe_path") {
        return cfg_->file_safe_path ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (k == "security.unattended_enabled") {
        return cfg_->unattended_enabled ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (k == "log.level") {
        return QString::fromStdString(cfg_->log_level);
    }
    if (k == "log.max_size_mb") {
        return QString::number(cfg_->log_max_size_mb);
    }
    if (k == "log.max_files") {
        return QString::number(cfg_->log_max_files);
    }
    return {};
}

void QtEventBridge::settingsSetValue(const QString& key, const QString& value) {
    if (ini_ == nullptr || cfg_ == nullptr) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "settings",
                 "配置未注入，设置变更被忽略 key=" + key.toStdString());
        return;
    }
    const std::string k = key.toStdString();
    const std::string v = value.toStdString();
    auto warn_invalid = [&](const char* why) {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "settings",
                 "非法值已忽略 " + k + "=" + v + "（" + why + "）");
    };
    auto persist = [&](const char* note) {
        cfg_->save(*ini_);
        if (ini_->save()) {
            SCLD_LOG(log_, foundation::LogLevel::Info, "settings",
                     "已保存 " + k + "=" + v + note);
        }
        else {
            SCLD_LOG(log_, foundation::LogLevel::Error, "settings",
                     "config.ini 落盘失败");
        }
    };
    bool num_ok = false;

    if (k == "general.alias") {
        if (!validAlias(value)) {
            // PRD P5 别名治理：3~20 字符，拒绝不落盘，UI 经 settingsRejected 提示
            emit settingsRejected(key, QStringLiteral("连接名需 3~20 个字符"));
            SCLD_LOG(log_, foundation::LogLevel::Warn, "settings",
                     "连接名非法已拒绝 alias=" + v);
            return;
        }
        cfg_->alias = v;
        if (aliases_ != nullptr) {
            aliases_->set_self_alias(v);
        }
        if (discovery_ != nullptr) {
            discovery_->update_self(v, static_cast<uint16_t>(cfg_->ws_port));
        }
        persist("");
    }
    else if (k == "app.auto_start") {
        cfg_->auto_start = (value == QStringLiteral("true"));
        (void)platform::set_auto_start(
            cfg_->auto_start, QCoreApplication::applicationFilePath().toStdWString());
        persist("");
    }
    else if (k == "app.minimize_to_tray") {
        cfg_->minimize_to_tray = (value == QStringLiteral("true"));
        persist("");
    }
    else if (k == "appearance.theme") {
        if (v != "dark" && v != "light") {
            warn_invalid("仅 dark/light");
            return;
        }
        cfg_->appearance_theme = v;
        persist("");
    }
    else if (k == "appearance.reduce_transparency") {
        cfg_->appearance_reduce_transparency = (value == QStringLiteral("true"));
        persist("");
    }
    else if (k == "network.ws_port") {
        const int port = value.toInt(&num_ok);
        if (!num_ok || port < 1024 || port > 65535) {
            warn_invalid("1024~65535");
            return;
        }
        cfg_->ws_port = port;  // 监听 socket 构建期绑定，重启生效
        persist("（重启生效）");
    }
    else if (k == "network.multicast_addr") {
        cfg_->multicast_addr = v;  // 适配器 Options 构建期固定，重启生效
        persist("（重启生效）");
    }
    else if (k == "network.heartbeat_interval_s") {
        const int s = value.toInt(&num_ok);
        if (!num_ok || s < 1 || s > 60) {
            warn_invalid("1~60");
            return;
        }
        cfg_->heartbeat_interval_s = s;
        if (discovery_ != nullptr) {
            discovery_->set_heartbeat_interval(s * 1000);
        }
        persist("");
    }
    else if (k == "session.fps") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || (n != 10 && n != 15 && n != 24 && n != 30)) {
            warn_invalid("档位 10/15/24/30");
            return;
        }
        cfg_->fps = n;
        if (sessions_ != nullptr) {
            sessions_->set_stream_params(cfg_->fps, cfg_->jpeg_quality);
        }
        persist("");
    }
    else if (k == "session.jpeg_quality") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 1 || n > 100) {
            warn_invalid("1~100");
            return;
        }
        cfg_->jpeg_quality = n;
        if (sessions_ != nullptr) {
            sessions_->set_stream_params(cfg_->fps, cfg_->jpeg_quality);
        }
        persist("");
    }
    else if (k == "file.save_path") {
        cfg_->save_path = v;
        if (files_ != nullptr) {
            files_->set_save_dir(value.isEmpty()
                ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                      .toStdString()
                : v);
        }
        persist("");
    }
    else if (k == "file.chunk_size_kb") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 16 || n > 4096) {
            warn_invalid("16~4096");
            return;
        }
        cfg_->chunk_size_kb = n;
        if (files_ != nullptr) {
            files_->set_chunk_size(static_cast<uint32_t>(n));
        }
        persist("");
    }
    else if (k == "file.confirm_before_transfer") {
        cfg_->confirm_before_transfer = (value == QStringLiteral("true"));
        if (files_ != nullptr) {
            files_->set_confirm_enabled(cfg_->confirm_before_transfer);
        }
        persist("");
    }
    else if (k == "file.safe_path") {
        cfg_->file_safe_path = (value == QStringLiteral("true"));
        if (files_ != nullptr) {
            files_->set_safe_path(cfg_->file_safe_path);
        }
        persist("");
    }
    else if (k == "security.unattended_enabled") {
        cfg_->unattended_enabled = (value == QStringLiteral("true"));
        (void)setUnattendedEnabled(cfg_->unattended_enabled,
                                   QString());  // 内存开关同步；凭据 PBKDF2 为 V1.0 任务
        persist("");
    }
    else if (k == "log.level") {
        foundation::LogLevel lv;
        if (!parse_level(v, lv)) {
            warn_invalid("trace/debug/info/warn/error");
            return;
        }
        cfg_->log_level = v;
        if (log_ != nullptr) {
            log_->set_level(lv);  // 即时生效
        }
        persist("");
    }
    else if (k == "log.max_size_mb" || k == "log.max_files") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 1 || n > 1024) {
            warn_invalid("1~1024");
            return;
        }
        if (k == "log.max_size_mb") {
            cfg_->log_max_size_mb = n;
        }
        else {
            cfg_->log_max_files = n;
        }
        persist("（重启生效）");  // 日志后端滚动参数构建期固定
    }
    else {
        SCLD_LOG(log_, foundation::LogLevel::Warn, "settings", "未知设置键 " + k);
    }
}

}  // namespace sc::localdesk::platform
