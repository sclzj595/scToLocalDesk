// scToLocalDesk | 文件传输列表页（FileTransferPage）：真事件流 + 发送入口
// Evt_File* → 桥接信号 → 动态行卡（进度/完成/失败）；按钮选文件 + 拖拽发送
#include "pages.h"
#include "page_kit.h"

#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "../glass_theme.h"
#include "platform/qt_adapter/qt_event_bridge.hpp"

namespace sc::localdesk::ui {

namespace {
QString human_size(qint64 bytes) {
    if (bytes >= 1073741824) {
        return QString::number(bytes / 1073741824.0, 'f', 2) + QStringLiteral(" GB");
    }
    if (bytes >= 1048576) {
        return QString::number(bytes / 1048576.0, 'f', 1) + QStringLiteral(" MB");
    }
    if (bytes >= 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    return QString::number(bytes) + QStringLiteral(" B");
}
}  // namespace

// 每行引用（find/upsert 复用，避免重建控件）；定义在 pages.h（QHash 值需完整类型）

FileTransferPage::FileTransferPage(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);  // 拖拽发送入口（Doc18 §5）
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(16);

    auto* head = new QHBoxLayout();
    head->addWidget(makeTitle(QStringLiteral("文件传输")));
    head->addStretch();
    auto* sendBtn = new QPushButton(QStringLiteral("发送文件…"));
    sendBtn->setObjectName(QStringLiteral("primaryBtn"));
    head->addWidget(sendBtn);
    lay->addLayout(head);

    // 行容器（拖放事件由本页统一处理：host_ 不 acceptDrops，向上冒泡到 page）
    host_ = new QWidget(this);
    auto* host_lay = new QVBoxLayout(host_);
    host_lay->setContentsMargins(1, 1, 1, 1);
    host_lay->setSpacing(12);
    host_lay->addStretch();

    // 拖放提示条
    drag_hint_ = new QLabel(QStringLiteral("可将文件拖入本区直接发送"), host_);
    drag_hint_->setAlignment(Qt::AlignCenter);
    drag_hint_->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px; border: 1px dashed %2; border-radius: 12px;"
        "padding: 18px;")
        .arg(GlassTheme::instance().textHint().name(QColor::HexRgb),
             GlassTheme::instance().divider().name(QColor::HexRgb)));
    host_lay->insertWidget(0, drag_hint_);
    lay->addWidget(host_, 1);

    // 空态占位（禁止假数据）
    empty_ = new QLabel(QStringLiteral("暂无传输任务\n连接设备后，从设备卡或\"发送文件\"发起传输"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px;")
        .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    lay->addWidget(empty_, 1);
    empty_->show();
    host_->hide();

    connect(sendBtn, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("选择要发送的文件"));
        if (!files.isEmpty()) {
            emit filesPicked(files);
        }
    });
}

FileRow& FileTransferPage::upsert_row(const QString& file_id, const QString& name, int dir) {
    const auto it = rows_.find(file_id);
    if (it != rows_.end()) {
        return *it;
    }
    const bool send = dir == 0;
    auto* card = new QFrame(host_);
    card->setObjectName(QStringLiteral("deviceCard"));
    card->setFixedHeight(68);
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(14);

    auto* dirLbl = new QLabel(send ? QStringLiteral("↑") : QStringLiteral("↓"), card);
    dirLbl->setStyleSheet(QStringLiteral(
        "font-size: 17px; font-weight: 600; color: %1;")
        .arg((send ? GlassTheme::instance().primary()
                   : GlassTheme::instance().accentBlue())
                 .name(QColor::HexRgb)));
    row->addWidget(dirLbl);

    auto* nameLbl = new QLabel(name, card);
    nameLbl->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;")
                               .arg(GlassTheme::instance().textMain().name(QColor::HexRgb)));
    row->addWidget(nameLbl);
    row->addStretch();

    auto* sizeLbl = new QLabel(card);
    sizeLbl->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                               .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    row->addWidget(sizeLbl);

    auto* bar = new QProgressBar(card);
    bar->setFixedSize(200, 6);
    bar->setTextVisible(false);
    bar->setRange(0, 100);
    bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: rgba(110,118,129,0.25); border: none;"
        " border-radius: 3px; }"
        "QProgressBar::chunk { background: %1; border-radius: 3px; }")
        .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    row->addWidget(bar);

    auto* stLbl = new QLabel(card);
    stLbl->setStyleSheet(QStringLiteral("font-size: 12px;"));
    row->addWidget(stLbl);

    // 插到 stretch 之前
    auto* host_lay = static_cast<QVBoxLayout*>(host_->layout());
    host_lay->insertWidget(host_lay->count() - 1, card);
    drag_hint_->hide();
    empty_->hide();
    host_->show();
    return *rows_.insert(file_id, FileRow{bar, stLbl, sizeLbl});
}

void FileTransferPage::on_file_progress(const QString& file_id, const QString& name,
                                        qint64 transferred, qint64 total, int dir) {
    FileRow& r = upsert_row(file_id, name, dir);
    r.size->setText(human_size(total));
    const int pct = total > 0 ? static_cast<int>(transferred * 100 / total) : 0;
    r.bar->setValue(pct);
    r.status->setText(QStringLiteral("%1%").arg(pct));
    r.status->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                                .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
}

void FileTransferPage::on_file_done(const QString& file_id, const QString& name, int dir) {
    FileRow& r = upsert_row(file_id, name, dir);
    r.bar->setValue(100);
    r.status->setText(QStringLiteral("已完成"));
    r.status->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                                .arg(GlassTheme::instance().accentGreen().name(QColor::HexRgb)));
}

void FileTransferPage::on_file_failed(const QString& file_id, const QString& name, int dir) {
    FileRow& r = upsert_row(file_id, name, dir);
    r.status->setText(QStringLiteral("失败"));
    r.status->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                                .arg(GlassTheme::instance().accentRed().name(QColor::HexRgb)));
    r.bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: rgba(110,118,129,0.25); border: none;"
        " border-radius: 3px; }"
        "QProgressBar::chunk { background: #F72585; border-radius: 3px; }"));
}

void FileTransferPage::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

void FileTransferPage::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            paths << u.toLocalFile();
        }
    }
    if (!paths.isEmpty()) {
        emit filesPicked(paths);
    }
}

}  // namespace sc::localdesk::ui
