#pragma once

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QVariantList>
#include <QMenu>

class FileBrowser : public QWidget {
    Q_OBJECT
public:
    explicit FileBrowser(QWidget *parent = nullptr);
    void setEntries(const QVariantList &entries);

signals:
    void enterDirectory(qint64 parentId, const QString &name);
    void downloadNode(qint64 nodeId, const QString &name, bool directory);
    void renameNode(qint64 nodeId);
    void deleteNode(qint64 nodeId);
    void createShareCode(qint64 nodeId);
    void refreshRequested();

private slots:
    void onActivated(const QModelIndex &index);
    void onContextMenuRequested(const QPoint &pos);

private:
    QTreeView *view_;
    QStandardItemModel *model_;
};
