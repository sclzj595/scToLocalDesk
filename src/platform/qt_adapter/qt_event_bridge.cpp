// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_event_bridge.cpp
// 桥实现：EventBus 订阅（Qt::QueuedConnection 跨线程投递到主线程）
// ============================================================
#include "platform/qt_adapter/qt_event_bridge.hpp"

#include <core/events.hpp>

#include <QApplication>
#include <QClipboard>
#include <QTimer>

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
    bus->subscribe<Evt_ConnectRequest>([this](const Evt_ConnectRequest& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            if (!e.auto_approved) {
                emit connectRequest(QString::fromStdString(e.device_alias));
            }
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_SessionStart>([this](const Evt_SessionStart& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit sessionStarted(QString::fromStdString(e.device_alias));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_SessionClosed>([this](const Evt_SessionClosed& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            static constexpr const char* kText[] = {
                "协助已结束", "对方拒绝了协助", "访问密码不正确",
                "请求等待超时", "对方正在协助中", "网络中断，协助已断开", "对方已断开协助"};
            const int idx = static_cast<int>(e.reason);
            emit sessionClosed(QString::fromUtf8(
                kText[idx >= 0 && idx < 7 ? idx : 0]));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileProgress>([this](const Evt_FileProgress& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileProgress(QString::fromStdString(e.file_id),
                              static_cast<qint64>(e.transferred),
                              static_cast<qint64>(e.total));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileDone>([this](const Evt_FileDone& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileDone(QString::fromStdString(e.file_id));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileFailed>([this](const Evt_FileFailed& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileFailed(QString::fromStdString(e.file_id));
        }, Qt::QueuedConnection);
    });
    bus->subscribe<Evt_FileMetaReceived>([this](const Evt_FileMetaReceived& e) {
        QMetaObject::invokeMethod(this, [this, e] {
            emit fileMetaReceived(QString::fromStdString(e.file_id),
                                  QString::fromStdString(e.name),
                                  static_cast<qint64>(e.size));
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

QString QtEventBridge::deviceIp(const QString& code) const {
    if (aliases_ == nullptr) {
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

bool QtEventBridge::startControl(const QString& alias, bool unattended,
                                 const QString& password) {
    return sessions_ != nullptr
        && sessions_->connect_to_peer(alias.toStdString(), unattended,
                                      password.toStdString());
}

bool QtEventBridge::sendFile(const QString& absPath) {
    return files_ != nullptr && files_->send_file(absPath.toStdString());
}

void QtEventBridge::copyText(const QString& text) {
    QApplication::clipboard()->setText(text);
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
        if (v.empty()) {
            warn_invalid("不能为空");
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
