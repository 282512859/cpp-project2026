// 负责人：成员2：服务端存储（扩展功能模块）
#include "client_qt/FeatureHub.h"

#include "client_qt/FileBrowser.h"
#include "cloud/client/ExtendedClient.h"
#include "cloud/common/JsonLite.h"

#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QThread>
#include <QToolBar>

#include <algorithm>

namespace {

using cloud::client::ExtendedRow;

/// 把 QVariantMap 中的值编码成 JSON 值文本（数字/布尔裸写，字符串加引号转义）。
QString jsonValue(const QVariant &value) {
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QString::number(value.toLongLong());
    default:
        return QString::fromStdString(
            cloud::common::json::quote(value.toString().toUtf8().toStdString()));
    }
}

/// 扩展行 → 界面行。detail 拼进名称列，因为 FileBrowser 的列定义是固定的。
QVariantMap toBrowserRow(const QVariantMap &row) {
    QVariantMap entry;
    const auto detail = row.value("detail").toString();
    const auto name = row.value("name").toString();
    entry["id"] = row.value("id").toLongLong();
    entry["name"] = detail.isEmpty() ? name
                                     : QStringLiteral("%1  —  %2").arg(name, detail);
    entry["directory"] = row.value("directory").toBool();
    entry["size"] = row.value("size").toLongLong();
    entry["modifiedAt"] = row.value("modified").toLongLong();
    return entry;
}

QString formatTime(qint64 millis) {
    if (millis <= 0) return QStringLiteral("—");
    return QDateTime::fromMSecsSinceEpoch(millis).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace

// ---------------------------------------------------------------- FeatureWorker

FeatureWorker::FeatureWorker(QObject *parent) : QObject(parent) {}

FeatureWorker::~FeatureWorker() = default;

bool FeatureWorker::ensureClient() {
    if (client_) return true;
    if (username_.isEmpty()) return false;
    try {
        client_ = std::make_unique<cloud::client::ExtendedClientCore>(
            host_.toStdString(), port_);
        client_->login(username_.toStdString(), password_.toStdString());
        return true;
    } catch (...) {
        client_.reset();
        throw;
    }
}

void FeatureWorker::login(const QString &host, quint16 port, const QString &username,
                          const QString &password, quint64 tag) {
    host_ = host;
    port_ = port;
    username_ = username;
    password_ = password;
    try {
        client_.reset();
        ensureClient();
        emit loggedIn(tag);
    } catch (const cloud::client::ClientError &error) {
        emit failed(QStringLiteral("login"), tag, QString::fromStdString(error.code()),
                    QString::fromLocal8Bit(error.what()));
    } catch (const std::exception &error) {
        emit failed(QStringLiteral("login"), tag, QStringLiteral("NO_CONNECTION"),
                    QString::fromLocal8Bit(error.what()));
    }
}

void FeatureWorker::reset() {
    if (client_) {
        try {
            client_->logout();
        } catch (...) {
            // 注销失败也要保证本地状态被清理。
        }
    }
    client_.reset();
    password_.clear();
}

void FeatureWorker::perform(const QString &operation, const QVariantMap &args, quint64 tag) {
    try {
        if (!ensureClient()) {
            emit failed(operation, tag, QStringLiteral("NO_CONNECTION"),
                        QStringLiteral("尚未登录，请先登录后重试。"));
            return;
        }
        cloud::client::ExtendedClientCore::Args encoded;
        for (auto it = args.constBegin(); it != args.constEnd(); ++it)
            encoded.emplace_back(it.key().toStdString(),
                                 jsonValue(it.value()).toStdString());

        const auto result = client_->dispatch(operation.toStdString(), encoded);
        if (result.rows.empty() && !result.fields.empty() &&
            result.fields.find("entries") == result.fields.end()) {
            QVariantMap fields;
            for (const auto &[key, value] : result.fields)
                fields.insert(QString::fromStdString(key), QString::fromStdString(value));
            emit completed(operation, tag, fields);
            return;
        }

        QVariantList rows;
        for (const auto &row : result.rows) {
            QVariantMap item;
            item["kind"] = QString::fromStdString(row.kind);
            item["id"] = QVariant::fromValue<qint64>(row.id);
            item["name"] = QString::fromStdString(row.name);
            item["owner"] = QString::fromStdString(row.owner);
            item["detail"] = QString::fromStdString(row.detail);
            item["size"] = QVariant::fromValue<qint64>(row.size);
            item["modified"] = QVariant::fromValue<qint64>(row.modified);
            item["directory"] = row.directory;
            item["token"] = QString::fromStdString(row.token);
            item["extra"] = QString::fromStdString(row.extra);
            rows.append(item);
        }
        if (rows.isEmpty() && !result.fields.empty()) {
            QVariantMap fields;
            for (const auto &[key, value] : result.fields)
                fields.insert(QString::fromStdString(key), QString::fromStdString(value));
            emit completed(operation, tag, fields);
            return;
        }
        emit listed(operation, tag, rows);
    } catch (const cloud::client::ClientError &error) {
        emit failed(operation, tag, QString::fromStdString(error.code()),
                    QString::fromLocal8Bit(error.what()));
    } catch (const std::exception &error) {
        emit failed(operation, tag, QStringLiteral("EXCEPTION"),
                    QString::fromLocal8Bit(error.what()));
    }
}

// ---------------------------------------------------------------- FeatureHub

FeatureHub::FeatureHub(QWidget *window, FileBrowser *browser, QToolBar *viewToolbar,
                       QLabel *locationLabel, QLabel *itemCountLabel, QLabel *quotaLabel,
                       QObject *parent)
    : QObject(parent), window_(window), browser_(browser), viewToolbar_(viewToolbar),
      locationLabel_(locationLabel), itemCountLabel_(itemCountLabel), quotaLabel_(quotaLabel) {
    workerThread_ = new QThread(this);
    worker_ = new FeatureWorker();
    worker_->moveToThread(workerThread_);
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &FeatureWorker::loggedIn, this, &FeatureHub::onWorkerLoggedIn,
            Qt::QueuedConnection);
    connect(worker_, &FeatureWorker::failed, this, &FeatureHub::onWorkerFailed,
            Qt::QueuedConnection);
    connect(worker_, &FeatureWorker::listed, this, &FeatureHub::onWorkerListed,
            Qt::QueuedConnection);
    connect(worker_, &FeatureWorker::completed, this, &FeatureHub::onWorkerCompleted,
            Qt::QueuedConnection);

    connect(browser_, &FileBrowser::enterDirectory, this,
            &FeatureHub::onBrowserEnterDirectory);
    currentView_ = QStringLiteral("files");
    workerThread_->start();
}

FeatureHub::~FeatureHub() {
    workerThread_->quit();
    workerThread_->wait();
}

void FeatureHub::attachSidebar(const QVector<QPushButton *> &buttons,
                               const QStringList &viewIds) {
    sidebarButtons_ = buttons;
    sidebarViewIds_ = viewIds;
    for (auto *button : sidebarButtons_) {
        connect(button, &QPushButton::clicked, this, &FeatureHub::onSidebarClicked);
    }
}

void FeatureHub::attachStandardToolbar(QToolBar *toolbar) {
    standardToolbar_ = toolbar;
}

QVector<FeatureHub::ViewSpec> FeatureHub::viewSpecs() {
    return {
        // 文档库沿用原有文件树；个人文档展示“全部文件”平铺视图。
        {QStringLiteral("files"), QStringLiteral("文档库"), QString(), {}},
        {QStringLiteral("personal"), QStringLiteral("个人文档"),
         QStringLiteral("library.list"), {}},
        {QStringLiteral("shared"), QStringLiteral("共享文档"),
         QStringLiteral("share.list"), {}},
        {QStringLiteral("group"), QStringLiteral("群组文档"),
         QStringLiteral("group.list"), {}},
        {QStringLiteral("library"), QStringLiteral("文档库"),
         QStringLiteral("library.list"), {}},
        {QStringLiteral("archive"), QStringLiteral("归档库"),
         QStringLiteral("archive.list"), {}},
        {QStringLiteral("transfers"), QStringLiteral("收发任务"),
         QStringLiteral("transfer.list"), {}},
        {QStringLiteral("trash"), QStringLiteral("回收站"),
         QStringLiteral("trash.list"), {}},
        {QStringLiteral("quarantine"), QStringLiteral("隔离区"),
         QStringLiteral("quarantine.list"), {}},
        {QStringLiteral("permission"), QStringLiteral("权限共享"),
         QStringLiteral("permission.list"), {}},
        {QStringLiteral("link"), QStringLiteral("外链共享"),
         QStringLiteral("link.list"), {}},
        {QStringLiteral("discover"), QStringLiteral("发现共享"),
         QStringLiteral("discover.list"), {}},
        {QStringLiteral("blocked"), QStringLiteral("已屏蔽共享"),
         QStringLiteral("block.list"), {}},
        {QStringLiteral("request"), QStringLiteral("权限申请"),
         QStringLiteral("request.list"), {}},
        {QStringLiteral("review"), QStringLiteral("权限审核"),
         QStringLiteral("review.list"), {}},
        {QStringLiteral("workflow"), QStringLiteral("流程申请"),
         QStringLiteral("workflow.list"), {}},
    };
}

const FeatureHub::ViewSpec *FeatureHub::findSpec(const QString &viewId) const {
    static const auto specs = viewSpecs();
    for (const auto &spec : specs) {
        if (spec.id == viewId) return &spec;
    }
    return nullptr;
}

bool FeatureHub::isExtendedView() const noexcept {
    return !currentView_.isEmpty() && currentView_ != QStringLiteral("files");
}

bool FeatureHub::usesStandardFileActions() const noexcept {
    return currentView_ == QStringLiteral("files") ||
           currentView_ == QStringLiteral("personal");
}

void FeatureHub::setSession(const QString &host, quint16 port, const QString &user,
                            const QString &password) {
    sessionReady_ = false;
    const auto tag = nextTag_++;
    QMetaObject::invokeMethod(
        worker_, "login", Qt::QueuedConnection, Q_ARG(QString, host), Q_ARG(quint16, port),
        Q_ARG(QString, user), Q_ARG(QString, password), Q_ARG(quint64, tag));
}

void FeatureHub::clearSession() {
    sessionReady_ = false;
    currentView_.clear();
    groupTitle_.clear();
    currentGroupId_ = 0;
    rows_.clear();
    rowsById_.clear();
    viewToolbar_->clear();
    // reset 必须在工作线程执行：它要关闭那条属于 worker 的连接。
    QMetaObject::invokeMethod(worker_, "reset", Qt::QueuedConnection);
    if (quotaLabel_) quotaLabel_->setText(QStringLiteral("存储空间\n默认配额 10 GB"));
}

void FeatureHub::onWorkerLoggedIn(quint64 tag) {
    Q_UNUSED(tag);
    sessionReady_ = true;
    emit statusMessage(QStringLiteral("扩展功能已连接（回收站、共享、权限、群组等）"));
    refreshQuota();
    reloadLibraryCache();
    refresh();
}

void FeatureHub::refreshQuota() {
    const auto tag = nextTag_++;
    QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("quota")),
                              Q_ARG(QVariantMap, QVariantMap()),
                              Q_ARG(quint64, tag));
}

void FeatureHub::reloadLibraryCache() {
    const auto tag = nextTag_++;
    libraryCacheTag_ = tag;
    QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("library.list")),
                              Q_ARG(QVariantMap, QVariantMap()), Q_ARG(quint64, tag));
}

void FeatureHub::onWorkerFailed(const QString &operation, quint64 tag, const QString &code,
                                const QString &message) {
    if (tag == libraryCacheTag_) return;
    if (tag == quotaTag_) return;
    busy_ = false;
    pendingAction_.clear();
    rebuildToolbar();
    const auto text = QStringLiteral("[%1] %2").arg(code, message);
    emit errorMessage(text);
    if (operation == QStringLiteral("login")) {
        sessionReady_ = false;
        if (itemCountLabel_)
            itemCountLabel_->setText(QStringLiteral("扩展功能未连接：%1").arg(message));
    }
}

void FeatureHub::onWorkerListed(const QString &operation, quint64 tag,
                                const QVariantList &rows) {
    if (tag == libraryCacheTag_) {
        libraryCache_ = rows;
        return;
    }
    if (tag == quotaTag_) return;
    if (tag == pendingPickTag_) {
        // 选择文件类动作：把结果交给对话框，而不是渲染到列表里。
        pendingPickTag_ = 0;
        busy_ = false;
        rebuildToolbar();
        openRequestPicker(rows);
        return;
    }
    if (tag == pendingLoadTag_) {
        busy_ = false;
        renderRows(rows);
        return;
    }
    // 动作完成后的回读：只刷新列表。
    busy_ = false;
    pendingAction_.clear();
    if (tag == pendingActionTag_) {
        renderRows(rows);
    } else {
        Q_UNUSED(operation);
        refresh();
    }
    rebuildToolbar();
}

void FeatureHub::onWorkerCompleted(const QString &operation, quint64 tag,
                                   const QVariantMap &fields) {
    if (tag == quotaTag_) {
        const auto quota = fields.value("quota").toLongLong();
        const auto used = fields.value("used").toLongLong();
        if (quotaLabel_ && quota > 0) {
            quotaLabel_->setText(QStringLiteral("存储空间\n已用 %1 / %2")
                                     .arg(formatBytes(used), formatBytes(quota)));
        }
        return;
    }
    if (tag == libraryCacheTag_) return;

    busy_ = false;
    if (!pendingSuccess_.isEmpty()) {
        emit statusMessage(pendingSuccess_);
        pendingSuccess_.clear();
    }
    pendingAction_.clear();
    rebuildToolbar();

    const auto finished = operation;
    if (finished == QStringLiteral("quota")) {
        return;
    }
    refresh();
    reloadLibraryCache();
}

void FeatureHub::onSidebarClicked() {
    auto *button = qobject_cast<QPushButton *>(sender());
    if (!button) return;
    const auto index = sidebarButtons_.indexOf(button);
    if (index < 0 || index >= sidebarViewIds_.size()) return;
    activateView(sidebarViewIds_.at(index));
}

void FeatureHub::activateView(const QString &viewId) {
    const auto *spec = findSpec(viewId);
    if (!spec) return;

    currentView_ = viewId;
    groupTitle_.clear();
    currentGroupId_ = 0;
    for (int i = 0; i < sidebarButtons_.size(); ++i) {
        const bool active = sidebarViewIds_.value(i) == viewId;
        sidebarButtons_.at(i)->setObjectName(active ? QStringLiteral("navCurrent")
                                                    : QStringLiteral("navButton"));
        sidebarButtons_.at(i)->style()->unpolish(sidebarButtons_.at(i));
        sidebarButtons_.at(i)->style()->polish(sidebarButtons_.at(i));
    }

    if (viewId == QStringLiteral("files")) {
        // 文档库沿用原有文件树，交给主窗口加载根目录。
        rows_.clear();
        rowsById_.clear();
        viewToolbar_->clear();
        rebuildToolbar();
        emit standardViewRequested();
        return;
    }
    loadView(viewId);
}

void FeatureHub::loadView(const QString &viewId) {
    const auto *spec = findSpec(viewId);
    if (!spec || spec->operation.isEmpty()) return;
    if (locationLabel_) locationLabel_->setText(spec->title);
    setBusy(true, QStringLiteral("正在加载 %1…").arg(spec->title));
    pendingLoadTag_ = nextTag_++;
    QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                              Q_ARG(QString, spec->operation), Q_ARG(QVariantMap, spec->args),
                              Q_ARG(quint64, pendingLoadTag_));
}

void FeatureHub::refresh() {
    if (!sessionReady_ || currentView_.isEmpty()) return;
    if (currentView_ == QStringLiteral("files")) return;
    if (currentView_ == QStringLiteral("permission-node") && pendingPermissionNode_ > 0) {
        // 权限配置视图：重新读取该节点的授权列表。
        QVariantMap args;
        args["nodeId"] = pendingPermissionNode_;
        setBusy(true, QStringLiteral("正在读取权限…"));
        pendingLoadTag_ = nextTag_++;
        QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("permission.node")),
                                  Q_ARG(QVariantMap, args), Q_ARG(quint64, pendingLoadTag_));
        return;
    }
    if (!groupTitle_.isEmpty()) {
        QVariantMap args;
        args["groupId"] = currentGroupId_;
        if (locationLabel_) locationLabel_->setText(QStringLiteral("群组文档 / %1").arg(groupTitle_));
        setBusy(true, QStringLiteral("正在加载群组 %1…").arg(groupTitle_));
        pendingLoadTag_ = nextTag_++;
        QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("group.items")),
                                  Q_ARG(QVariantMap, args), Q_ARG(quint64, pendingLoadTag_));
        return;
    }
    loadView(currentView_);
}

void FeatureHub::setBusy(bool busy, const QString &text) {
    busy_ = busy;
    if (busy) {
        if (itemCountLabel_) itemCountLabel_->setText(text);
    }
    viewToolbar_->setEnabled(!busy);
}

void FeatureHub::renderRows(const QVariantList &rows) {
    rows_ = rows;
    rowsById_.clear();
    QVariantList display;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        rowsById_.insert(row.value("id").toLongLong(), row);
        display.append(toBrowserRow(row));
    }
    browser_->setEntries(display);
    if (itemCountLabel_) {
        itemCountLabel_->setText(QStringLiteral("%1 项").arg(display.size()));
    }
    rebuildToolbar();
}

QVariantMap FeatureHub::selectedRow() const {
    qint64 id = 0;
    QString name;
    bool directory = false;
    if (!browser_->selectedNode(id, name, directory)) return {};
    return rowsById_.value(id);
}

qint64 FeatureHub::rowId(const QVariantMap &row) {
    return row.value("id").toLongLong();
}

QString FeatureHub::rowText(const QVariantMap &row, const QString &key) {
    return row.value(key).toString();
}

QString FeatureHub::formatBytes(qint64 bytes) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units[unit]);
}

void FeatureHub::onBrowserEnterDirectory(qint64 nodeId, const QString &name) {
    if (currentView_ == QStringLiteral("group")) {
        openGroup(nodeId, name);
    }
}

void FeatureHub::openGroup(qint64 groupId, const QString &name) {
    currentGroupId_ = groupId;
    groupTitle_ = name;
    refresh();
}

bool FeatureHub::moveNodeToTrash(qint64 nodeId) {
    if (!sessionReady_ || nodeId <= 0) return false;
    QVariantMap args;
    args["nodeId"] = nodeId;
    runAction(QStringLiteral("trash.add"), args, QStringLiteral("正在移入回收站…"),
              QStringLiteral("已移入回收站，可在左侧“回收站”中恢复。"));
    return true;
}

void FeatureHub::openPermissionDialog() {
    const auto row = selectedRow();
    if (row.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("权限配置"),
                                 QStringLiteral("请先选择一个文件或文件夹。"));
        return;
    }
    const auto nodeId = rowId(row);
    QVariantMap args;
    args["nodeId"] = nodeId;
    const auto tag = nextTag_++;
    pendingPermissionNode_ = nodeId;
    pendingLoadTag_ = tag;
    setBusy(true, QStringLiteral("正在读取权限…"));

    // 权限列表渲染到浏览器后，用户可继续用工具栏完成授权/取消授权。
    const auto previousView = currentView_;
    const auto previousGroup = groupTitle_;
    QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("permission.node")),
                              Q_ARG(QVariantMap, args), Q_ARG(quint64, tag));
    permissionDialogView_ = previousView;
    permissionDialogGroup_ = previousGroup;
    currentView_ = QStringLiteral("permission-node");
    if (locationLabel_)
        locationLabel_->setText(QStringLiteral("权限配置 / %1").arg(rowText(row, "name")));
}

void FeatureHub::rebuildToolbar() {
    viewToolbar_->clear();
    // 扩展视图只显示自己的动作；回到标准文件视图时恢复原有工具栏。
    if (standardToolbar_) standardToolbar_->setVisible(!isExtendedView());
    viewToolbar_->setVisible(isExtendedView());
    if (!sessionReady_ || currentView_.isEmpty()) {
        viewToolbar_->setVisible(false);
        return;
    }

    const auto add = [&](const QString &text, auto slot) {
        auto *action = viewToolbar_->addAction(text);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    const auto view = currentView_;
    if (view == QStringLiteral("trash")) {
        add(QStringLiteral("恢复"), [this]() { actionRestoreFromTrash(); });
        add(QStringLiteral("彻底删除"), [this]() { actionPurgeFromTrash(); });
        add(QStringLiteral("清空回收站"), [this]() { actionEmptyTrash(); });
    } else if (view == QStringLiteral("archive")) {
        add(QStringLiteral("取消归档"), [this]() { actionRemoveArchive(); });
        add(QStringLiteral("移入回收站"), [this]() { actionMoveToTrash(); });
    } else if (view == QStringLiteral("quarantine")) {
        add(QStringLiteral("扫描可疑文件"), [this]() { actionScanQuarantine(); });
        add(QStringLiteral("解除隔离"), [this]() { actionReleaseQuarantine(); });
        add(QStringLiteral("彻底删除"), [this]() { actionPurgeQuarantine(); });
    } else if (view == QStringLiteral("library")) {
        add(QStringLiteral("移入回收站"), [this]() { actionMoveToTrash(); });
        add(QStringLiteral("加入归档"), [this]() { actionAddArchive(); });
        add(QStringLiteral("创建外链"), [this]() { actionCreateLink(); });
        add(QStringLiteral("权限配置"), [this]() { openPermissionDialog(); });
    } else if (view == QStringLiteral("shared")) {
        add(QStringLiteral("领取到我的文档"), [this]() { actionClaimShare(); });
    } else if (view == QStringLiteral("permission")) {
        add(QStringLiteral("新增授权"), [this]() { actionGrantPermission(); });
        add(QStringLiteral("取消授权"), [this]() { actionRevokePermission(); });
        add(QStringLiteral("权限配置"), [this]() { openPermissionDialog(); });
    } else if (view == QStringLiteral("permission-node")) {
        add(QStringLiteral("授权给用户"), [this]() { actionGrantPermission(); });
        add(QStringLiteral("取消授权"), [this]() { actionRevokePermission(); });
        add(QStringLiteral("返回"), [this]() { actionBackToGroups(); });
    } else if (view == QStringLiteral("link")) {
        add(QStringLiteral("创建外链"), [this]() { actionCreateLink(); });
        add(QStringLiteral("复制外链码"), [this]() { actionCopyLink(); });
        add(QStringLiteral("撤销"), [this]() { actionRevokeLink(); });
    } else if (view == QStringLiteral("discover")) {
        add(QStringLiteral("领取"), [this]() { actionOpenLink(); });
        add(QStringLiteral("屏蔽该分享者"), [this]() { actionBlockSharer(); });
    } else if (view == QStringLiteral("blocked")) {
        add(QStringLiteral("解除屏蔽"), [this]() { actionUnblock(); });
    } else if (view == QStringLiteral("request")) {
        add(QStringLiteral("新建权限申请"), [this]() { actionCreateRequest(); });
    } else if (view == QStringLiteral("review")) {
        add(QStringLiteral("批准"), [this]() { actionDecideReview(true); });
        add(QStringLiteral("驳回"), [this]() { actionDecideReview(false); });
    } else if (view == QStringLiteral("workflow")) {
        add(QStringLiteral("提交流程申请"), [this]() { actionCreateWorkflow(); });
    } else if (view == QStringLiteral("group")) {
        if (groupTitle_.isEmpty()) {
            add(QStringLiteral("新建群组"), [this]() { actionCreateGroup(); });
            add(QStringLiteral("打开群组"), [this]() {
                const auto row = selectedRow();
                if (!row.isEmpty())
                    openGroup(rowId(row), rowText(row, "name"));
            });
        } else {
            add(QStringLiteral("添加我的文件"), [this]() { actionAddGroupItem(); });
            add(QStringLiteral("领取到我的文档"), [this]() { actionClaimGroupItem(); });
            add(QStringLiteral("移出群组"), [this]() { actionRemoveGroupItem(); });
            add(QStringLiteral("邀请成员"), [this]() { actionInviteMember(); });
            add(QStringLiteral("返回群组列表"), [this]() { actionBackToGroups(); });
        }
    }

    // 所有扩展视图都提供统一的刷新入口。
    add(QStringLiteral("刷新"), [this]() { refresh(); });
    viewToolbar_->setVisible(true);
    viewToolbar_->setEnabled(!busy_);
}

bool FeatureHub::dispatch(const QString &operation, const QVariantMap &args,
                          const QString &busyText) {
    if (!sessionReady_) {
        emit errorMessage(QStringLiteral("扩展功能尚未连接，请重新登录后再试。"));
        return false;
    }
    pendingAction_ = operation;
    pendingActionTag_ = nextTag_++;
    setBusy(true, busyText);
    QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                              Q_ARG(QString, operation), Q_ARG(QVariantMap, args),
                              Q_ARG(quint64, pendingActionTag_));
    return true;
}

void FeatureHub::runAction(const QString &operation, const QVariantMap &args,
                           const QString &busyText, const QString &successText) {
    pendingSuccess_ = successText;
    dispatch(operation, args, busyText);
}

// ---------------------------------------------------------------- 视图动作

void FeatureHub::actionMoveToTrash() {
    const auto row = selectedRow();
    if (row.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("移入回收站"),
                                 QStringLiteral("请先选择一个文件或文件夹。"));
        return;
    }
    moveNodeToTrash(rowId(row));
}

void FeatureHub::actionRestoreFromTrash() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("trash.restore"), args, QStringLiteral("正在恢复…"),
              QStringLiteral("已恢复到原目录。"));
}

void FeatureHub::actionPurgeFromTrash() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    if (QMessageBox::question(window_, QStringLiteral("彻底删除"),
                              QStringLiteral("删除后无法恢复，确定继续？"))
        != QMessageBox::Yes)
        return;
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("trash.purge"), args, QStringLiteral("正在彻底删除…"),
              QStringLiteral("已彻底删除。"));
}

void FeatureHub::actionEmptyTrash() {
    if (QMessageBox::question(window_, QStringLiteral("清空回收站"),
                              QStringLiteral("将彻底删除回收站中的全部内容，确定继续？"))
        != QMessageBox::Yes)
        return;
    runAction(QStringLiteral("trash.empty"), {}, QStringLiteral("正在清空回收站…"),
              QStringLiteral("回收站已清空。"));
}

void FeatureHub::actionAddArchive() {
    const auto row = selectedRow();
    if (row.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("加入归档"),
                                 QStringLiteral("请先选择一个文件或文件夹。"));
        return;
    }
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("archive.add"), args, QStringLiteral("正在归档…"),
              QStringLiteral("已加入归档库。"));
}

void FeatureHub::actionRemoveArchive() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("archive.remove"), args, QStringLiteral("正在取消归档…"),
              QStringLiteral("已取消归档。"));
}

void FeatureHub::actionScanQuarantine() {
    runAction(QStringLiteral("quarantine.scan"), {}, QStringLiteral("正在扫描可疑文件…"),
              QStringLiteral("扫描完成。"));
}

void FeatureHub::actionReleaseQuarantine() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("quarantine.release"), args, QStringLiteral("正在解除隔离…"),
              QStringLiteral("已解除隔离并回到原目录。"));
}

void FeatureHub::actionPurgeQuarantine() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    if (QMessageBox::question(window_, QStringLiteral("删除隔离文件"),
                              QStringLiteral("删除后无法恢复，确定继续？"))
        != QMessageBox::Yes)
        return;
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("quarantine.purge"), args, QStringLiteral("正在删除…"),
              QStringLiteral("已删除隔离文件。"));
}

void FeatureHub::actionClaimShare() {
    const auto row = selectedRow();
    if (row.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("领取共享文件"),
                                 QStringLiteral("请先选择一条共享记录。"));
        return;
    }
    QVariantMap args;
    args["nodeId"] = rowId(row);
    runAction(QStringLiteral("share.claim"), args, QStringLiteral("正在领取…"),
              QStringLiteral("已领取到你的文档。"));
}

void FeatureHub::actionGrantPermission() {
    qint64 nodeId = pendingPermissionNode_;
    if (nodeId <= 0) {
        const auto row = selectedRow();
        if (row.isEmpty()) {
            QMessageBox::information(window_, QStringLiteral("授权"),
                                     QStringLiteral("请先选择要授权的文件或文件夹。"));
            return;
        }
        nodeId = rowId(row);
    }
    bool ok = false;
    const auto user = QInputDialog::getText(window_, QStringLiteral("授权给用户"),
                                            QStringLiteral("对方用户名："),
                                            QLineEdit::Normal, {}, &ok)
                          .trimmed();
    if (!ok || user.isEmpty()) return;
    const auto write = QMessageBox::question(window_, QStringLiteral("授权范围"),
                                             QStringLiteral("允许对方读写吗？\n是=读写，否=只读"))
                           == QMessageBox::Yes;
    QVariantMap args;
    args["nodeId"] = nodeId;
    args["grantee"] = user;
    args["canWrite"] = write;
    runAction(QStringLiteral("permission.grant"), args, QStringLiteral("正在授权…"),
              QStringLiteral("已授权给 %1。").arg(user));
}

void FeatureHub::actionRevokePermission() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    const auto owner = rowText(row, "owner");
    if (owner.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("取消授权"),
                                 QStringLiteral("请选择一条带用户信息的授权记录。"));
        return;
    }
    QVariantMap args;
    args["nodeId"] = rowId(row);
    args["grantee"] = owner;
    runAction(QStringLiteral("permission.revoke"), args, QStringLiteral("正在取消授权…"),
              QStringLiteral("已取消 %1 的授权。").arg(owner));
}

void FeatureHub::actionCreateLink() {
    const auto nodeId = pickMyFile(QStringLiteral("选择要创建外链的文件"), false);
    if (nodeId <= 0) return;

    QDialog dialog(window_);
    dialog.setWindowTitle(QStringLiteral("创建外链"));
    auto *form = new QFormLayout(&dialog);
    auto *hours = new QSpinBox(&dialog);
    hours->setRange(1, 24 * 30);
    hours->setValue(24);
    hours->setSuffix(QStringLiteral(" 小时"));
    auto *password = new QLineEdit(&dialog);
    password->setPlaceholderText(QStringLiteral("留空表示无需密码"));
    auto *discoverable = new QCheckBox(QStringLiteral("发布到“发现共享”"), &dialog);
    form->addRow(QStringLiteral("有效期"), hours);
    form->addRow(QStringLiteral("访问密码"), password);
    form->addRow(QString(), discoverable);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QVariantMap args;
    args["nodeId"] = nodeId;
    args["hours"] = hours->value();
    args["password"] = password->text();
    args["discoverable"] = discoverable->isChecked();
    runAction(QStringLiteral("link.create"), args, QStringLiteral("正在创建外链…"),
              QStringLiteral("外链已创建，可在“外链共享”中查看分享码。"));
}

void FeatureHub::actionCopyLink() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    const auto token = rowText(row, "token");
    if (token.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("复制外链码"),
                                 QStringLiteral("请选择一条外链记录。"));
        return;
    }
    QApplication::clipboard()->setText(token.toUpper());
    emit statusMessage(QStringLiteral("外链码 %1 已复制到剪贴板。").arg(token.toUpper()));
}

void FeatureHub::actionRevokeLink() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    const auto token = rowText(row, "token");
    if (token.isEmpty()) return;
    QVariantMap args;
    args["shareToken"] = token;
    runAction(QStringLiteral("link.revoke"), args, QStringLiteral("正在撤销外链…"),
              QStringLiteral("外链已撤销。"));
}

void FeatureHub::actionOpenLink() {
    const auto row = selectedRow();
    if (row.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("领取共享文件"),
                                 QStringLiteral("请先选择一条发现共享记录。"));
        return;
    }
    bool ok = false;
    const auto password = QInputDialog::getText(window_, QStringLiteral("领取共享文件"),
                                                QStringLiteral("如需要请输入访问密码："),
                                                QLineEdit::Password, {}, &ok);
    if (!ok) return;
    QVariantMap args;
    args["shareToken"] = rowText(row, "token");
    args["password"] = password;
    runAction(QStringLiteral("link.open"), args, QStringLiteral("正在领取…"),
              QStringLiteral("已领取到你的文档。"));
}

void FeatureHub::actionBlockSharer() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["shareToken"] = rowText(row, "token");
    args["reason"] = QStringLiteral("用户主动屏蔽");
    runAction(QStringLiteral("block.add"), args, QStringLiteral("正在屏蔽…"),
              QStringLiteral("已屏蔽该分享者，其内容不再出现在发现共享中。"));
}

void FeatureHub::actionUnblock() {
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["blockId"] = rowId(row);
    runAction(QStringLiteral("block.remove"), args, QStringLiteral("正在解除屏蔽…"),
              QStringLiteral("已解除屏蔽。"));
}

void FeatureHub::actionCreateRequest() {
    // 申请对象是别人的文件：从“发现共享”里挑选，再由服务端校验所有权。
    pendingPickTag_ = nextTag_++;
    setBusy(true, QStringLiteral("正在获取可申请的文件列表…"));
    QMetaObject::invokeMethod(worker_, "perform", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("discover.list")),
                              Q_ARG(QVariantMap, QVariantMap()),
                              Q_ARG(quint64, pendingPickTag_));
}

/// 从“发现共享”的候选中选择文件并提交权限申请。
void FeatureHub::openRequestPicker(const QVariantList &rows) {
    if (rows.isEmpty()) {
        QMessageBox::information(
            window_, QStringLiteral("权限申请"),
            QStringLiteral("当前没有可申请的共享文件。请让对方先用外链发布到“发现共享”。"));
        return;
    }
    QStringList names;
    QVector<qint64> nodeIds;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        names.append(QStringLiteral("%1（来自 %2）")
                         .arg(row.value("name").toString(), row.value("owner").toString()));
        nodeIds.append(row.value("extra").toString().toLongLong());
    }
    bool ok = false;
    const auto choice = QInputDialog::getItem(window_, QStringLiteral("权限申请"),
                                              QStringLiteral("选择要申请权限的文件："),
                                              names, 0, false, &ok);
    if (!ok) return;
    const auto index = names.indexOf(choice);
    if (index < 0 || nodeIds.at(index) <= 0) return;

    const auto write = QMessageBox::question(window_, QStringLiteral("权限范围"),
                                             QStringLiteral("申请读写权限吗？\n是=读写，否=只读"))
                           == QMessageBox::Yes;
    const auto reason = QInputDialog::getText(window_, QStringLiteral("权限申请"),
                                              QStringLiteral("申请理由（可选）："),
                                              QLineEdit::Normal, {});
    QVariantMap args;
    args["nodeId"] = nodeIds.at(index);
    args["canWrite"] = write;
    args["reason"] = reason;
    runAction(QStringLiteral("request.create"), args, QStringLiteral("正在提交申请…"),
              QStringLiteral("权限申请已提交，等待文件所有者审批。"));
}

void FeatureHub::actionDecideReview(bool approve) {
    const auto row = selectedRow();
    if (row.isEmpty()) {
        QMessageBox::information(window_, QStringLiteral("权限审核"),
                                 QStringLiteral("请先选择一条待审核记录。"));
        return;
    }
    const auto kind = rowText(row, "kind") == QStringLiteral("review-workflow")
                          ? QStringLiteral("workflow")
                          : QStringLiteral("acl");
    QVariantMap args;
    args["kind"] = kind;
    args["reviewId"] = rowId(row);
    args["approve"] = approve;
    runAction(QStringLiteral("review.decide"), args, QStringLiteral("正在提交审核结果…"),
              approve ? QStringLiteral("已批准。") : QStringLiteral("已驳回。"));
}

void FeatureHub::actionCreateWorkflow() {
    bool ok = false;
    const auto title = QInputDialog::getText(window_, QStringLiteral("流程申请"),
                                             QStringLiteral("申请标题："),
                                             QLineEdit::Normal, {}, &ok)
                           .trimmed();
    if (!ok || title.isEmpty()) return;
    const auto kind = QInputDialog::getItem(
        window_, QStringLiteral("流程申请"), QStringLiteral("申请类型："),
        {QStringLiteral("扩容申请"), QStringLiteral("权限变更"), QStringLiteral("数据恢复"),
         QStringLiteral("其它申请")},
        0, false, &ok);
    if (!ok) return;
    const auto detail = QInputDialog::getMultiLineText(window_, QStringLiteral("流程申请"),
                                                       QStringLiteral("申请说明："),
                                                       QString());
    QVariantMap args;
    args["kind"] = kind;
    args["title"] = title;
    args["detail"] = detail;
    runAction(QStringLiteral("workflow.create"), args, QStringLiteral("正在提交申请…"),
              QStringLiteral("流程申请已提交，等待管理员审批。"));
}

void FeatureHub::actionCreateGroup() {
    bool ok = false;
    const auto name = QInputDialog::getText(window_, QStringLiteral("新建群组"),
                                            QStringLiteral("群组名称："),
                                            QLineEdit::Normal, {}, &ok)
                          .trimmed();
    if (!ok || name.isEmpty()) return;
    QVariantMap args;
    args["name"] = name;
    runAction(QStringLiteral("group.create"), args, QStringLiteral("正在创建群组…"),
              QStringLiteral("群组 %1 已创建。").arg(name));
}

void FeatureHub::actionInviteMember() {
    if (currentGroupId_ <= 0) return;
    bool ok = false;
    const auto member = QInputDialog::getText(window_, QStringLiteral("邀请成员"),
                                              QStringLiteral("成员用户名："),
                                              QLineEdit::Normal, {}, &ok)
                            .trimmed();
    if (!ok || member.isEmpty()) return;
    QVariantMap args;
    args["groupId"] = currentGroupId_;
    args["member"] = member;
    runAction(QStringLiteral("group.member.add"), args, QStringLiteral("正在邀请…"),
              QStringLiteral("已邀请 %1 加入群组。").arg(member));
}

void FeatureHub::actionAddGroupItem() {
    if (currentGroupId_ <= 0) return;
    const auto nodeId = pickMyFile(QStringLiteral("选择要加入群组的文件"), true);
    if (nodeId <= 0) return;
    QVariantMap args;
    args["groupId"] = currentGroupId_;
    args["nodeId"] = nodeId;
    runAction(QStringLiteral("group.item.add"), args, QStringLiteral("正在加入群组…"),
              QStringLiteral("已加入群组共享。"));
}

void FeatureHub::actionRemoveGroupItem() {
    if (currentGroupId_ <= 0) return;
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["groupId"] = currentGroupId_;
    args["itemId"] = rowId(row);
    runAction(QStringLiteral("group.item.remove"), args, QStringLiteral("正在移出群组…"),
              QStringLiteral("已移出群组。"));
}

void FeatureHub::actionClaimGroupItem() {
    if (currentGroupId_ <= 0) return;
    const auto row = selectedRow();
    if (row.isEmpty()) return;
    QVariantMap args;
    args["groupId"] = currentGroupId_;
    args["itemId"] = rowId(row);
    runAction(QStringLiteral("group.item.claim"), args, QStringLiteral("正在领取…"),
              QStringLiteral("已领取到你的文档。"));
}

void FeatureHub::actionBackToGroups() {
    if (!permissionDialogView_.isEmpty()) {
        const auto view = permissionDialogView_;
        permissionDialogView_.clear();
        pendingPermissionNode_ = 0;
        activateView(view);
        return;
    }
    groupTitle_.clear();
    currentGroupId_ = 0;
    activateView(QStringLiteral("group"));
}

/// 从“我的文档”列表中选一个文件（优先使用缓存，缓存为空时提示先刷新文档库）。
qint64 FeatureHub::pickMyFile(const QString &title, bool directoriesAllowed) {
    QStringList names;
    QVector<qint64> ids;
    for (const auto &value : libraryCache_) {
        const auto row = value.toMap();
        if (!directoriesAllowed && row.value("directory").toBool()) continue;
        names.append(row.value("name").toString());
        ids.append(rowId(row));
    }
    if (names.isEmpty()) {
        QMessageBox::information(
            window_, title,
            QStringLiteral("“文档库”中还没有可选文件，请先上传文件并刷新文档库。"));
        reloadLibraryCache();
        return 0;
    }
    bool ok = false;
    const auto choice = QInputDialog::getItem(window_, title, QStringLiteral("选择文件："),
                                              names, 0, false, &ok);
    if (!ok) return 0;
    const auto index = names.indexOf(choice);
    return index < 0 ? 0 : ids.at(index);
}
