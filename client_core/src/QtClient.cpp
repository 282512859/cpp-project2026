#include "cloud/client/QtClient.h"
#include <QMetaObject>
#include <QDir>
#include <QStringList>

#include <utility>

namespace cloud::client {

// ---------------------- ClientWorker ----------------------
ClientWorker::ClientWorker(const std::string &host, std::uint16_t port, QObject *parent)
    : QObject(parent) {
    try {
        core_.reset(new ClientCore(host, port));
    } catch (const std::exception &e) {
        // Delay emitting error until a slot is called; store no core_ means all ops will report error
        core_.reset();
    }
}

ClientWorker::~ClientWorker() = default;

void ClientWorker::doRegister(const QString &username, const QString &password) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        core_->registerUser(username.toStdString(), password.toStdString());
        emit registerFinished();
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doLogin(const QString &username, const QString &password) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        core_->login(username.toStdString(), password.toStdString());
        emit loginFinished();
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doLogout() {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        core_->logout();
        emit logoutFinished();
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doList(qint64 parentId) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        const auto nodes = core_->list(static_cast<std::int64_t>(parentId));
        QVariantList out;
        for (const auto &n : nodes) out.append(remoteNodeToMap(n));
        emit listFinished(out);
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doMkdir(qint64 parentId, const QString &name) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        const auto id = core_->mkdir(static_cast<std::int64_t>(parentId), name.toStdString());
        emit mkdirFinished(static_cast<qint64>(id));
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doRename(qint64 nodeId, const QString &name) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        core_->renameNode(static_cast<std::int64_t>(nodeId), name.toStdString());
        emit renameFinished();
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doDelete(qint64 nodeId) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        core_->deleteNode(static_cast<std::int64_t>(nodeId));
        emit deleteFinished();
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doUpload(const QString &localPath, qint64 parentId, const QString &remoteName) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        const auto progressCb = [this](std::int64_t done, std::int64_t total) {
            emit uploadProgress(static_cast<qint64>(done), static_cast<qint64>(total));
        };
        const auto nodeId = core_->upload(std::filesystem::path(localPath.toStdString()),
                                         static_cast<std::int64_t>(parentId),
                                         remoteName.toStdString(),
                                         progressCb);
        emit uploadFinished(static_cast<qint64>(nodeId));
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doDownload(qint64 nodeId, const QString &localPath) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        const auto progressCb = [this](std::int64_t done, std::int64_t total) {
            emit downloadProgress(static_cast<qint64>(done), static_cast<qint64>(total));
        };
        core_->download(static_cast<std::int64_t>(nodeId), std::filesystem::path(localPath.toStdString()), progressCb);
        emit downloadFinished();
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

// ---------------------- QtClient ----------------------
QtClient::QtClient(const QString &host, quint16 port, QObject *parent)
    : QObject(parent), worker_(nullptr) {
    qRegisterMetaType<QVariantList>("QVariantList");
    qRegisterMetaType<QVariantMap>("QVariantMap");

    worker_ = new ClientWorker(host.toStdString(), static_cast<std::uint16_t>(port));
    worker_->moveToThread(&workerThread_);

    // Proxy: connect internal invoke signals to worker slots (queued)
    connect(this, &QtClient::invokeRegister, worker_, &ClientWorker::doRegister, Qt::QueuedConnection);
    connect(this, &QtClient::invokeLogin, worker_, &ClientWorker::doLogin, Qt::QueuedConnection);
    connect(this, &QtClient::invokeLogout, worker_, &ClientWorker::doLogout, Qt::QueuedConnection);
    connect(this, &QtClient::invokeList, worker_, &ClientWorker::doList, Qt::QueuedConnection);
    connect(this, &QtClient::invokeMkdir, worker_, &ClientWorker::doMkdir, Qt::QueuedConnection);
    connect(this, &QtClient::invokeRename, worker_, &ClientWorker::doRename, Qt::QueuedConnection);
    connect(this, &QtClient::invokeDelete, worker_, &ClientWorker::doDelete, Qt::QueuedConnection);
    connect(this, &QtClient::invokeUpload, worker_, &ClientWorker::doUpload, Qt::QueuedConnection);
    connect(this, &QtClient::invokeDownload, worker_, &ClientWorker::doDownload, Qt::QueuedConnection);

    // Worker -> public signals
    connect(worker_, &ClientWorker::error, this, &QtClient::errorOccurred, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::listFinished, this, &QtClient::listReady, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::uploadProgress, this, &QtClient::uploadProgress, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::uploadFinished, this, &QtClient::uploadFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::downloadProgress, this, &QtClient::downloadProgress, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::downloadFinished, this, &QtClient::downloadFinished, Qt::QueuedConnection);

    workerThread_.start();
}

QtClient::~QtClient() {
    // Ask worker thread to finish and delete worker
    workerThread_.quit();
    workerThread_.wait();
    if (worker_) { delete worker_; worker_ = nullptr; }
}

void QtClient::registerUser(const QString &username, const QString &password) {
    emit invokeRegister(username, password);
}

void QtClient::login(const QString &username, const QString &password) {
    emit invokeLogin(username, password);
}

void QtClient::logout() {
    emit invokeLogout();
}

void QtClient::list(qint64 parentId) {
    emit invokeList(parentId);
}

void QtClient::mkdir(qint64 parentId, const QString &name) {
    emit invokeMkdir(parentId, name);
}

void QtClient::renameNode(qint64 nodeId, const QString &name) {
    emit invokeRename(nodeId, name);
}

void QtClient::deleteNode(qint64 nodeId) {
    emit invokeDelete(nodeId);
}

void QtClient::upload(const QString &localPath, qint64 parentId, const QString &remoteName) {
    emit invokeUpload(localPath, parentId, remoteName);
}

void QtClient::download(qint64 nodeId, const QString &localPath) {
    emit invokeDownload(nodeId, localPath);
}

} // namespace cloud::client
