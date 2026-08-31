#pragma once

#include <QWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QMap>
#include <QVBoxLayout>

class TransferManager : public QWidget {
    Q_OBJECT
public:
    explicit TransferManager(QWidget *parent = nullptr);

    // create task entries; returns task id
    int addUploadTask(const QString &localPath, qint64 parentId);
    int addDownloadTask(qint64 nodeId, const QString &localPath);

public slots:
    void updateUploadProgress(qint64 done, qint64 total);
    void updateDownloadProgress(qint64 done, qint64 total);
    void markUploadFinished(qint64 nodeId);
    void markDownloadFinished();

private:
    QListWidget *list_;
    QVBoxLayout *layout_;
    int nextTaskId_ = 1;
    // map taskId -> list row index
    QMap<int, QListWidgetItem*> items_;
};
