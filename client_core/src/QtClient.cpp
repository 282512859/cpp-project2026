#include "cloud/client/QtClient.h"
#include <QMetaObject>
#include <QDir>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <utility>

namespace cloud::client {

namespace {

std::string fileNameUtf8(const std::filesystem::path &path) {
    return QString::fromStdWString(path.filename().wstring()).toUtf8().toStdString();
}

std::filesystem::path safeChildPath(const std::filesystem::path &parent,
                                    const std::string &utf8Name) {
    const auto component = std::filesystem::path(
        QString::fromUtf8(utf8Name).toStdWString());
    if (component.empty() || component.is_absolute() || component.filename() != component) {
        throw std::runtime_error("server returned an unsafe file name");
    }
    return parent / component;
}

} // namespace

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
        const auto nativePath = std::filesystem::path(localPath.toStdWString());
        const auto utf8RemoteName = remoteName.isEmpty()
            ? QString::fromStdWString(nativePath.filename().wstring()).toUtf8().toStdString()
            : remoteName.toUtf8().toStdString();
        const auto progressCb = [this](std::int64_t done, std::int64_t total) {
            emit uploadProgress(static_cast<qint64>(done), static_cast<qint64>(total));
        };
        const auto nodeId = core_->upload(nativePath,
                                         static_cast<std::int64_t>(parentId),
                                         utf8RemoteName,
                                         progressCb);
        emit uploadFinished(static_cast<qint64>(nodeId));
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doCreateShareCode(qint64 nodeId) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        emit shareCodeCreated(QString::fromStdString(core_->createShareCode(nodeId)));
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doClaimShareCode(const QString &code) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        emit shareCodeClaimed(static_cast<qint64>(core_->claimShareCode(
            code.trimmed().toLower().toStdString())));
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doUploadDirectory(const QString &localPath, qint64 parentId) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        const auto root = std::filesystem::path(localPath.toStdWString());
        if (!std::filesystem::is_directory(root)) {
            throw std::runtime_error("selected path is not a directory");
        }

        std::int64_t totalBytes = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file() && !entry.is_symlink()) {
                totalBytes += static_cast<std::int64_t>(entry.file_size());
            }
        }

        std::int64_t completedBytes = 0;
        std::function<std::int64_t(const std::filesystem::path &, std::int64_t)> uploadTree;
        uploadTree = [&](const std::filesystem::path &localDir,
                         std::int64_t remoteParent) -> std::int64_t {
            const auto remoteDir = core_->mkdir(remoteParent, fileNameUtf8(localDir));
            for (const auto &entry : std::filesystem::directory_iterator(localDir)) {
                if (entry.is_symlink()) continue;
                if (entry.is_directory()) {
                    uploadTree(entry.path(), remoteDir);
                } else if (entry.is_regular_file()) {
                    const auto base = completedBytes;
                    const auto fileSize = static_cast<std::int64_t>(entry.file_size());
                    core_->upload(entry.path(), remoteDir, fileNameUtf8(entry.path()),
                                  [this, base, totalBytes](std::int64_t done, std::int64_t) {
                        emit uploadProgress(static_cast<qint64>(base + done),
                                            static_cast<qint64>(totalBytes));
                    });
                    completedBytes += fileSize;
                    emit uploadProgress(static_cast<qint64>(completedBytes),
                                        static_cast<qint64>(totalBytes));
                }
            }
            return remoteDir;
        };

        emit uploadProgress(0, static_cast<qint64>(totalBytes));
        const auto rootId = uploadTree(root, static_cast<std::int64_t>(parentId));
        emit uploadFinished(static_cast<qint64>(rootId));
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
        core_->download(static_cast<std::int64_t>(nodeId),
                        std::filesystem::path(localPath.toStdWString()),
                        progressCb);
        emit downloadFinished();
    } catch (const ClientError &e) {
        emit error(QString::fromStdString(e.code()), QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        emit error("EXCEPTION", QString::fromLocal8Bit(e.what()));
    }
}

void ClientWorker::doDownloadDirectory(qint64 nodeId, const QString &remoteName,
                                       const QString &localParentPath) {
    if (!core_) { emit error("NO_CONNECTION", "ClientCore not initialized"); return; }
    try {
        const auto parent = std::filesystem::path(localParentPath.toStdWString());
        const auto root = safeChildPath(parent, remoteName.toUtf8().toStdString());
        if (std::filesystem::exists(root)) {
            throw std::runtime_error("download target directory already exists");
        }

        std::function<std::int64_t(std::int64_t)> totalForDirectory;
        totalForDirectory = [&](std::int64_t remoteDir) -> std::int64_t {
            std::int64_t total = 0;
            for (const auto &node : core_->list(remoteDir)) {
                total += node.directory ? totalForDirectory(node.id) : node.size;
            }
            return total;
        };
        const auto totalBytes = totalForDirectory(static_cast<std::int64_t>(nodeId));

        std::filesystem::create_directories(root);
        std::int64_t completedBytes = 0;
        std::function<void(std::int64_t, const std::filesystem::path &)> downloadTree;
        downloadTree = [&](std::int64_t remoteDir, const std::filesystem::path &localDir) {
            for (const auto &node : core_->list(remoteDir)) {
                const auto target = safeChildPath(localDir, node.name);
                if (node.directory) {
                    std::filesystem::create_directory(target);
                    downloadTree(node.id, target);
                } else {
                    const auto base = completedBytes;
                    core_->download(node.id, target,
                                    [this, base, totalBytes](std::int64_t done, std::int64_t) {
                        emit downloadProgress(static_cast<qint64>(base + done),
                                              static_cast<qint64>(totalBytes));
                    });
                    completedBytes += node.size;
                    emit downloadProgress(static_cast<qint64>(completedBytes),
                                          static_cast<qint64>(totalBytes));
                }
            }
        };

        emit downloadProgress(0, static_cast<qint64>(totalBytes));
        downloadTree(static_cast<std::int64_t>(nodeId), root);
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
    connect(this, &QtClient::invokeCreateShareCode, worker_, &ClientWorker::doCreateShareCode, Qt::QueuedConnection);
    connect(this, &QtClient::invokeClaimShareCode, worker_, &ClientWorker::doClaimShareCode, Qt::QueuedConnection);
    connect(this, &QtClient::invokeUpload, worker_, &ClientWorker::doUpload, Qt::QueuedConnection);
    connect(this, &QtClient::invokeUploadDirectory, worker_, &ClientWorker::doUploadDirectory,
            Qt::QueuedConnection);
    connect(this, &QtClient::invokeDownload, worker_, &ClientWorker::doDownload, Qt::QueuedConnection);
    connect(this, &QtClient::invokeDownloadDirectory, worker_, &ClientWorker::doDownloadDirectory,
            Qt::QueuedConnection);

    // Worker -> public signals
    connect(worker_, &ClientWorker::error, this, &QtClient::errorOccurred, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::registerFinished, this, &QtClient::registerFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::loginFinished, this, &QtClient::loginFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::logoutFinished, this, &QtClient::logoutFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::listFinished, this, &QtClient::listReady, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::mkdirFinished, this, &QtClient::mkdirFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::renameFinished, this, &QtClient::renameFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::deleteFinished, this, &QtClient::deleteFinished, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::shareCodeCreated, this, &QtClient::shareCodeCreated, Qt::QueuedConnection);
    connect(worker_, &ClientWorker::shareCodeClaimed, this, &QtClient::shareCodeClaimed, Qt::QueuedConnection);
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

void QtClient::createShareCode(qint64 nodeId) {
    emit invokeCreateShareCode(nodeId);
}

void QtClient::claimShareCode(const QString &code) {
    emit invokeClaimShareCode(code);
}

void QtClient::upload(const QString &localPath, qint64 parentId, const QString &remoteName) {
    emit invokeUpload(localPath, parentId, remoteName);
}

void QtClient::uploadDirectory(const QString &localPath, qint64 parentId) {
    emit invokeUploadDirectory(localPath, parentId);
}

void QtClient::download(qint64 nodeId, const QString &localPath) {
    emit invokeDownload(nodeId, localPath);
}

void QtClient::downloadDirectory(qint64 nodeId, const QString &remoteName,
                                 const QString &localParentPath) {
    emit invokeDownloadDirectory(nodeId, remoteName, localParentPath);
}

} // namespace cloud::client
