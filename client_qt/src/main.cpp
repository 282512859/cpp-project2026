#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMovie>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "client_qt/FileBrowser.h"
#include "cloud/client/QtClient.h"

using namespace cloud::client;

namespace {

int progressPercent(qint64 done, qint64 total) {
    if (total <= 0) return 0;
    return std::clamp(static_cast<int>(std::round(
                          static_cast<long double>(done) * 100.0L /
                          static_cast<long double>(total))),
                      0, 100);
}

QFrame *makeSurface(const char *name = "surface") {
    auto *surface = new QFrame();
    surface->setObjectName(name);
    return surface;
}

QLabel *makeLabel(const QString &text, const char *name) {
    auto *label = new QLabel(text);
    label->setObjectName(name);
    return label;
}

bool videoPreviewable(const QString &name) {
    const auto ext = name.section('.', -1).toLower();
    return ext == "mp4" || ext == "m4v" || ext == "mov" || ext == "mkv" ||
           ext == "avi" || ext == "webm" || ext == "wmv" ||
           ext == "mpeg" || ext == "mpg";
}

bool visualPreviewable(const QString &name) {
    const auto ext = name.section('.', -1).toLower();
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
           ext == "gif" || ext == "svg" || ext == "pdf" || videoPreviewable(name);
}

bool textPreviewable(const QString &name) {
    const auto ext = name.section('.', -1).toLower();
    return ext == "txt" || ext == "md" || ext == "markdown" || ext == "cpp" ||
           ext == "c" || ext == "h" || ext == "hpp" || ext == "json" ||
           ext == "log" || ext == "csv" || ext == "py" || ext == "cmake" ||
           ext == "yml" || ext == "yaml" || ext == "docx";
}

bool codePreviewable(const QString &name) {
    const auto ext = name.section('.', -1).toLower();
    return ext == "cpp" || ext == "c" || ext == "h" || ext == "hpp" ||
           ext == "py" || ext == "json" || ext == "cmake" || ext == "yml" ||
           ext == "yaml";
}

class HoverPreviewCard : public QFrame {
public:
    explicit HoverPreviewCard(QWidget *parent = nullptr)
        : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint) {
        setObjectName("hoverPreviewCard");
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        resize(520, 360);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 14, 16, 16);
        layout->setSpacing(8);
        title_ = new QLabel();
        title_->setStyleSheet("font-size: 14px; font-weight: 700; color: #1f2328;");
        status_ = new QLabel();
        status_->setStyleSheet("color: #64748b;");
        status_->setWordWrap(true);
        pages_ = new QStackedWidget();
        text_ = new QTextEdit();
        text_->setReadOnly(true);
        code_ = new QPlainTextEdit();
        code_->setReadOnly(true);
        code_->setLineWrapMode(QPlainTextEdit::NoWrap);
        code_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        image_ = new QLabel();
        image_->setAlignment(Qt::AlignCenter);
        imageScroll_ = new QScrollArea();
        imageScroll_->setWidget(image_);
        imageScroll_->setWidgetResizable(true);
        imageScroll_->setAlignment(Qt::AlignCenter);
        pages_->addWidget(text_);
        pages_->addWidget(code_);
        pages_->addWidget(imageScroll_);
        layout->addWidget(title_);
        layout->addWidget(status_);
        layout->addWidget(pages_, 1);
        setStyleSheet("QFrame#hoverPreviewCard{background:#ffffff;border:1px solid #cfd8e3;border-radius:10px;}"
                      "QTextEdit,QPlainTextEdit,QScrollArea{background:#fbfcfe;border:1px solid #e5eaf0;border-radius:6px;}"
                      "QScrollBar:vertical{width:10px;}");
    }
    bool matches(const QString &title) const {
        return visibleTitle_.compare(title, Qt::CaseInsensitive) == 0;
    }

    void hidePreview() {
        visibleTitle_.clear();
        clearAnimatedPreview();
        hide();
    }

    void showLoading(const QString &title, qint64 size, const QPoint &anchor) {
        clearAnimatedPreview();
        visibleTitle_ = title;
        title_->setText(title);
        status_->setText(QStringLiteral("正在加载预览 · %1 KB").arg((size + 1023) / 1024));
        text_->setPlainText(QStringLiteral("正在从服务端读取预览内容…"));
        pages_->setCurrentWidget(text_);
        showNear(anchor);
    }

    void showUnsupported(const QString &title, qint64 size, const QPoint &anchor) {
        clearAnimatedPreview();
        visibleTitle_ = title;
        title_->setText(title);
        status_->setText(QStringLiteral("%1 KB").arg((size + 1023) / 1024));
        text_->setPlainText(QStringLiteral("当前文件类型暂不支持内容预览。\n\n仍可通过右键菜单下载或执行其他操作。"));
        pages_->setCurrentWidget(text_);
        showNear(anchor);
    }

    void showError(const QString &message) {
        if (!isVisible()) return;
        clearAnimatedPreview();
        status_->setText(QStringLiteral("预览失败"));
        text_->setPlainText(message);
        pages_->setCurrentWidget(text_);
    }
    void showText(const QString &title, const QString &content, bool markdown, bool truncated) {
        if (!matches(title)) return;
        clearAnimatedPreview();
        title_->setText(title);
        status_->setText(truncated ? QStringLiteral("预览内容已截断至前 512 KiB")
                                   : QStringLiteral("内容预览"));
        QString visible = content;
        if (truncated) visible += QStringLiteral("\n\n[仅显示前 512 KiB]");
        if (codePreviewable(title)) {
            code_->setPlainText(visible);
            pages_->setCurrentWidget(code_);
        } else {
            if (markdown) text_->setMarkdown(visible);
            else text_->setPlainText(visible);
            pages_->setCurrentWidget(text_);
        }
    }

    void showAsset(const QString &title, const QByteArray &bytes) {
        if (!matches(title)) return;
        if (videoPreviewable(title)) {
            clearAnimatedPreview();
            movieBuffer_ = new QBuffer(this);
            movieBuffer_->setData(bytes);
            if (!movieBuffer_->open(QIODevice::ReadOnly)) {
                showError(QStringLiteral("无法打开视频预览缓存。"));
                return;
            }
            movie_ = new QMovie(movieBuffer_, QByteArray("gif"), this);
            movie_->setCacheMode(QMovie::CacheAll);
            if (!movie_->isValid()) {
                showError(QStringLiteral("服务端返回了视频预览，但客户端无法解析动画。"));
                return;
            }
            image_->clear();
            image_->setMovie(movie_);
            if (movie_->jumpToFrame(0)) {
                QSize frameSize = movie_->currentPixmap().size();
                frameSize.scale(480, 285, Qt::KeepAspectRatio);
                movie_->setScaledSize(frameSize);
                image_->setFixedSize(frameSize);
            }
            title_->setText(title);
            status_->setText(QStringLiteral("视频预览 · 3 秒循环 · 静音"));
            pages_->setCurrentWidget(imageScroll_);
            movie_->start();
            return;
        }
        clearAnimatedPreview();
        QPixmap pixmap;
        if (!pixmap.loadFromData(bytes)) {
            showError(QStringLiteral("服务端返回了预览数据，但无法解析为图像。"));
            return;
        }
        title_->setText(title);
        status_->setText(title.endsWith(".pdf", Qt::CaseInsensitive)
            ? QStringLiteral("PDF 第 1 页") : QStringLiteral("图像预览"));
        image_->setPixmap(pixmap.scaled(480, 285, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
        image_->resize(image_->pixmap().size());
        pages_->setCurrentWidget(imageScroll_);
    }
private:
    void clearAnimatedPreview() {
        if (movie_) {
            movie_->stop();
            image_->setMovie(nullptr);
            delete movie_;
            movie_ = nullptr;
        }
        if (movieBuffer_) {
            movieBuffer_->close();
            delete movieBuffer_;
            movieBuffer_ = nullptr;
        }
        image_->setMinimumSize(0, 0);
        image_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }

    void showNear(const QPoint &anchor) {
        QPoint pos = anchor + QPoint(18, 18);
        QScreen *screen = QGuiApplication::screenAt(anchor);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect area = screen->availableGeometry();
            if (pos.x() + width() > area.right()) pos.setX(anchor.x() - width() - 18);
            if (pos.y() + height() > area.bottom()) pos.setY(area.bottom() - height() - 8);
            if (pos.x() < area.left()) pos.setX(area.left() + 8);
            if (pos.y() < area.top()) pos.setY(area.top() + 8);
        }
        move(pos);
        show();
        raise();
    }

    QString visibleTitle_;
    QLabel *title_ = nullptr;
    QLabel *status_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QTextEdit *text_ = nullptr;
    QPlainTextEdit *code_ = nullptr;
    QLabel *image_ = nullptr;
    QScrollArea *imageScroll_ = nullptr;
    QBuffer *movieBuffer_ = nullptr;
    QMovie *movie_ = nullptr;
};


} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("LanCloudDrive");
    app.setOrganizationName("cpp-project2026");
    // 设置应用级图标，确保窗口和 Windows 任务栏显示 LanCloudDrive 图标。
    QPixmap iconPixmap(64, 64);
    iconPixmap.fill(Qt::transparent);
    QPainter iconPainter(&iconPixmap);
    iconPainter.setRenderHint(QPainter::Antialiasing);
    iconPainter.setPen(Qt::NoPen);
    iconPainter.setBrush(QColor("#0969da"));
    iconPainter.drawRoundedRect(2, 2, 60, 60, 14, 14);
    iconPainter.setBrush(Qt::white);
    iconPainter.drawEllipse(16, 30, 22, 18);
    iconPainter.drawEllipse(28, 22, 24, 26);
    iconPainter.drawRoundedRect(14, 34, 38, 16, 8, 8);
    app.setWindowIcon(QIcon(iconPixmap));
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(R"(
        QWidget { background: #f6f8fa; color: #1f2328; font-family: "Segoe UI"; font-size: 13px; }
        QFrame#surface, QFrame#loginForm, QFrame#sideSurface {
            background: #ffffff; border: 1px solid #d0d7de; border-radius: 6px;
        }
        QFrame#introPanel { background: #f6f8fa; border: none; }
        QFrame#topBar { background: #ffffff; border: none; border-bottom: 1px solid #d8dee4; }
        QLabel#brand { color: #1f2328; font-size: 18px; font-weight: 650; }
        QLabel#loginTitle { color: #1f2328; font-size: 26px; font-weight: 650; }
        QLabel#pageTitle { color: #1f2328; font-size: 20px; font-weight: 650; }
        QLabel#sectionTitle { color: #1f2328; font-size: 14px; font-weight: 650; }
        QLabel#eyebrow { color: #0969da; font-size: 12px; font-weight: 650; }
        QLabel#muted { color: #59636e; }
        QLabel#statusPill { background: #ddf4ff; color: #0969da; border-radius: 10px; padding: 3px 9px; }
        QLabel#featureIcon { background: #ddf4ff; color: #0969da; border-radius: 16px; font-size: 16px; font-weight: 700; }
        QLineEdit {
            background: #ffffff; border: 1px solid #818b98; border-radius: 6px;
            padding: 8px 10px; min-height: 20px; selection-background-color: #0969da;
        }
        QLineEdit:focus { border: 2px solid #0969da; padding: 7px 9px; }
        QPushButton {
            background: #0969da; color: #ffffff; border: 1px solid #0969da;
            border-radius: 6px; padding: 8px 14px; font-weight: 600;
        }
        QPushButton:hover { background: #0860ca; }
        QPushButton:pressed { background: #0757ba; }
        QPushButton#secondaryButton {
            background: #f6f8fa; color: #1f2328; border-color: #d0d7de;
        }
        QPushButton#secondaryButton:hover { background: #eef1f4; }
        QToolBar { background: #ffffff; border: none; spacing: 4px; padding: 0; }
        QToolButton {
            background: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de;
            border-radius: 6px; padding: 7px 10px; font-weight: 550;
        }
        QToolButton:hover { background: #eef1f4; border-color: #afb8c1; }
        QTreeView {
            background: #ffffff; alternate-background-color: #ffffff; border: 1px solid #d0d7de;
            border-radius: 6px; outline: none; show-decoration-selected: 1;
        }
        QHeaderView::section {
            background: #f6f8fa; color: #59636e; border: none;
            border-bottom: 1px solid #d0d7de; padding: 9px 10px; font-weight: 600;
        }
        QTreeView::item { padding: 8px 7px; border-bottom: 1px solid #eaeef2; }
        QTreeView::item:hover { background: #f6f8fa; }
        QTreeView::item:selected { background: #ddf4ff; color: #1f2328; }
        QTextEdit {
            background: #f6f8fa; color: #59636e; border: 1px solid #d8dee4;
            border-radius: 6px; padding: 6px;
        }
        QProgressBar {
            background: #eaeef2; border: none; border-radius: 3px;
            min-height: 6px; max-height: 6px; color: transparent;
        }
        QProgressBar::chunk { background: #0969da; border-radius: 3px; }
        QMenu { background: #ffffff; border: 1px solid #d0d7de; padding: 4px; }
        QMenu::item { padding: 7px 28px 7px 10px; border-radius: 4px; }
        QMenu::item:selected { background: #ddf4ff; color: #1f2328; }
    )");
    app.setStyleSheet(app.styleSheet() + R"(
        QFrame#sidebar { background: #f7f9fc; border: none; border-right: 1px solid #e7ebf0; }
        QLabel#brandSub { color: #8a95a3; font-size: 11px; }
        QLabel#navGroup { color: #98a2b3; font-size: 11px; font-weight: 700; padding: 11px 10px 5px 10px; }
        QLabel#breadcrumb { color: #64748b; font-size: 12px; }
        QPushButton#breadcrumb {
            background: transparent; color: #64748b; border: none;
            padding: 2px 0; text-align: left; font-size: 12px;
        }
        QPushButton#breadcrumb:hover { color: #1664d9; }
        QPushButton#navButton { background: transparent; color: #475569; border: none; border-radius: 7px; padding: 9px 12px; text-align: left; font-weight: 500; }
        QPushButton#navButton:hover { background: #edf4ff; color: #1664d9; }
        QPushButton#navCurrent { background: #eaf2ff; color: #1769e0; border: none; border-radius: 7px; padding: 9px 12px; text-align: left; font-weight: 700; }
        QLineEdit#searchBox { background: #f7f9fc; border: 1px solid #e2e8f0; border-radius: 18px; padding: 7px 14px; min-width: 260px; }
        QLineEdit#searchBox:focus { background: #ffffff; border: 1px solid #6fa8ff; }
        QFrame#statusStrip { background: #fbfcfe; border: 1px solid #edf0f4; border-radius: 7px; }
)");

    QWidget window;
    window.setWindowTitle("LanCloudDrive");
    window.setWindowIcon(app.windowIcon());
    window.setMinimumSize(1180, 720);
    window.resize(1480, 880);
    auto *windowLayout = new QVBoxLayout(&window);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    auto *pages = new QStackedWidget();
    windowLayout->addWidget(pages);

    // Authentication is intentionally separate from the file workspace.
    auto *loginPage = new QWidget();
    auto *loginPageLayout = new QVBoxLayout(loginPage);
    loginPageLayout->setContentsMargins(52, 34, 52, 52);
    auto *loginBrandRow = new QHBoxLayout();
    auto *loginBrand = makeLabel("LanCloudDrive", "brand");
    auto *lanBadge = makeLabel(QStringLiteral("局域网私有云"), "statusPill");
    loginBrandRow->addWidget(loginBrand);
    loginBrandRow->addStretch();
    loginBrandRow->addWidget(lanBadge);
    loginPageLayout->addLayout(loginBrandRow);
    loginPageLayout->addStretch();

    auto *loginContent = new QWidget();
    loginContent->setMaximumWidth(960);
    auto *loginContentLayout = new QHBoxLayout(loginContent);
    loginContentLayout->setContentsMargins(0, 0, 0, 0);
    loginContentLayout->setSpacing(64);

    auto *intro = makeSurface("introPanel");
    auto *introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(0, 12, 20, 12);
    introLayout->setSpacing(12);
    introLayout->addWidget(makeLabel(QStringLiteral("PRIVATE CLOUD · LOCAL NETWORK"), "eyebrow"));
    introLayout->addWidget(makeLabel(QStringLiteral("团队文件，\n安全留在局域网内。"), "loginTitle"));
    auto *introText = makeLabel(
        QStringLiteral("一台电脑运行服务端，同一局域网内的其他设备通过 IP 地址连接。"),
        "muted");
    introText->setWordWrap(true);
    introLayout->addWidget(introText);
    introLayout->addSpacing(18);
    const QStringList features = {
        "Upload and download complete folders",
        "Switch accounts with an automatic refresh",
        "Transfer progress and local activity history"
    };
    for (const auto &feature : features) {
        auto *row = new QHBoxLayout();
        auto *icon = makeLabel("✓", "featureIcon");
        icon->setFixedSize(32, 32);
        icon->setAlignment(Qt::AlignCenter);
        auto *textLabel = new QLabel(feature);
        textLabel->setWordWrap(true);
        row->addWidget(icon);
        row->addSpacing(8);
        row->addWidget(textLabel, 1);
        introLayout->addLayout(row);
    }
    introLayout->addStretch();

    QSettings settings;
    auto *loginForm = makeSurface("loginForm");
    loginForm->setFixedWidth(390);
    auto *formLayout = new QVBoxLayout(loginForm);
    formLayout->setContentsMargins(30, 30, 30, 30);
    formLayout->setSpacing(10);
    formLayout->addWidget(makeLabel(QStringLiteral("登录"), "pageTitle"));
    formLayout->addWidget(makeLabel(QStringLiteral("连接到 LanCloudDrive 服务端"), "muted"));
    formLayout->addSpacing(10);
    formLayout->addWidget(makeLabel(QStringLiteral("服务器"), "sectionTitle"));
    auto *serverRow = new QHBoxLayout();
    auto *hostEdit = new QLineEdit(settings.value("server/host", "127.0.0.1").toString());
    hostEdit->setPlaceholderText(QStringLiteral("服务器 IP 地址"));
    auto *portEdit = new QLineEdit(settings.value("server/port", "9000").toString());
    portEdit->setPlaceholderText(QStringLiteral("端口"));
    portEdit->setMaximumWidth(88);
    serverRow->addWidget(hostEdit, 1);
    serverRow->addWidget(portEdit);
    formLayout->addLayout(serverRow);
    formLayout->addWidget(makeLabel(QStringLiteral("账号"), "sectionTitle"));
    auto *userEdit = new QLineEdit();
    userEdit->setPlaceholderText(QStringLiteral("用户名"));
    auto *passEdit = new QLineEdit();
    passEdit->setPlaceholderText(QStringLiteral("密码"));
    passEdit->setEchoMode(QLineEdit::Password);
    auto *loginButton = new QPushButton(QStringLiteral("登录"));
    auto *registerButton = new QPushButton(QStringLiteral("创建账号"));
    registerButton->setObjectName("secondaryButton");
    auto *loginStatus = makeLabel("Use this computer's LAN IP when signing in from another device.", "muted");
    loginStatus->setWordWrap(true);
    formLayout->addWidget(userEdit);
    formLayout->addWidget(passEdit);
    formLayout->addSpacing(4);
    formLayout->addWidget(loginButton);
    formLayout->addWidget(registerButton);
    formLayout->addSpacing(6);
    formLayout->addWidget(loginStatus);
    loginContentLayout->addWidget(intro, 1);
    loginContentLayout->addWidget(loginForm);
    loginPageLayout->addWidget(loginContent, 0, Qt::AlignHCenter);
    loginPageLayout->addStretch();

    // Main file workspace.
    auto *workspacePage = new QWidget();
    auto *workspacePageLayout = new QVBoxLayout(workspacePage);
    workspacePageLayout->setContentsMargins(0, 0, 0, 0);
    workspacePageLayout->setSpacing(0);
    auto *topBar = makeSurface("topBar");
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(24, 13, 24, 13);
    auto *brandColumn = new QVBoxLayout();
    brandColumn->setSpacing(0);
    auto *workspaceBrand = makeLabel(QStringLiteral("☁  LanCloudDrive"), "brand");
    brandColumn->addWidget(workspaceBrand);
    brandColumn->addWidget(makeLabel(QStringLiteral("校园网盘 / 文档管理"), "brandSub"));
    auto *serverBadge = makeLabel(QStringLiteral("未连接"), "statusPill");
    auto *searchEdit = new QLineEdit();
    searchEdit->setObjectName("searchBox");
    searchEdit->setPlaceholderText(QStringLiteral("搜索文件 / 文件夹"));
    auto *accountLabel = makeLabel("", "sectionTitle");
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"));
    logoutButton->setObjectName("secondaryButton");
    topLayout->addLayout(brandColumn);
    topLayout->addSpacing(14);
    topLayout->addWidget(serverBadge);
    topLayout->addStretch();
    topLayout->addWidget(searchEdit);
    topLayout->addSpacing(18);
    topLayout->addWidget(accountLabel);
    topLayout->addSpacing(8);
    topLayout->addWidget(logoutButton);
    workspacePageLayout->addWidget(topBar);

    auto *workspaceBody = new QWidget();
    auto *bodyLayout = new QHBoxLayout(workspaceBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto *sidebar = makeSurface("sidebar");
    sidebar->setFixedWidth(214);
    auto *navLayout = new QVBoxLayout(sidebar);
    navLayout->setContentsMargins(14, 18, 14, 18);
    navLayout->setSpacing(3);
    const auto addNavGroup = [&](const QString &title, const QStringList &items, int current = -1) {
        navLayout->addWidget(makeLabel(title, "navGroup"));
        for (int i = 0; i < items.size(); ++i) {
            auto *button = new QPushButton(items.at(i));
            button->setObjectName(i == current ? "navCurrent" : "navButton");
            button->setCursor(Qt::PointingHandCursor);
            navLayout->addWidget(button);
        }
    };
    addNavGroup(QStringLiteral("文档访问"), {QStringLiteral("⌂  个人文档"), QStringLiteral("☁  共享文档"), QStringLiteral("◎  群组文档"), QStringLiteral("▣  文档库"), QStringLiteral("▤  归档库"), QStringLiteral("⇄  收发任务"), QStringLiteral("♲  回收站"), QStringLiteral("◇  隔离区")}, 3);
    addNavGroup(QStringLiteral("共享管理"), {QStringLiteral("⌘  权限共享"), QStringLiteral("↗  外链共享"), QStringLiteral("◉  发现共享"), QStringLiteral("⊘  已屏蔽共享")});
    addNavGroup(QStringLiteral("审核管理"), {QStringLiteral("▧  权限申请"), QStringLiteral("✓  权限审核"), QStringLiteral("⇢  流程申请")});
    navLayout->addStretch();
    auto *quota = makeLabel(QStringLiteral("存储空间\n默认配额 10 GB"), "muted");
    quota->setWordWrap(true);
    navLayout->addWidget(quota);
    bodyLayout->addWidget(sidebar);
    auto *fileSurface = makeSurface();
    auto *fileLayout = new QVBoxLayout(fileSurface);
    fileLayout->setContentsMargins(18, 16, 18, 18);
    fileLayout->setSpacing(12);
    auto *fileHeading = new QHBoxLayout();
    auto *headingColumn = new QVBoxLayout();
    auto *locationLabel = makeLabel(QStringLiteral("文档库"), "pageTitle");
    auto *itemCount = makeLabel(QStringLiteral("0 项"), "muted");
    headingColumn->addWidget(locationLabel);
    headingColumn->addWidget(itemCount);
    fileHeading->addLayout(headingColumn);
    fileHeading->addStretch();
    auto *toolbar = new QToolBar();
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *newFolderAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DirIcon), QStringLiteral("新建文件夹"));
    auto *uploadFileAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_ArrowUp), QStringLiteral("上传文件"));
    auto *uploadFolderAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("上传文件夹"));
    auto *downloadAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_ArrowDown), QStringLiteral("下载"));
    auto *shareAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("分享"));
    auto *claimCodeAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("提取文件"));
    auto *refreshAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"));
    auto *upAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_ArrowBack), QStringLiteral("上一层"));
    auto *permissionAction = toolbar->addAction(QStringLiteral("权限配置"));
    permissionAction->setEnabled(false);
    permissionAction->setToolTip(QStringLiteral("当前版本暂未实现权限管理"));
    fileHeading->addWidget(toolbar);
    // 面包屑同时作为可点击的“返回上一级”按钮，避免用户只能使用工具栏按钮。
    auto *breadcrumbLabel = new QPushButton(QStringLiteral("回到上一层  |  文档库"));
    breadcrumbLabel->setObjectName("breadcrumb");
    breadcrumbLabel->setFlat(true);
    breadcrumbLabel->setCursor(Qt::PointingHandCursor);
    auto *browser = new FileBrowser();
    auto *hoverPreview = new HoverPreviewCard(&window);
    fileLayout->addLayout(fileHeading);
    fileLayout->addWidget(breadcrumbLabel);
    fileLayout->addWidget(browser, 1);

    auto *statusStrip = makeSurface("statusStrip");
    auto *statusLayout = new QHBoxLayout(statusStrip);
    statusLayout->setContentsMargins(12, 9, 12, 9);
    statusLayout->setSpacing(10);
    auto *uploadLabel = makeLabel(QStringLiteral("上传空闲"), "muted");
    auto *uploadProgress = new QProgressBar();
    uploadProgress->setRange(0, 100);
    uploadProgress->setFixedWidth(130);
    auto *downloadLabel = makeLabel(QStringLiteral("下载空闲"), "muted");
    auto *downloadProgress = new QProgressBar();
    downloadProgress->setRange(0, 100);
    downloadProgress->setFixedWidth(130);
    auto *log = new QTextEdit();
    log->setReadOnly(true);
    log->setVisible(false);
    statusLayout->addWidget(uploadLabel);
    statusLayout->addWidget(uploadProgress);
    statusLayout->addSpacing(16);
    statusLayout->addWidget(downloadLabel);
    statusLayout->addWidget(downloadProgress);
    statusLayout->addStretch();
    statusLayout->addWidget(makeLabel(QStringLiteral("就绪"), "muted"));
    fileLayout->addWidget(statusStrip);
    bodyLayout->addWidget(fileSurface, 1);
    workspacePageLayout->addWidget(workspaceBody, 1);

    pages->addWidget(loginPage);
    pages->addWidget(workspacePage);
    pages->setCurrentWidget(loginPage);

    QtClient *client = nullptr;
    QVariantList currentEntries;
    qint64 currentParent = 0;
    QVector<qint64> parentStack;
    QStringList pathNames;

    const auto updateLocation = [&]() {
        const QString pathText = pathNames.isEmpty()
            ? QStringLiteral("文档库")
            : QStringLiteral("文档库 / %1").arg(pathNames.join(" / "));
        locationLabel->setText(pathText);
        breadcrumbLabel->setText(QStringLiteral("回到上一层  |  %1").arg(QString(pathText).replace(" / ", "  >  ")));
        upAction->setEnabled(!parentStack.isEmpty());
    };
    const auto applySearch = [&]() {
        const QString needle = searchEdit->text().trimmed();
        if (needle.isEmpty()) {
            browser->setEntries(currentEntries);
            itemCount->setText(QStringLiteral("%1 项").arg(currentEntries.size()));
            return;
        }
        QVariantList filtered;
        for (const auto &entry : currentEntries) {
            if (entry.toMap().value("name").toString().contains(needle, Qt::CaseInsensitive))
                filtered.append(entry);
        }
        browser->setEntries(filtered);
        itemCount->setText(QStringLiteral("%1 / %2 项").arg(filtered.size()).arg(currentEntries.size()));
    };
    const auto doList = [&](qint64 parentId) {
        if (!client) return;
        log->append(QStringLiteral("Refreshing folder %1...").arg(parentId));
        client->list(parentId);
    };
    const auto bindClient = [&](QtClient *boundClient) {
        QObject::connect(boundClient, &QtClient::registerFinished, [&]() {
            loginStatus->setStyleSheet("color: #1a7f37;");
            loginStatus->setText("Account created. Sign in with the same credentials.");
        });
        QObject::connect(boundClient, &QtClient::loginFinished, [&]() {
            currentParent = 0;
            parentStack.clear();
            pathNames.clear();
            updateLocation();
            accountLabel->setText(userEdit->text().trimmed());
            browser->setCreatorName(userEdit->text().trimmed());
            serverBadge->setText(QStringLiteral("%1:%2").arg(hostEdit->text().trimmed(), portEdit->text()));
            pages->setCurrentWidget(workspacePage);
            doList(0);
        });
        QObject::connect(boundClient, &QtClient::logoutFinished, [&]() {
            currentEntries.clear();
            hoverPreview->hidePreview();
            browser->setEntries({});
            searchEdit->clear();
            pages->setCurrentWidget(loginPage);
            loginStatus->setStyleSheet("color: #59636e;");
            loginStatus->setText("Signed out. Sign in to refresh another account.");
        });
        QObject::connect(boundClient, &QtClient::errorOccurred,
                         [&](const QString &code, const QString &message) {
            log->append(QStringLiteral("ERROR [%1] %2").arg(code, message));
            if (hoverPreview->isVisible()) hoverPreview->showError(message);
            if (pages->currentWidget() == loginPage) {
                loginStatus->setStyleSheet("color: #cf222e;");
                loginStatus->setText(QStringLiteral("%1: %2").arg(code, message));
            }
        });
        QObject::connect(boundClient, &QtClient::listReady, [&](const QVariantList &entries) {
            currentEntries = entries;
            applySearch();
            log->append(QStringLiteral("目录已刷新：%1 项").arg(entries.size()));
        });
        QObject::connect(boundClient, &QtClient::mkdirFinished, [&](qint64) {
            log->append("Folder created");
            doList(currentParent);
        });
        QObject::connect(boundClient, &QtClient::renameFinished, [&]() {
            log->append("Item renamed");
            doList(currentParent);
        });
        QObject::connect(boundClient, &QtClient::deleteFinished, [&]() {
            log->append("Item deleted");
            doList(currentParent);
        });
        QObject::connect(boundClient, &QtClient::shareCodeCreated, [&](const QString &code) {
            const auto displayCode = code.toUpper();
            log->append(QStringLiteral("Extraction code created: %1").arg(displayCode));
            QMessageBox::information(&window, "Extraction code",
                                     QStringLiteral("Send this code to the recipient:\n\n%1\n\n"
                                                    "It is valid for 24 hours and can be claimed once.")
                                         .arg(displayCode));
        });
        QObject::connect(boundClient, &QtClient::shareCodeClaimed, [&](qint64 nodeId) {
            currentParent = 0;
            parentStack.clear();
            pathNames.clear();
            updateLocation();
            log->append(QStringLiteral("Extraction code claimed, new node %1").arg(nodeId));
            QMessageBox::information(&window, "File received",
                                     "The shared file was added to My files.");
            doList(0);
        });
        QObject::connect(boundClient, &QtClient::previewReady,
                         [&](const QString &title, const QString &content, bool markdown, bool truncated) {
            hoverPreview->showText(title, content, markdown, truncated);
        });
        QObject::connect(boundClient, &QtClient::previewAssetReady,
                         [&](const QString &title, const QByteArray &bytes) {
            hoverPreview->showAsset(title, bytes);
        });
        QObject::connect(boundClient, &QtClient::uploadProgress, [&](qint64 done, qint64 total) {
            const int percent = progressPercent(done, total);
            uploadProgress->setValue(percent);
            uploadLabel->setText(QStringLiteral("Uploading · %1%").arg(percent));
        });
        QObject::connect(boundClient, &QtClient::uploadFinished, [&](qint64 nodeId) {
            uploadProgress->setValue(100);
            uploadLabel->setText("Upload complete");
            log->append(QStringLiteral("Upload finished, node %1").arg(nodeId));
            doList(currentParent);
        });
        QObject::connect(boundClient, &QtClient::downloadProgress, [&](qint64 done, qint64 total) {
            const int percent = progressPercent(done, total);
            downloadProgress->setValue(percent);
            downloadLabel->setText(QStringLiteral("Downloading · %1%").arg(percent));
        });
        QObject::connect(boundClient, &QtClient::downloadFinished, [&]() {
            downloadProgress->setValue(100);
            downloadLabel->setText("Download complete");
            log->append("Download finished");
        });
    };

    const auto rebuildClient = [&]() {
        if (client) delete client;
        const QString host = hostEdit->text().trimmed();
        const quint16 port = static_cast<quint16>(portEdit->text().toUShort());
        settings.setValue("server/host", host);
        settings.setValue("server/port", portEdit->text());
        client = new QtClient(host, port, &window);
        bindClient(client);
        loginStatus->setStyleSheet("color: #59636e;");
        loginStatus->setText(QStringLiteral("Connecting to %1:%2...").arg(host).arg(port));
    };

    QObject::connect(loginButton, &QPushButton::clicked, [&]() {
        if (hostEdit->text().trimmed().isEmpty() || portEdit->text().toUShort() == 0 ||
            userEdit->text().trimmed().isEmpty() || passEdit->text().isEmpty()) {
            loginStatus->setStyleSheet("color: #cf222e;");
            loginStatus->setText("Enter a server address, username, and password.");
            return;
        }
        rebuildClient();
        client->login(userEdit->text(), passEdit->text());
    });
    QObject::connect(registerButton, &QPushButton::clicked, [&]() {
        if (hostEdit->text().trimmed().isEmpty() || portEdit->text().toUShort() == 0 ||
            userEdit->text().trimmed().isEmpty() || passEdit->text().isEmpty()) {
            loginStatus->setStyleSheet("color: #cf222e;");
            loginStatus->setText("Enter a server address, username, and password first.");
            return;
        }
        rebuildClient();
        client->registerUser(userEdit->text(), passEdit->text());
    });
    QObject::connect(passEdit, &QLineEdit::returnPressed, loginButton, &QPushButton::click);
    QObject::connect(logoutButton, &QPushButton::clicked, [&]() {
        if (client) client->logout();
    });
    QObject::connect(searchEdit, &QLineEdit::textChanged, [&](const QString &) {
        applySearch();
    });
    QObject::connect(refreshAction, &QAction::triggered, [&]() { doList(currentParent); });
    QObject::connect(upAction, &QAction::triggered, [&]() {
        if (parentStack.isEmpty()) return;
        currentParent = parentStack.takeLast();
        if (!pathNames.isEmpty()) pathNames.removeLast();
        updateLocation();
        doList(currentParent);
    });
    QObject::connect(breadcrumbLabel, &QPushButton::clicked, [&]() {
        if (parentStack.isEmpty()) return;
        currentParent = parentStack.takeLast();
        if (!pathNames.isEmpty()) pathNames.removeLast();
        updateLocation();
        doList(currentParent);
    });
    QObject::connect(newFolderAction, &QAction::triggered, [&]() {
        if (!client) return;
        bool ok = false;
        const QString name = QInputDialog::getText(&window, "New folder", "Folder name:",
                                                    QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty()) client->mkdir(currentParent, name);
    });
    QObject::connect(uploadFileAction, &QAction::triggered, [&]() {
        const QString path = QFileDialog::getOpenFileName(&window, "Select file to upload");
        if (path.isEmpty() || !client) return;
        uploadProgress->setValue(0);
        uploadLabel->setText("Preparing file upload...");
        log->append(QStringLiteral("Uploading file %1").arg(path));
        client->upload(path, currentParent, QString());
    });
    QObject::connect(uploadFolderAction, &QAction::triggered, [&]() {
        const QString path = QFileDialog::getExistingDirectory(&window, "Select folder to upload");
        if (path.isEmpty() || !client) return;
        uploadProgress->setValue(0);
        uploadLabel->setText("Preparing folder upload...");
        log->append(QStringLiteral("Uploading folder %1").arg(path));
        client->uploadDirectory(path, currentParent);
    });
    QObject::connect(downloadAction, &QAction::triggered, [&]() {
        if (!client) return;
        qint64 id = 0; QString name; bool directory = false;
        if (!browser->selectedNode(id, name, directory)) {
            QMessageBox::information(&window, QStringLiteral("下载"), QStringLiteral("请先选择一个文件或文件夹。"));
            return;
        }
        if (directory) {
            const QString parent = QFileDialog::getExistingDirectory(&window, QStringLiteral("选择保存目录"));
            if (!parent.isEmpty()) client->downloadDirectory(id, name, parent);
        } else {
            const QString save = QFileDialog::getSaveFileName(&window, QStringLiteral("文件另存为"), name);
            if (!save.isEmpty()) client->download(id, save);
        }
    });
    QObject::connect(shareAction, &QAction::triggered, [&]() {
        if (!client) return;
        qint64 id = 0; QString name; bool directory = false;
        if (!browser->selectedNode(id, name, directory)) {
            QMessageBox::information(&window, QStringLiteral("分享"), QStringLiteral("请先选择一个文件。"));
            return;
        }
        if (directory) {
            QMessageBox::information(&window, QStringLiteral("分享"), QStringLiteral("当前版本仅支持单文件提取码分享。"));
            return;
        }
        client->createShareCode(id);
    });
    QObject::connect(claimCodeAction, &QAction::triggered, [&]() {
        if (!client) return;
        bool ok = false;
        const QString code = QInputDialog::getText(&window, "Extract shared file",
                                                    "8-character extraction code:",
                                                    QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !code.isEmpty()) client->claimShareCode(code);
    });
    QObject::connect(browser, &FileBrowser::previewHovered,
                     [&](qint64 id, const QString &name, qint64 size, const QPoint &globalPos) {
        if (visualPreviewable(name) || textPreviewable(name)) {
            hoverPreview->showLoading(name, size, globalPos);
        } else {
            hoverPreview->showUnsupported(name, size, globalPos);
            return;
        }
        if (!client) {
            hoverPreview->showError(QStringLiteral("尚未连接服务端。"));
            return;
        }
        if (visualPreviewable(name)) client->previewAsset(id, name, 1);
        else client->preview(id);
    });
    QObject::connect(browser, &FileBrowser::previewHoverEnded,
                     [&]() { hoverPreview->hidePreview(); });
    QObject::connect(browser, &FileBrowser::enterDirectory,
                     [&](qint64 id, const QString &name) {
        parentStack.append(currentParent);
        pathNames.append(name);
        currentParent = id;
        updateLocation();
        doList(currentParent);
    });
    QObject::connect(browser, &FileBrowser::downloadNode,
                     [&](qint64 id, const QString &name, bool directory) {
        if (!client) return;
        downloadProgress->setValue(0);
        if (directory) {
            const QString parent = QFileDialog::getExistingDirectory(
                &window, "Choose where to save the folder");
            if (parent.isEmpty()) return;
            downloadLabel->setText("Preparing folder download...");
            log->append(QStringLiteral("Downloading folder %1 to %2").arg(name, parent));
            client->downloadDirectory(id, name, parent);
            return;
        }
        const QString save = QFileDialog::getSaveFileName(&window, "Save file as", name);
        if (save.isEmpty()) return;
        downloadLabel->setText("Preparing file download...");
        log->append(QStringLiteral("Downloading %1 to %2").arg(name, save));
        client->download(id, save);
    });
    QObject::connect(browser, &FileBrowser::renameNode, [&](qint64 id) {
        bool ok = false;
        const QString name = QInputDialog::getText(&window, "Rename", "New name:",
                                                    QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty() && client) client->renameNode(id, name);
    });
    QObject::connect(browser, &FileBrowser::deleteNode, [&](qint64 id) {
        if (!client) return;
        if (QMessageBox::question(&window, "Delete item",
                                  "Delete this item from the server? This cannot be undone.")
            == QMessageBox::Yes) {
            client->deleteNode(id);
        }
    });
    QObject::connect(browser, &FileBrowser::createShareCode, [&](qint64 id) {
        if (client) client->createShareCode(id);
    });
    QObject::connect(browser, &FileBrowser::refreshRequested, [&]() { doList(currentParent); });

    updateLocation();
    window.show();
    return app.exec();
}
