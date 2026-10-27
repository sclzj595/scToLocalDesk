// ============================================================
// scToLocalDesk | src/platform/qt_adapter/device_list_model.cpp
// 增量刷新实现：上线 upsert / 离线 erase / 周期对账仅动变化行
// ============================================================
#include "platform/qt_adapter/device_list_model.hpp"

#include <QTimer>

#include "core/events.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"
#include "service/alias_service.hpp"
#include "service/id_service.hpp"
#include "service/trust_store.hpp"

namespace sc::localdesk::platform {

namespace {
constexpr int kRefreshMs = 3000;  // 对账周期 = 默认心跳周期（Doc07 §4）
}  // namespace

DeviceListModel::DeviceListModel(std::shared_ptr<foundation::EventBus> bus,
                                 AliasService* aliases, TrustStore* trust,
                                 foundation::ILogger* logger, QObject* parent)
    : QAbstractListModel(parent),
      bus_(std::move(bus)),
      aliases_(aliases),
      trust_(trust),
      log_(logger) {
    // 事件回调在发布线程（主线程事件循环）同步执行，模型可直接变更
    if (bus_ != nullptr) {
        sub_online_ = bus_->subscribe<Evt_DeviceOnline>(
            [this](const Evt_DeviceOnline& e) { on_device_online(e.device_alias); });
        sub_offline_ = bus_->subscribe<Evt_DeviceOffline>(
            [this](const Evt_DeviceOffline& e) { on_device_offline(e.device_alias); });
    }
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DeviceListModel::refresh_changed);
    timer->start(kRefreshMs);
    // 启动回放：静态映射等装载先于本模型构造时上线事件已丢失（订阅时序），
    // 构造即全量拉取对账一次（与 Widgets device_list_page 全量口径一致）
    if (aliases_ != nullptr) {
        for (const auto& dev : aliases_->online_devices()) {
            on_device_online(dev.alias);
        }
    }
}

DeviceListModel::~DeviceListModel() {
    if (bus_ != nullptr) {
        if (sub_online_ != 0) {
            bus_->unsubscribe<Evt_DeviceOnline>(sub_online_);
        }
        if (sub_offline_ != 0) {
            bus_->unsubscribe<Evt_DeviceOffline>(sub_offline_);
        }
    }
}

int DeviceListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int DeviceListModel::count() const {
    return static_cast<int>(rows_.size());
}

QHash<int, QByteArray> DeviceListModel::roleNames() const {
    return {{kNameRole, "name"},
            {kCodeTextRole, "codeText"},
            {kCodeRawRole, "codeRaw"},
            {kIpRole, "ip"},
            {kBusyRole, "busy"},
            {kTrustedRole, "trusted"}};
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const Row& row = rows_[static_cast<size_t>(index.row())];
    switch (role) {
        case kNameRole:
            return QString::fromStdString(row.info.alias);
        case kCodeTextRole:
            return row.info.code == 0
                       ? QStringLiteral("--- --- ---")
                       : QString::fromStdString(IdService::format_code(row.info.code));
        case kCodeRawRole:
            return row.info.code == 0
                       ? QString()
                       : QString::number(row.info.code).rightJustified(9, '0');
        case kIpRole:
            return QString::fromStdString(row.info.ip);
        case kBusyRole:
            return row.info.status == 0x01;
        case kTrustedRole:
            return row.trusted;
        default:
            return {};
    }
}

int DeviceListModel::find_row(const std::string& alias) const {
    const auto it = index_.find(alias);
    return it == index_.end() ? -1 : it->second;
}

void DeviceListModel::resync_trust(Row& row) const {
    // 清空信任缓存后角标同步熄灭：不短路缓存，对账周期逐行重查（局域网量级）
    row.trusted = trust_ != nullptr && row.info.code != 0
                  && trust_->is_trusted(QString::number(row.info.code)
                                            .rightJustified(9, '0')
                                            .toStdString());
}

void DeviceListModel::on_device_online(const std::string& alias) {
    if (aliases_ == nullptr) {
        return;
    }
    DeviceInfo opt;
    if (!aliases_->resolve(alias, opt)) {
        return;
    }
    const int row = find_row(alias);
    if (row >= 0) {
        rows_[static_cast<size_t>(row)].info = opt;
        resync_trust(rows_[static_cast<size_t>(row)]);
        emit dataChanged(index(row, 0), index(row, 0));
        return;
    }
    const int insert = static_cast<int>(rows_.size());
    beginInsertRows(QModelIndex(), insert, insert);
    Row r;
    r.info = opt;
    resync_trust(r);
    rows_.push_back(r);
    index_[alias] = insert;
    endInsertRows();
    emit countChanged();
    SCLD_LOG(log_, foundation::LogLevel::Info, "devices",
             "列表新增 [alias=" + alias + "] code="
                 + (opt.code == 0 ? "---" : std::to_string(opt.code)));
}

void DeviceListModel::on_device_offline(const std::string& alias) {
    const int row = find_row(alias);
    if (row < 0) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    rows_.erase(rows_.begin() + row);
    index_.erase(alias);
    for (auto& kv : index_) {
        if (kv.second > row) {
            --kv.second;  // 行号回填
        }
    }
    endRemoveRows();
    emit countChanged();
}

void DeviceListModel::refresh_changed() {
    if (aliases_ == nullptr) {
        return;
    }
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        DeviceInfo opt;
        if (!aliases_->resolve(row.info.alias, opt)) {
            continue;  // 离线事件随后到达，由 on_device_offline 移除
        }
        bool dirty = false;
        if (opt.status != row.info.status || opt.ip != row.info.ip
            || (opt.code != 0 && opt.code != row.info.code)) {
            row.info = opt;
            resync_trust(row);  // 清空信任缓存/新记忆后角标随对账同步
            dirty = true;
        }
        if (dirty) {
            emit dataChanged(index(static_cast<int>(i), 0),
                             index(static_cast<int>(i), 0));
        }
    }
}

}  // namespace sc::localdesk::platform
