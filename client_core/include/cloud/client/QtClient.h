#pragma once

#include <QObject>
#include <QThread>
#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QVariantList>

#include <memory>
#include <string>
#include <cstdint>

#include "cloud/client/ClientCore.h"

namespace cloud::client {

static inline QVariantMap remoteNodeToMap(const RemoteNode &n) {
    QVariantMap m;
    m["id"] = QVariant::fromValue<qint64>(static_cast<qint64>(n.id));
    m["parentId"] = QVariant::fromValue<qint64>(static_cast<qint64>(n.parentId));
    m["name"] = QString::fromStdString(n.name);
    m["directory"] = QVariant::fromValue<bool>(n.directory);
    m["size"] = QVariant::fromValue<qint64>(static_cast<qint64>(n.size));
    m["modifiedAt"] = QVariant::fromValue<qint64>(static_cast<qint64>(n.modifiedAt));
    return m;
}

class ClientWorker : public QObject {
    Q_OBJECT
public:
    explicit ClientWorker(const std::string &host, std::uint16_t port, QObject *parent = nullptr);
    ~ClientWorker() override;

public slots:
    void doRegister(const QString &username, const QString &password);
    void doLogin(const QString &username, const QString &password);
    void doLogout();
    void doList(qint64 parentId);
    void doMkdir(qint64 parentId, const QString &name);
    void doRename(qint64 nodeId, const QString &name);
    void doDelete(qint64 nodeId);
    void doUpload(const QString &localPath, qint64 parentId, const QString &remoteName);
    void doDownload(qint64 nodeId, const QString &localPath);

signals:
    void error(const QString &code, const QString &message);
    void registerFinished();
    void loginFinished();
    void logoutFinished();
    void listFinished(const QVariantList &entries);
    void mkdirFinished(qint64 nodeId);
    void renameFinished();
    void deleteFinished();
    void uploadProgress(qint64 done, qint64 total);
    void uploadFinished(qint64 nodeId);
    void downloadProgress(qint64 done, qint64 total);
    void downloadFinished();

private:
    std::unique_ptr<ClientCore> core_;
};

class QtClient : public QObject {
    Q_OBJECT
public:
    explicit QtClient(const QString &host, quint16 port, QObject *parent = nullptr);
    ~QtClient() override;

    Q_INVOKABLE void registerUser(const QString &username, const QString &password);
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void list(qint64 parentId = 0);
    Q_INVOKABLE void mkdir(qint64 parentId, const QString &name);
    Q_INVOKABLE void renameNode(qint64 nodeId, const QString &name);
    Q_INVOKABLE void deleteNode(qint64 nodeId);
    Q_INVOKABLE void upload(const QString &localPath, qint64 parentId, const QString &remoteName = QString());
    Q_INVOKABLE void download(qint64 nodeId, const QString &localPath);

signals: // public signals for UI
    void errorOccurred(const QString &code, const QString &message);
    void listReady(const QVariantList &entries);
    void uploadProgress(qint64 done, qint64 total);
    void uploadFinished(qint64 nodeId);
    void downloadProgress(qint64 done, qint64 total);
    void downloadFinished();

private:
    QThread workerThread_;
    ClientWorker *worker_;

    // internal proxy signals connected to worker slots (emitted by this object's methods)
signals:
    void invokeRegister(const QString &username, const QString &password);
    void invokeLogin(const QString &username, const QString &password);
    void invokeLogout();
    void invokeList(qint64 parentId);
    void invokeMkdir(qint64 parentId, const QString &name);
    void invokeRename(qint64 nodeId, const QString &name);
    void invokeDelete(qint64 nodeId);
    void invokeUpload(const QString &localPath, qint64 parentId, const QString &remoteName);
    void invokeDownload(qint64 nodeId, const QString &localPath);
};

} // namespace cloud::client

Q_DECLARE_METATYPE(QVariantList)
Q_DECLARE_METATYPE(QVariantMap)
