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

#include <service/alias_service.hpp>
#include <service/file_service.hpp>
#include <service/remote_session_service.hpp>

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

}  // namespace sc::localdesk::platform
