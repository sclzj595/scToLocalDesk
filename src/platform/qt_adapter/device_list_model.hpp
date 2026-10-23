// ============================================================
// scToLocalDesk | src/platform/qt_adapter/device_list_model.hpp
// 设备列表模型（QAbstractListModel，Doc18 §3.4）：EventBus 上下线事件
// 驱动增量插入/删除，3s 轮询仅对变化行发 dataChanged（防整表刷新抖动）
// ============================================================
#pragma once

#include <QAbstractListModel>

#include <memory>
#include <unordered_map>

#include "core/interface/types.hpp"

namespace sc::localdesk {

class AliasService;
class TrustStore;

namespace foundation {
class EventBus;
class ILogger;
}

namespace platform {

class DeviceListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        kNameRole = Qt::UserRole + 1,  ///< 连接名（alias）
        kCodeTextRole,                 ///< "xxx xxx xxx"；旧包无码为 "--- --- ---"
        kCodeRawRole,                  ///< 9 位数字串；未知为空串
        kIpRole,
        kBusyRole,                     ///< 对端会话中（状态点绿/蓝）
        kTrustedRole,                  ///< TrustStore 已记忆
    };

    DeviceListModel(std::shared_ptr<foundation::EventBus> bus,
                    AliasService* aliases, TrustStore* trust,
                    foundation::ILogger* logger, QObject* parent = nullptr);
    ~DeviceListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const;

    /// 供 Widgets 复用的增量接口：上线/离线/变化时调用
    void on_device_online(const std::string& alias);
    void on_device_offline(const std::string& alias);
    void refresh_changed();  ///< 心跳周期同步 busy/code/ip 变化

signals:
    void countChanged();

private:
    struct Row {
        DeviceInfo info;
        bool trusted{false};
    };
    int find_row(const std::string& alias) const;
    void resync_trust(Row& row) const;

    std::shared_ptr<foundation::EventBus> bus_;
    AliasService* aliases_{nullptr};
    TrustStore* trust_{nullptr};
    foundation::ILogger* log_{nullptr};
    std::vector<Row> rows_;
    std::unordered_map<std::string, int> index_;  // alias → 行号
    uint64_t sub_online_{0};
    uint64_t sub_offline_{0};
};

}  // namespace platform
}  // namespace sc::localdesk
