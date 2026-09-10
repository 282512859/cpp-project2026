#include "client_qt/FileBrowser.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHeaderView>
#include <QEvent>
#include <QMouseEvent>
#include <QLocale>
#include <QStandardItem>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QString friendlySize(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    const QStringList units = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = -1;
    do {
        value /= 1024.0;
        ++unit;
    } while (value >= 1024.0 && unit < units.size() - 1);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10.0 ? 1 : 0).arg(units.at(unit));
}

} // namespace

FileBrowser::FileBrowser(QWidget *parent)
    : QWidget(parent), view_(new QTreeView(this)), model_(new QStandardItemModel(this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);

    model_->setHorizontalHeaderLabels({
        QStringLiteral("文档名称"), QStringLiteral("大小"), QStringLiteral("修改时间"),
        QStringLiteral("创建者"), QStringLiteral("操作"), QStringLiteral("ID"), QStringLiteral("TYPE")
    });
    view_->setModel(model_);
    view_->setRootIsDecorated(false);
    view_->setAlternatingRowColors(false);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setUniformRowHeights(true);
    view_->setAnimated(false);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setMouseTracking(true);
    view_->viewport()->setMouseTracking(true);
    view_->viewport()->installEventFilter(this);
    view_->setColumnHidden(5, true);
    view_->setColumnHidden(6, true);

    auto *header = view_->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    view_->setMinimumHeight(360);

    hoverTimer_ = new QTimer(this);
    hoverTimer_->setSingleShot(true);
    hoverTimer_->setInterval(650);
    connect(hoverTimer_, &QTimer::timeout, this, &FileBrowser::onHoverTimeout);

    connect(view_, &QTreeView::activated, this, &FileBrowser::onActivated);
    connect(view_, &QTreeView::clicked, this, &FileBrowser::onClicked);
    connect(view_, &QWidget::customContextMenuRequested,
            this, &FileBrowser::onContextMenuRequested);
}

void FileBrowser::setCreatorName(const QString &name) {
    creatorName_ = name;
}

void FileBrowser::setEntries(const QVariantList &entries) {
    model_->removeRows(0, model_->rowCount());
    for (const auto &value : entries) {
        const auto map = value.toMap();
        const bool directory = map.value("directory").toBool();
        QList<QStandardItem *> row;

        auto *nameItem = new QStandardItem(map.value("name").toString());
        nameItem->setIcon(view_->style()->standardIcon(
            directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        nameItem->setData(map.value("size"), Qt::UserRole + 1);
        row.append(nameItem);

        auto *sizeItem = new QStandardItem(directory
            ? QStringLiteral("—")
            : friendlySize(map.value("size").toLongLong()));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.append(sizeItem);

        const auto modified = QDateTime::fromMSecsSinceEpoch(
            map.value("modifiedAt").toLongLong());
        row.append(new QStandardItem(
            QLocale().toString(modified, QStringLiteral("yyyy-MM-dd HH:mm"))));

        row.append(new QStandardItem(
            creatorName_.isEmpty() ? QStringLiteral("—") : creatorName_));
        row.append(new QStandardItem(directory
            ? QStringLiteral("双击打开 · 右键更多")
            : QStringLiteral("下载 · 右键更多")));
        row.append(new QStandardItem(QString::number(map.value("id").toLongLong())));
        row.append(new QStandardItem(directory ? QStringLiteral("DIR") : QStringLiteral("FILE")));
        model_->appendRow(row);
    }
}

bool FileBrowser::selectedNode(qint64 &nodeId, QString &name, bool &directory) const {
    const QModelIndex index = view_->currentIndex();
    if (!index.isValid()) return false;
    const int row = index.row();
    nodeId = model_->data(model_->index(row, 5)).toLongLong();
    name = model_->data(model_->index(row, 0)).toString();
    directory = model_->data(model_->index(row, 6)).toString() == QStringLiteral("DIR");
    return true;
}


bool FileBrowser::eventFilter(QObject *watched, QEvent *event) {
    if (watched == view_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            QModelIndex index = view_->indexAt(mouse->position().toPoint());
            if (index.isValid()) index = model_->index(index.row(), 0);
            hoverGlobalPos_ = view_->viewport()->mapToGlobal(mouse->position().toPoint());
            const bool changed = (!index.isValid() && hoverIndex_.isValid()) ||
                (index.isValid() && (!hoverIndex_.isValid() || index.row() != hoverIndex_.row()));
            if (changed) {
                hoverTimer_->stop();
                if (hoverIndex_.isValid()) emit previewHoverEnded();
                hoverIndex_ = QPersistentModelIndex(index);
                if (index.isValid()) {
                    const bool directory = model_->data(model_->index(index.row(), 6)).toString() == QStringLiteral("DIR");
                    if (!directory) hoverTimer_->start();
                    else hoverIndex_ = QPersistentModelIndex();
                }
            }
        } else if (event->type() == QEvent::Leave) {
            hoverTimer_->stop();
            hoverIndex_ = QPersistentModelIndex();
            emit previewHoverEnded();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FileBrowser::onClicked(const QModelIndex &index) {
    if (!index.isValid()) return;
    const int row = index.row();
    const bool directory = model_->data(model_->index(row, 6)).toString() == QStringLiteral("DIR");
    if (directory) {
        hoverTimer_->stop();
        hoverIndex_ = QPersistentModelIndex();
        emit previewHoverEnded();
        return;
    }
    hoverTimer_->stop();
    hoverIndex_ = QPersistentModelIndex(model_->index(row, 0));
    hoverGlobalPos_ = view_->viewport()->mapToGlobal(view_->visualRect(index).center());
    const qint64 id = model_->data(model_->index(row, 5)).toLongLong();
    const QString name = model_->data(model_->index(row, 0)).toString();
    const qint64 size = model_->data(model_->index(row, 0), Qt::UserRole + 1).toLongLong();
    emit previewHovered(id, name, size, hoverGlobalPos_);
}

void FileBrowser::onActivated(const QModelIndex &index) {
    if (!index.isValid()) return;
    const int row = index.row();
    const qint64 id = model_->data(model_->index(row, 5)).toLongLong();
    const bool directory = model_->data(model_->index(row, 6)).toString() == QStringLiteral("DIR");
    if (directory) {
        emit enterDirectory(id, model_->data(model_->index(row, 0)).toString());
    }
}


void FileBrowser::onHoverTimeout() {
    if (!hoverIndex_.isValid()) return;
    const int row = hoverIndex_.row();
    const bool directory = model_->data(model_->index(row, 6)).toString() == QStringLiteral("DIR");
    if (directory) return;
    const qint64 id = model_->data(model_->index(row, 5)).toLongLong();
    const QString name = model_->data(model_->index(row, 0)).toString();
    const qint64 size = model_->data(model_->index(row, 0), Qt::UserRole + 1).toLongLong();
    emit previewHovered(id, name, size, hoverGlobalPos_);
}

void FileBrowser::onContextMenuRequested(const QPoint &pos) {
    const QModelIndex index = view_->indexAt(pos);
    if (!index.isValid()) return;
    view_->setCurrentIndex(index);

    const int row = index.row();
    const qint64 id = model_->data(model_->index(row, 5)).toLongLong();
    const QString name = model_->data(model_->index(row, 0)).toString();
    const bool directory = model_->data(model_->index(row, 6)).toString() == QStringLiteral("DIR");

    QMenu menu(this);
    menu.addAction(directory ? QStringLiteral("下载文件夹") : QStringLiteral("下载"),
                   [this, id, name, directory]() { emit downloadNode(id, name, directory); });
    if (!directory) {
        menu.addAction(QStringLiteral("创建提取码"),
                       [this, id]() { emit createShareCode(id); });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("重命名"), [this, id]() { emit renameNode(id); });
    menu.addAction(QStringLiteral("删除"), [this, id]() { emit deleteNode(id); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("刷新"), [this]() { emit refreshRequested(); });
    menu.exec(view_->viewport()->mapToGlobal(pos));
}
