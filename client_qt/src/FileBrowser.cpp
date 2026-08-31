#include "client_qt/FileBrowser.h"
#include <QHeaderView>
#include <QStandardItem>
#include <QContextMenuEvent>

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
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    view_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    view_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    view_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    view_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    connect(view_, &QTreeView::activated, this, &FileBrowser::onActivated);
    connect(view_, &QWidget::customContextMenuRequested, this, &FileBrowser::onContextMenuRequested);
}

void FileBrowser::setEntries(const QVariantList &entries) {
    model_->removeRows(0, model_->rowCount());
    for (const auto &v : entries) {
        auto m = v.toMap();
        QList<QStandardItem*> row;
        auto nameItem = new QStandardItem(m.value("name").toString());
        nameItem->setEditable(false);
        row.append(nameItem);
        auto sizeItem = new QStandardItem(QString::number(m.value("size").toLongLong()));
        sizeItem->setEditable(false);
        row.append(sizeItem);
        auto modItem = new QStandardItem(QString::number(m.value("modifiedAt").toLongLong()));
        modItem->setEditable(false);
        row.append(modItem);
        auto idItem = new QStandardItem(QString::number(m.value("id").toLongLong()));
        idItem->setEditable(false);
        row.append(idItem);
        auto typeItem = new QStandardItem(m.value("directory").toBool() ? "DIR" : "FILE");
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
        emit enterDirectory(id);
    }
}

void FileBrowser::onContextMenuRequested(const QPoint &pos) {
    QModelIndex idx = view_->indexAt(pos);
    if (!idx.isValid()) return;
    auto id = model_->data(model_->index(idx.row(), 3)).toLongLong();
    QMenu menu(this);
    menu.addAction("Download", [this, id](){ emit downloadNode(id); });
    menu.addAction("Rename", [this, id](){ emit renameNode(id); });
    menu.addAction("Delete", [this, id](){ emit deleteNode(id); });
    menu.addAction("Refresh", [this](){ emit refreshRequested(); });
    menu.exec(view_->viewport()->mapToGlobal(pos));
}
