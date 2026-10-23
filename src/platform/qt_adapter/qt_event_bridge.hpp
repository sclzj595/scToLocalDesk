// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_event_bridge.hpp
// EventBus → Qt 信号桥 + QML 反向调用入口（Doc13 §4 数据流红线）
// QML 只 import 本桥（注册为单例 ScLocaldesk），不接触任何 core 头
// ============================================================
#pragma once

#include <QObject>
#include <QString>

namespace sc::localdesk {

class AliasService;
class RemoteSessionService;
class FileService;
class TrustStore;
class AppConfig;
class UnattendedPasswordAuthenticator;

namespace foundation {
class EventBus;
class ILogger;
class IniConfig;
}

namespace platform {

class UdpDiscoveryAdapter;

class QtEventBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString selfAlias READ selfAlias NOTIFY selfAliasChanged)
    Q_PROPERTY(bool unattendedEnabled READ unattendedEnabled NOTIFY unattendedChanged)
    Q_PROPERTY(bool listening READ listening CONSTANT)
    /// Qt 6.5+ 才有 MultiEffect 真模糊（SCLD_HAS_MULTIEFFECT 编译期注入）；
    /// Qt 6.2（远程 Linux）QML 据此退化为半透底玻璃
    Q_PROPERTY(bool hasGlassBlur READ hasGlassBlur CONSTANT)

public:
    /// DI 组装后调用：绑定服务并订阅全部领域事件（回调线程 → Queued 信号）
    void init(foundation::EventBus* bus,
              AliasService* aliases,
              RemoteSessionService* sessions,
              FileService* files,
              foundation::ILogger* logger);

    // ---------------- QML 绑定 ----------------

    [[nodiscard]] QString selfAlias() const;
    [[nodiscard]] bool unattendedEnabled() const;
    [[nodiscard]] static bool hasGlassBlur() {
#ifdef SCLD_HAS_MULTIEFFECT
        return SCLD_HAS_MULTIEFFECT != 0;
#else
        return false;
#endif
    }

    // ---------------- QML 调用（Q_INVOKABLE） ----------------

    Q_INVOKABLE void approve();                    ///< 确认弹窗【同意】
    Q_INVOKABLE void reject();                     ///< 确认弹窗【拒绝】
    Q_INVOKABLE void disconnectSession();          ///< 一键断开（悬浮按钮）
    Q_INVOKABLE bool startControl(const QString& alias, bool unattended,
                                  const QString& password);
    Q_INVOKABLE bool sendFile(const QString& absPath);
    Q_INVOKABLE bool acceptFile(const QString& fileId);   ///< 接收确认弹窗【同意】
    Q_INVOKABLE bool rejectFile(const QString& fileId);   ///< 接收确认弹窗【拒绝】
    Q_INVOKABLE bool setUnattendedEnabled(bool enabled, const QString& password);
    Q_INVOKABLE void copyText(const QString& text);       ///< 连接码复制进剪贴板

    /// 信任缓存（Doc18 §3.2）：QML 确认弹窗/直连判定/清空入口
    void set_trust_store(TrustStore* ts);
    Q_INVOKABLE bool isTrusted(const QString& code);
    Q_INVOKABLE void trust(const QString& code, const QString& name);
    Q_INVOKABLE void clearTrust();

    /// 设置页接线（Doc18 §3.3）：7 组字段 ↔ AppConfig，变更即落盘 config.ini
    void set_config_store(foundation::IniConfig* ini, AppConfig* cfg);
    void set_discovery(UdpDiscoveryAdapter* d);
    /// 被控监听启动结果（端口占用 UI 提示，Doc18 风险清单 P1 顺带项）
    void set_listening(bool on) {
        listening_ = on;
    }
    [[nodiscard]] bool listening() const {
        return listening_;
    }
    /// 读配置值（key 契约同 AppConfig section.key，未知返回空串）
    Q_INVOKABLE QString settingsValue(const QString& key) const;
    /// 写配置值：校验 → 更新 AppConfig → 全量落盘 → 即时生效项接服务 setter
    Q_INVOKABLE void settingsSetValue(const QString& key, const QString& value);

signals:
    // 发现
    void deviceOnline(const QString& alias, const QString& ip);
    void deviceOffline(const QString& alias);
    void aliasConflict(const QString& alias, const QString& conflictIp);
    // 会话
    void connectRequest(const QString& alias);      ///< 人工确认弹窗触发
    void sessionStarted(const QString& alias);
    void sessionClosed(const QString& reasonText);
    // 文件
    void fileProgress(const QString& fileId, qint64 transferred, qint64 total);
    void fileDone(const QString& fileId);
    void fileFailed(const QString& fileId);
    void fileMetaReceived(const QString& fileId, const QString& name, qint64 size);  ///< 接收确认弹窗触发
    // 无人值守
    void unattendedLogin(const QString& alias);     ///< Toast 触发（Doc16 §7）
    void unattendedChanged();
    void selfAliasChanged();

private:
    foundation::EventBus* bus_{nullptr};
    AliasService* aliases_{nullptr};
    RemoteSessionService* sessions_{nullptr};
    FileService* files_{nullptr};
    TrustStore* trust_{nullptr};
    foundation::IniConfig* ini_{nullptr};
    AppConfig* cfg_{nullptr};
    UdpDiscoveryAdapter* discovery_{nullptr};
    foundation::ILogger* log_{nullptr};
    QString self_alias_;
    bool unattended_{false};
    bool listening_{false};
};

}  // namespace platform
}  // namespace sc::localdesk
