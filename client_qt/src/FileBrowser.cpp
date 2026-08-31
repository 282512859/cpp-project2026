#include "client_qt/FileBrowser.h"
#include <QHeaderView>
#include <QStandardItem>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QLocale>
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
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(view_);

    model_->setHorizontalHeaderLabels({"Name", "Size", "Modified", "ID", "Type"});
    view_->setModel(model_);
    view_->setRootIsDecorated(false);
    view_->setAlternatingRowColors(true);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setUniformRowHeights(true);
    view_->setAnimated(true);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    view_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    view_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    view_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    view_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    view_->setColumnHidden(3, true);
    view_->setColumnHidden(4, true);

    connect(view_, &QTreeView::activated, this, &FileBrowser::onActivated);
    connect(view_, &QWidget::customContextMenuRequested, this, &FileBrowser::onContextMenuRequested);
}

void FileBrowser::setEntries(const QVariantList &entries) {
    model_->removeRows(0, model_->rowCount());
    for (const auto &v : entries) {
        auto m = v.toMap();
        const bool directory = m.value("directory").toBool();
        QList<QStandardItem*> row;
        auto nameItem = new QStandardItem(m.value("name").toString());
        nameItem->setIcon(view_->style()->standardIcon(
            directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        nameItem->setEditable(false);
        row.append(nameItem);
        auto sizeItem = new QStandardItem(directory ? QStringLiteral("—")
                                                    : friendlySize(m.value("size").toLongLong()));
        sizeItem->setEditable(false);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.append(sizeItem);
        const auto modified = QDateTime::fromMSecsSinceEpoch(m.value("modifiedAt").toLongLong());
        auto modItem = new QStandardItem(QLocale().toString(modified, QLocale::ShortFormat));
        modItem->setEditable(false);
        row.append(modItem);
        auto idItem = new QStandardItem(QString::number(m.value("id").toLongLong()));
        idItem->setEditable(false);
        row.append(idItem);
        auto typeItem = new QStandardItem(directory ? "DIR" : "FILE");
        typeItem->setEditable(false);
        row.append(typeItem);
        model_->appendRow(row);
    }
}

void FileBrowser::onActivated(const QModelIndex &index) {
    if (!index.isValid()) return;
    // column 3 stores ID
    auto idIndex = model_->index(index.row(), 3);
    bool ok=false;
    qint64 id = model_->data(idIndex).toLongLong();
    auto typeIndex = model_->index(index.row(), 4);
    QString type = model_->data(typeIndex).toString();
    if (type=="DIR") {
        emit enterDirectory(id, model_->data(model_->index(index.row(), 0)).toString());
    }
}

void FileBrowser::onContextMenuRequested(const QPoint &pos) {
    QModelIndex idx = view_->indexAt(pos);
    if (!idx.isValid()) return;
    auto id = model_->data(model_->index(idx.row(), 3)).toLongLong();
    const auto name = model_->data(model_->index(idx.row(), 0)).toString();
    const bool directory = model_->data(model_->index(idx.row(), 4)).toString() == "DIR";
    QMenu menu(this);
    menu.addAction(directory ? "Download folder" : "Download",
                   [this, id, name, directory](){ emit downloadNode(id, name, directory); });
    menu.addAction("Rename", [this, id](){ emit renameNode(id); });
    menu.addAction("Delete", [this, id](){ emit deleteNode(id); });
    menu.addAction("Refresh", [this](){ emit refreshRequested(); });
    menu.exec(view_->viewport()->mapToGlobal(pos));
}
