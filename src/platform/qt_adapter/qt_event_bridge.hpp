// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_event_bridge.hpp
// EventBus → Qt 信号桥 + QML 反向调用入口（Doc13 §4 数据流红线）
// QML 只 import 本桥（注册为单例 ScLocaldesk），不接触任何 core 头
// ============================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

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
    Q_PROPERTY(QString selfCode READ selfCode CONSTANT)
    Q_PROPERTY(bool unattendedEnabled READ unattendedEnabled NOTIFY unattendedChanged)
    Q_PROPERTY(bool listening READ listening CONSTANT)
    /// 会话占用中（PRD P7 防重复发起）：连接/会话期 true，设备卡点击拦截
    Q_PROPERTY(bool sessionBusy READ sessionBusy NOTIFY sessionBusyChanged)
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

    QString selfAlias() const;
    bool unattendedEnabled() const;
    static bool hasGlassBlur() {
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
    /// 控制端取消连接（PRD P7）：拨号/等确认期取消；Running 不响应
    Q_INVOKABLE void cancelConnect();
    /// 最近一次成功发起的别名（重连按钮数据源）；无则空
    Q_INVOKABLE QString lastPeerAlias() const;
    bool sessionBusy() const;
    Q_INVOKABLE bool startControl(const QString& alias, bool unattended,
                                  const QString& password);
    Q_INVOKABLE bool sendFile(const QString& absPath);
    /// 会话 Running 判定（发送文件前置：文件走会话业务帧通道）
    Q_INVOKABLE bool sessionRunning() const;
    Q_INVOKABLE bool acceptFile(const QString& fileId);   ///< 接收确认弹窗【同意】
    Q_INVOKABLE bool rejectFile(const QString& fileId);   ///< 接收确认弹窗【拒绝】
    Q_INVOKABLE bool setUnattendedEnabled(bool enabled, const QString& password);
    Q_INVOKABLE void copyText(const QString& text);       ///< 连接码复制进剪贴板
    /// 连接名校验（PRD P5 别名治理）：trim 后 3~20 字符；设置页/发起前共用
    Q_INVOKABLE bool validAlias(const QString& alias) const;
    /// 确认弹窗系统级置顶（PRD 确认三件套）：Win32 SetWindowPos TOPMOST，
    /// 弹窗关闭时调 off 恢复；非 Windows 平台降级 raise+activate
    Q_INVOKABLE void setSelfWindowTopMost(bool on);
    /// 键鼠透传发送（Doc18 §4.4）：RemoteInputItem 已归一化数据直发会话层
    Q_INVOKABLE void sendMouse(int action, int x, int y, int wheel);
    Q_INVOKABLE void sendKey(int vk, int scan, bool down);

    /// 信任缓存（Doc18 §3.2）：QML 确认弹窗/直连判定/清空入口
    void set_trust_store(TrustStore* ts);
    Q_INVOKABLE bool isTrusted(const QString& code);
    Q_INVOKABLE void trust(const QString& code, const QString& name);
    /// 单条删除信任（PRD P6）：信任列表条目级删除，trust.json 即时重写
    Q_INVOKABLE void untrust(const QString& code);
    /// 设置设备备注（PRD P6）：条目存在返回 true
    Q_INVOKABLE bool setTrustNote(const QString& code, const QString& note);
    Q_INVOKABLE void clearTrust();
    /// 信任全表（连接码页"最近连接"真数据源）：[{code9, name, time}]
    Q_INVOKABLE QVariantList listTrusted() const;

    /// 设置页接线（Doc18 §3.3）：7 组字段 ↔ AppConfig，变更即落盘 config.ini
    void set_config_store(foundation::IniConfig* ini, AppConfig* cfg);
    void set_discovery(UdpDiscoveryAdapter* d);
    /// 本机 9 位连接码（IdService 派生结果，P1.1；QML 本机信息卡/复制用）
    void set_self_code(uint32_t code) {
        self_code_ = code;
    }
    QString selfCode() const;
    /// 按连接码反查在线设备 IP（确认弹窗 peerIp 真值，Doc18 §3.4）；未知返回空
    Q_INVOKABLE QString deviceIp(const QString& code) const;
    /// 按连接码反查在线设备别名（连接码页发起会话用，Doc18 §4.3）；未知返回空
    Q_INVOKABLE QString deviceAlias(const QString& code) const;
    /// 按别名反查在线设备连接码（确认弹窗"对方连接码"核对行，三段格式）；未知返回空
    Q_INVOKABLE QString deviceCode(const QString& alias) const;
    /// 剪贴板纯文本（连接码三段输入的粘贴分发，QML 无剪贴板访问能力）
    Q_INVOKABLE QString clipboardText() const;
    /// 被控监听启动结果（端口占用 UI 提示，Doc18 风险清单 P1 顺带项）
    void set_listening(bool on) {
        listening_ = on;
    }
    bool listening() const {
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
    void aliasConflict(const QString& alias, const QString& conflictIp);  ///< 别名冲突（PRD P5：UI 引导改名）
    // 设置
    void settingsRejected(const QString& key, const QString& why);  ///< 校验拒绝（PRD P5：QML toast）
    /// 配置项成功落盘（每次 settingsSetValue 走 persist 即发；UI 层热联动挂点，
    /// 如托盘无人值守徽标 Doc16 §7）
    void configKeySaved(const QString& key, const QString& value);
    // 会话
    void connectRequest(const QString& alias, const QString& peerIp);  ///< 人工确认弹窗触发（含请求方 IP）
    void connectRequestTimedOut();  ///< 确认弹窗 30s 未响应已自动拒，UI 关弹窗
    void connectDenied(const QString& alias, const QString& reason);  ///< 本地拦截提示
    void controlStartFailed(const QString& reason);  ///< 发起协助失败细分文案（PRD P5）
    /// 控制端连接过程状态（PRD P7）：connecting/wait_confirm/rejected
    void connectStateChanged(QString stage, QString alias);
    void sessionBusyChanged();
    void sessionStarted(const QString& alias);
    void sessionClosed(const QString& reasonText);
    // 文件（dir: 0 发送 1 接收；name 供 UI 首次插条目）
    void fileProgress(const QString& fileId, const QString& name, qint64 transferred,
                      qint64 total, int dir);
    void fileDone(const QString& fileId, const QString& name, int dir);
    void fileFailed(const QString& fileId, const QString& name, int dir);
    void fileMetaReceived(const QString& fileId, const QString& name, qint64 size,
                          const QString& alias);  ///< 接收确认弹窗触发
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
    uint32_t self_code_{0};
    bool unattended_{false};
    bool listening_{false};
};

}  // namespace platform
}  // namespace sc::localdesk
