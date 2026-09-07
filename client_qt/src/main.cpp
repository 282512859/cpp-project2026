#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
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

QString normalizedExtractionCode(QString code) {
    code.remove(QRegularExpression(QStringLiteral("[\\s-]")));
    return code.toUpper();
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

} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("LanCloudDrive");
    app.setOrganizationName("cpp-project2026");
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

    QWidget window;
    window.setWindowTitle("LanCloudDrive");
    window.setMinimumSize(1040, 700);
    window.resize(1240, 790);
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
    auto *lanBadge = makeLabel("LAN file workspace", "statusPill");
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
    introLayout->addWidget(makeLabel("PRIVATE CLOUD · LOCAL NETWORK", "eyebrow"));
    introLayout->addWidget(makeLabel("Files for your team,\nkept on your network.", "loginTitle"));
    auto *introText = makeLabel(
        "One computer runs the server. Other computers on the same LAN connect with its IP address.",
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
    formLayout->addWidget(makeLabel("Sign in", "pageTitle"));
    formLayout->addWidget(makeLabel("Connect to your LanCloudDrive server", "muted"));
    formLayout->addSpacing(10);
    formLayout->addWidget(makeLabel("Server", "sectionTitle"));
    auto *serverRow = new QHBoxLayout();
    auto *hostEdit = new QLineEdit(settings.value("server/host", "127.0.0.1").toString());
    hostEdit->setPlaceholderText("Server IP address");
    auto *portEdit = new QLineEdit(settings.value("server/port", "9000").toString());
    portEdit->setPlaceholderText("Port");
    portEdit->setMaximumWidth(88);
    serverRow->addWidget(hostEdit, 1);
    serverRow->addWidget(portEdit);
    formLayout->addLayout(serverRow);
    formLayout->addWidget(makeLabel("Account", "sectionTitle"));
    auto *userEdit = new QLineEdit();
    userEdit->setPlaceholderText("Username");
    auto *passEdit = new QLineEdit();
    passEdit->setPlaceholderText("Password");
    passEdit->setEchoMode(QLineEdit::Password);
    auto *loginButton = new QPushButton("Sign in");
    auto *registerButton = new QPushButton("Create account");
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
    auto *workspaceBrand = makeLabel("LanCloudDrive", "brand");
    auto *serverBadge = makeLabel("Offline", "statusPill");
    auto *accountLabel = makeLabel("", "sectionTitle");
    auto *logoutButton = new QPushButton("Sign out");
    logoutButton->setObjectName("secondaryButton");
    topLayout->addWidget(workspaceBrand);
    topLayout->addSpacing(12);
    topLayout->addWidget(serverBadge);
    topLayout->addStretch();
    topLayout->addWidget(accountLabel);
    topLayout->addSpacing(8);
    topLayout->addWidget(logoutButton);
    workspacePageLayout->addWidget(topBar);

    auto *workspaceBody = new QWidget();
    auto *bodyLayout = new QHBoxLayout(workspaceBody);
    bodyLayout->setContentsMargins(24, 22, 24, 24);
    bodyLayout->setSpacing(16);
    auto *fileSurface = makeSurface();
    auto *fileLayout = new QVBoxLayout(fileSurface);
    fileLayout->setContentsMargins(18, 16, 18, 18);
    fileLayout->setSpacing(12);
    auto *fileHeading = new QHBoxLayout();
    auto *headingColumn = new QVBoxLayout();
    auto *locationLabel = makeLabel("My files", "pageTitle");
    auto *itemCount = makeLabel("0 items", "muted");
    headingColumn->addWidget(locationLabel);
    headingColumn->addWidget(itemCount);
    fileHeading->addLayout(headingColumn);
    fileHeading->addStretch();
    auto *toolbar = new QToolBar();
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *upAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_ArrowUp), "Up");
    auto *refreshAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_BrowserReload), "Refresh");
    auto *newFolderAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DirIcon), "New folder");
    auto *uploadFileAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_FileIcon), "Upload file");
    auto *uploadFolderAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DirOpenIcon), "Upload folder");
    auto *claimCodeAction = toolbar->addAction(window.style()->standardIcon(QStyle::SP_DialogOpenButton), "Extract code");
    fileHeading->addWidget(toolbar);
    auto *browser = new FileBrowser();
    fileLayout->addLayout(fileHeading);
    fileLayout->addWidget(browser, 1);

    auto *sideColumn = new QVBoxLayout();
    sideColumn->setSpacing(12);
    auto *transferSurface = makeSurface("sideSurface");
    auto *transferLayout = new QVBoxLayout(transferSurface);
    transferLayout->setContentsMargins(16, 15, 16, 16);
    transferLayout->setSpacing(8);
    transferLayout->addWidget(makeLabel("Transfers", "sectionTitle"));
    auto *uploadLabel = makeLabel("Upload idle", "muted");
    auto *uploadProgress = new QProgressBar();
    uploadProgress->setRange(0, 100);
    auto *downloadLabel = makeLabel("Download idle", "muted");
    auto *downloadProgress = new QProgressBar();
    downloadProgress->setRange(0, 100);
    transferLayout->addWidget(uploadLabel);
    transferLayout->addWidget(uploadProgress);
    transferLayout->addSpacing(5);
    transferLayout->addWidget(downloadLabel);
    transferLayout->addWidget(downloadProgress);

    auto *activitySurface = makeSurface("sideSurface");
    auto *activityLayout = new QVBoxLayout(activitySurface);
    activityLayout->setContentsMargins(16, 15, 16, 16);
    activityLayout->setSpacing(9);
    activityLayout->addWidget(makeLabel("Activity", "sectionTitle"));
    auto *log = new QTextEdit();
    log->setReadOnly(true);
    log->setPlaceholderText("Recent activity appears here");
    activityLayout->addWidget(log);
    sideColumn->addWidget(transferSurface);
    sideColumn->addWidget(activitySurface, 1);
    bodyLayout->addWidget(fileSurface, 1);
    bodyLayout->addLayout(sideColumn);
    workspacePageLayout->addWidget(workspaceBody, 1);

    pages->addWidget(loginPage);
    pages->addWidget(workspacePage);
    pages->setCurrentWidget(loginPage);

    QtClient *client = nullptr;
    qint64 currentParent = 0;
    QVector<qint64> parentStack;
    QStringList pathNames;

    const auto updateLocation = [&]() {
        locationLabel->setText(pathNames.isEmpty()
            ? QStringLiteral("My files")
            : QStringLiteral("My files / %1").arg(pathNames.join(" / ")));
        upAction->setEnabled(!parentStack.isEmpty());
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
            serverBadge->setText(QStringLiteral("%1:%2").arg(hostEdit->text().trimmed(), portEdit->text()));
            pages->setCurrentWidget(workspacePage);
            doList(0);
        });
        QObject::connect(boundClient, &QtClient::logoutFinished, [&]() {
            browser->setEntries({});
            pages->setCurrentWidget(loginPage);
            loginStatus->setStyleSheet("color: #59636e;");
            loginStatus->setText("Signed out. Sign in to refresh another account.");
        });
        QObject::connect(boundClient, &QtClient::errorOccurred,
                         [&](const QString &code, const QString &message) {
            log->append(QStringLiteral("ERROR [%1] %2").arg(code, message));
            if (pages->currentWidget() == loginPage) {
                loginStatus->setStyleSheet("color: #cf222e;");
                loginStatus->setText(QStringLiteral("%1: %2").arg(code, message));
            }
        });
        QObject::connect(boundClient, &QtClient::listReady, [&](const QVariantList &entries) {
            browser->setEntries(entries);
            itemCount->setText(QStringLiteral("%1 item%2").arg(entries.size())
                               .arg(entries.size() == 1 ? "" : "s"));
            log->append(QStringLiteral("Folder refreshed: %1 entries").arg(entries.size()));
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
            const auto groupedCode = QStringLiteral("%1-%2")
                                         .arg(displayCode.left(4), displayCode.mid(4));
            QApplication::clipboard()->setText(displayCode);
            log->append(QStringLiteral("Extraction code created: %1").arg(displayCode));
            QMessageBox::information(&window, "Extraction code",
                                     QStringLiteral("Send this code to the recipient:\n\n%1\n\n"
                                                    "It has been copied to the clipboard. It is valid for 24 hours "
                                                    "and can be claimed once.")
                                         .arg(groupedCode));
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
    QObject::connect(refreshAction, &QAction::triggered, [&]() { doList(currentParent); });
    QObject::connect(upAction, &QAction::triggered, [&]() {
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
    QObject::connect(claimCodeAction, &QAction::triggered, [&]() {
        if (!client) return;
        bool ok = false;
        const QString enteredCode = QInputDialog::getText(&window, "Extract shared file",
                                                           "8-character code (for example: A1B2-C3D4):",
                                                           QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        const QString code = normalizedExtractionCode(enteredCode);
        static const QRegularExpression validCode(QStringLiteral("^[0-9A-F]{8}$"));
        if (!validCode.match(code).hasMatch()) {
            QMessageBox::warning(&window, "Invalid extraction code",
                                 "Enter the 8-character code created by the sender. "
                                 "Only 0-9 and A-F are allowed; spaces and hyphens are optional.");
            return;
        }
        client->claimShareCode(code);
    });
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
