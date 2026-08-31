#include "client_qt/TransferManager.h"
#include <QListWidgetItem>

TransferManager::TransferManager(QWidget *parent)
    : QWidget(parent), list_(new QListWidget(this)), layout_(new QVBoxLayout(this)) {
    layout_->setContentsMargins(0,0,0,0);
    layout_->addWidget(list_);
    setLayout(layout_);
}

int TransferManager::addUploadTask(const QString &localPath, qint64 parentId) {
    const int id = nextTaskId_++;
    QListWidgetItem *it = new QListWidgetItem(QStringLiteral("UPLOAD %1 -> %2").arg(localPath, QString::number(parentId)), list_);
    list_->addItem(it);
    items_.insert(id, it);
    return id;
}

int TransferManager::addDownloadTask(qint64 nodeId, const QString &localPath) {
    const int id = nextTaskId_++;
    QListWidgetItem *it = new QListWidgetItem(QStringLiteral("DOWNLOAD %1 -> %2").arg(QString::number(nodeId), localPath), list_);
    list_->addItem(it);
    items_.insert(id, it);
    return id;
}

void TransferManager::updateUploadProgress(qint64 done, qint64 total) {
    // naive: update last upload task
    if(items_.isEmpty()) return;
    auto it = items_.last();
    it->setText(QStringLiteral("UPLOAD %1/%2").arg(done).arg(total));
}

void TransferManager::updateDownloadProgress(qint64 done, qint64 total) {
    if(items_.isEmpty()) return;
    auto it = items_.last();
    it->setText(QStringLiteral("DOWNLOAD %1/%2").arg(done).arg(total));
}

void TransferManager::markUploadFinished(qint64 nodeId) {
    if(items_.isEmpty()) return;
    auto it = items_.last();
    it->setText(it->text() + " (Finished)");
}

void TransferManager::markDownloadFinished() {
    if(items_.isEmpty()) return;
    auto it = items_.last();
    it->setText(it->text() + " (Finished)");
}
