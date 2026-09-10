#pragma once

#include <QMenu>
#include <QPersistentModelIndex>
#include <QTimer>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVariantList>
#include <QWidget>

class FileBrowser : public QWidget {
    Q_OBJECT
public:
    explicit FileBrowser(QWidget *parent = nullptr);
    void setEntries(const QVariantList &entries);
    void setCreatorName(const QString &name);
    bool selectedNode(qint64 &nodeId, QString &name, bool &directory) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void enterDirectory(qint64 parentId, const QString &name);
    void downloadNode(qint64 nodeId, const QString &name, bool directory);
    void renameNode(qint64 nodeId);
    void deleteNode(qint64 nodeId);
    void createShareCode(qint64 nodeId);
    void refreshRequested();
    void previewHovered(qint64 nodeId, const QString &name, qint64 size, const QPoint &globalPos);
    void previewHoverEnded();

private slots:
    void onActivated(const QModelIndex &index);
    void onContextMenuRequested(const QPoint &pos);
    void onHoverTimeout();

private:
    QTreeView *view_ = nullptr;
    QStandardItemModel *model_ = nullptr;
    QString creatorName_;
    QTimer *hoverTimer_ = nullptr;
    QPersistentModelIndex hoverIndex_;
    QPoint hoverGlobalPos_;
};
