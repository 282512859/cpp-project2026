// 负责人：成员2：服务端存储（扩展功能模块）
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstdint>
#include <memory>

class FileBrowser;
class QLabel;
class QPushButton;
class QThread;
class QToolBar;
class QWidget;

namespace cloud::client {
class ExtendedClientCore;
}

/**
 * 扩展功能的后台执行器。
 *
 * 与 ClientWorker 的设计一致：它被移动到独立线程，所有网络调用都在那里阻塞执行，
 * 这样即使共享领取、隔离区扫描等操作较慢，界面也不会卡住。
 */
class FeatureWorker : public QObject {
    Q_OBJECT
public:
    explicit FeatureWorker(QObject *parent = nullptr);
    ~FeatureWorker() override;

public slots:
    /// 建立独立连接并登录；成功后发出 loggedIn。
    void login(const QString &host, quint16 port, const QString &username,
               const QString &password, quint64 tag);
    /// 执行一次扩展操作。rows 表示列表响应，fields 表示对象响应。
    void perform(const QString &operation, const QVariantMap &args, quint64 tag);
    /// 清理连接与登录态。
    void reset();

signals:
    void loggedIn(quint64 tag);
    void failed(const QString &operation, quint64 tag, const QString &code,
                const QString &message);
    void listed(const QString &operation, quint64 tag, const QVariantList &rows);
    void completed(const QString &operation, quint64 tag, const QVariantMap &fields);

private:
    bool ensureClient();

    QString host_;
    quint16 port_{};
    QString username_;
    QString password_;
    std::unique_ptr<cloud::client::ExtendedClientCore> client_;
};

/**
 * 界面扩展功能中枢。
 *
 * 职责：
 * 1. 管理左侧栏的全部入口（个人文档、共享文档、群组文档、文档库、归档库、
 *    收发任务、回收站、隔离区、权限共享、外链共享、发现共享、已屏蔽共享、
 *    权限申请、权限审核、流程申请）；
 * 2. 按当前视图重建工具栏动作，并把选中的行交给对应的扩展协议；
 * 3. 负责权限配置、外链创建、申请与审核等对话框。
 *
 * 界面外观保持原样：本类复用既有的 FileBrowser、工具栏与侧栏按钮，
 * 只切换它们显示的数据和可用动作。
 */
class FeatureHub : public QObject {
    Q_OBJECT
public:
    FeatureHub(QWidget *window, FileBrowser *browser, QToolBar *viewToolbar,
               QLabel *locationLabel, QLabel *itemCountLabel, QLabel *quotaLabel,
               QObject *parent = nullptr);
    ~FeatureHub() override;

    /// 绑定侧栏按钮与视图标识（顺序必须一致）。
    void attachSidebar(const QVector<QPushButton *> &buttons, const QStringList &viewIds);
    /// 绑定原有文件工具栏：扩展视图下隐藏它，避免动作作用到错误的列表。
    void attachStandardToolbar(QToolBar *toolbar);

    /// 主客户端登录成功后调用，扩展功能使用独立连接登录同一账号。
    void setSession(const QString &host, quint16 port, const QString &user,
                    const QString &password);
    /// 主客户端注销时调用。
    void clearSession();

    /// 当前是否处于扩展管理视图（此时文件浏览器的右键动作由本类接管）。
    [[nodiscard]] bool isExtendedView() const noexcept;
    /// 当前视图是否可以直接使用原有的文件操作（个人文档、文档库）。
    [[nodiscard]] bool usesStandardFileActions() const noexcept;
    [[nodiscard]] QString currentViewId() const { return currentView_; }

    /// 激活某个视图；viewId 为 "personal" 时交回标准文件树。
    void activateView(const QString &viewId);
    /// 打开某个群组。
    void openGroup(qint64 groupId, const QString &name);
    /// 工具栏“权限配置”：对当前选中的节点打开授权对话框。
    void openPermissionDialog();
    /// 把某个节点移入回收站；返回 false 表示当前视图不支持。
    bool moveNodeToTrash(qint64 nodeId);
    /// 刷新当前视图。
    void refresh();

signals:
    /// 需要回到标准文件树（个人文档视图中刷新根目录）。
    void standardViewRequested();
    void statusMessage(const QString &text);
    void errorMessage(const QString &text);

private slots:
    void onSidebarClicked();
    void onWorkerLoggedIn(quint64 tag);
    void onWorkerFailed(const QString &operation, quint64 tag, const QString &code,
                        const QString &message);
    void onWorkerListed(const QString &operation, quint64 tag, const QVariantList &rows);
    void onWorkerCompleted(const QString &operation, quint64 tag,
                           const QVariantMap &fields);
    void onBrowserEnterDirectory(qint64 nodeId, const QString &name);

private:
    /// 视图定义：标题、列表操作和该视图可用的动作。
    struct ViewSpec {
        QString id;
        QString title;
        QString operation;
        QVariantMap args;
    };

    static QVector<ViewSpec> viewSpecs();
    const ViewSpec *findSpec(const QString &viewId) const;

    void loadView(const QString &viewId);
    void rebuildToolbar();
    void renderRows(const QVariantList &rows);
    void setBusy(bool busy, const QString &text);
    void refreshQuota();
    void reloadLibraryCache();
    void openRequestPicker(const QVariantList &rows);

    bool dispatch(const QString &operation, const QVariantMap &args,
                  const QString &busyText);
    void runAction(const QString &operation, const QVariantMap &args,
                   const QString &busyText, const QString &successText);

    // ---- 各视图动作 ----
    void actionMoveToTrash();
    void actionRestoreFromTrash();
    void actionPurgeFromTrash();
    void actionEmptyTrash();
    void actionAddArchive();
    void actionRemoveArchive();
    void actionScanQuarantine();
    void actionReleaseQuarantine();
    void actionPurgeQuarantine();
    void actionClaimShare();
    void actionGrantPermission();
    void actionRevokePermission();
    void actionCreateLink();
    void actionCopyLink();
    void actionRevokeLink();
    void actionOpenLink();
    void actionBlockSharer();
    void actionUnblock();
    void actionCreateRequest();
    void actionDecideReview(bool approve);
    void actionCreateWorkflow();
    void actionCreateGroup();
    void actionInviteMember();
    void actionAddGroupItem();
    void actionRemoveGroupItem();
    void actionClaimGroupItem();
    void actionBackToGroups();

    /// 弹出“选择我的文件”对话框，返回节点 ID（取消时为 0）。
    qint64 pickMyFile(const QString &title, bool directoriesAllowed);
    /// 返回当前选中行（无选中时返回空表）。
    [[nodiscard]] QVariantMap selectedRow() const;
    [[nodiscard]] static qint64 rowId(const QVariantMap &row);
    [[nodiscard]] static QString rowText(const QVariantMap &row, const QString &key);
    [[nodiscard]] static QString formatBytes(qint64 bytes);

    QWidget *window_{};
    FileBrowser *browser_{};
    QToolBar *viewToolbar_{};
    QToolBar *standardToolbar_{};
    QLabel *locationLabel_{};
    QLabel *itemCountLabel_{};
    QLabel *quotaLabel_{};

    QThread *workerThread_{};
    FeatureWorker *worker_{};

    QVector<QPushButton *> sidebarButtons_;
    QStringList sidebarViewIds_;

    QString currentView_;
    QString groupTitle_;
    qint64 currentGroupId_{};
    QVariantList rows_;
    QHash<qint64, QVariantMap> rowsById_;
    quint64 nextTag_{1};
    quint64 pendingLoadTag_{};
    quint64 pendingActionTag_{};
    quint64 quotaTag_{};
    quint64 libraryCacheTag_{};
    quint64 pendingPickTag_{};
    qint64 pendingPermissionNode_{};
    QString pendingAction_;
    QString pendingSuccess_;
    QString permissionDialogView_;
    QString permissionDialogGroup_;
    QVariantList libraryCache_;
    bool sessionReady_{};
    bool busy_{};
};
