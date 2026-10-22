// ============================================================
// scToLocalDesk | src/platform/qt_adapter/single_instance_guard.cpp
// 单实例守卫实现：探测连接（杀青 - 投递 raise）→ 失败则接管监听
// 监听失败不阻断启动：宁可极端场景双开，不可起不来（日志 + 容忍）
// ============================================================
#include "platform/qt_adapter/single_instance_guard.hpp"

#include <QLocalServer>
#include <QLocalSocket>

namespace sc::localdesk::platform {

namespace {

/// key 清洗为合法管道/socket 名：仅保留字母数字与 - _（exe 名含 .exe 后缀等）
QString server_name(const QString& key) {
    QString k;
    k.reserve(key.size());
    for (const QChar c : key) {
        k += (c.isLetterOrNumber() || c == '-' || c == '_') ? c : QChar(u'_');
    }
    return QStringLiteral("scToLocalDesk-") + k;
}

}  // namespace

SingleInstanceGuard::~SingleInstanceGuard() {
    if (server_ != nullptr) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
}

bool SingleInstanceGuard::acquire(const QString& key) {
    const QString name = server_name(key);

    // 1) 探测已有实例：连上即视为在跑，投递 raise 后返回 false（调用方退出）
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(300)) {
        probe.write("raise\n");
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return false;
    }

    // 2) Unix 残余 socket 文件清理（Windows 命名管道无此概念，调用无害）
    QLocalServer::removeServer(name);

    // 3) 接管监听，成为首实例
    server_ = new QLocalServer();
    if (!server_->listen(name)) {
        // 监听失败（权限/异常占用）：放弃互斥但不阻断启动
        server_->deleteLater();
        server_ = nullptr;
        return true;
    }
    QObject::connect(server_, &QLocalServer::newConnection,
                     [this] { on_new_connection(); });
    return true;
}

void SingleInstanceGuard::set_raise_callback(std::function<void()> cb) {
    raise_cb_ = std::move(cb);
}

void SingleInstanceGuard::on_new_connection() {
    while (server_ != nullptr && server_->hasPendingConnections()) {
        auto* conn = server_->nextPendingConnection();
        // 读走请求后即刻唤起（窗口可能正收拢在托盘）
        QObject::connect(conn, &QLocalSocket::disconnected,
                         conn, &QLocalSocket::deleteLater);
        conn->readAll();
        if (raise_cb_) {
            raise_cb_();
        }
    }
}

}  // namespace sc::localdesk::platform
