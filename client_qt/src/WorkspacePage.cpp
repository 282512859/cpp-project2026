#include "client_qt/WorkspacePage.h"
#include "client_qt/FileBrowser.h"
#include "client_qt/Theme.h"
#include "cloud/client/QtClient.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace client_qt {

using cloud::client::QtClient;

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

QFrame* makeSurface(const char* name = "surface") {
    auto* surface = new QFrame();
    surface->setObjectName(name);
    return surface;
}

QLabel* makeLabel(const QString& text, const char* name) {
    auto* label = new QLabel(text);
    label->setObjectName(name);
    return label;
}

bool visualPreviewable(const QString& name) {
    const auto extension = name.section('.', -1).toLower();
    return extension == "png" || extension == "jpg" || extension == "jpeg" ||
           extension == "bmp" || extension == "gif" || extension == "svg" ||
           extension == "pdf";
}

bool codePreviewable(const QString& name) {
    const auto extension = name.section('.', -1).toLower();
    return extension == "cpp" || extension == "c" || extension == "h" || extension == "hpp" ||
           extension == "py" || extension == "json" || extension == "cmake" || extension == "yml" ||
           extension == "yaml" || extension == "tex";
}

QList<QString> splitTableRow(const QString& line, QChar separator) {
    QList<QString> fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const auto character = line.at(i);
        if (character == '"' && quoted && i + 1 < line.size() && line.at(i + 1) == '"') {
            field += '"';
            ++i;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (character == separator && !quoted) {
            fields.append(field);
            field.clear();
        } else {
            field += character;
        }
    }
    fields.append(field);
    return fields;
}

} // namespace

WorkspacePage::WorkspacePage(QWidget* parent) : QWidget(parent) {
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto* topBar = makeSurface("topBar");
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(24, 13, 24, 13);
    auto* workspaceBrand = makeLabel(QStringLiteral("LanCloudDrive"), "brand");
    serverBadge_ = makeLabel(QStringLiteral("Offline"), "statusPill");
    serverBadge_->setProperty("state", "muted");
    accountLabel_ = makeLabel(QString(), "sectionTitle");
    themeButton_ = new QPushButton();
    themeButton_->setObjectName("secondaryButton");
    themeButton_->setToolTip(QStringLiteral("Toggle light or dark theme"));
    auto* logoutButton = new QPushButton("Sign out");
    logoutButton->setObjectName("secondaryButton");
    topLayout->addWidget(workspaceBrand);
    topLayout->addSpacing(12);
    topLayout->addWidget(serverBadge_);
    topLayout->addStretch();
    topLayout->addWidget(accountLabel_);
    topLayout->addSpacing(8);
    topLayout->addWidget(themeButton_);
    topLayout->addSpacing(8);
    topLayout->addWidget(logoutButton);
    pageLayout->addWidget(topBar);

    auto* workspaceBody = new QWidget();
    auto* bodyLayout = new QHBoxLayout(workspaceBody);
    bodyLayout->setContentsMargins(24, 22, 24, 24);
    bodyLayout->setSpacing(16);
    auto* fileSurface = makeSurface();
    auto* fileLayout = new QVBoxLayout(fileSurface);
    fileLayout->setContentsMargins(18, 16, 18, 18);
    fileLayout->setSpacing(12);
    auto* fileHeading = new QHBoxLayout();
    auto* headingColumn = new QVBoxLayout();
    locationLabel_ = makeLabel(QStringLiteral("My files"), "pageTitle");
    itemCount_ = makeLabel(QStringLiteral("0 items"), "muted");
    headingColumn->addWidget(locationLabel_);
    headingColumn->addWidget(itemCount_);
    fileHeading->addLayout(headingColumn);
    fileHeading->addStretch();
    toolbar_ = new QToolBar();
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    upAction_ = toolbar_->addAction(style()->standardIcon(QStyle::SP_ArrowUp), "Up");
    upAction_->setToolTip("Up");
    auto* refreshAction = toolbar_->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh");
    refreshAction->setToolTip("Refresh");
    auto* newFolderAction = toolbar_->addAction(style()->standardIcon(QStyle::SP_DirIcon), "New folder");
    newFolderAction->setToolTip("New folder");
    auto* uploadFileAction = toolbar_->addAction(style()->standardIcon(QStyle::SP_FileIcon), "Upload file");
    uploadFileAction->setToolTip("Upload file");
    auto* uploadFolderAction = toolbar_->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Upload folder");
    uploadFolderAction->setToolTip("Upload folder");
    auto* claimCodeAction = toolbar_->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Extract code");
    claimCodeAction->setToolTip("Extract code");
    fileHeading->addWidget(toolbar_);
    browser_ = new FileBrowser();
    fileLayout->addLayout(fileHeading);
    fileLayout->addWidget(browser_, 1);

    auto* previewColumn = new QWidget();
    auto* sideColumn = new QVBoxLayout(previewColumn);
    sideColumn->setSpacing(12);
    auto* transferSurface = makeSurface("sideSurface");
    auto* transferLayout = new QVBoxLayout(transferSurface);
    transferLayout->setContentsMargins(16, 15, 16, 16);
    transferLayout->setSpacing(8);
    transferLayout->addWidget(makeLabel("Transfers", "sectionTitle"));
    uploadLabel_ = makeLabel("Upload idle", "muted");
    uploadProgress_ = new QProgressBar();
    uploadProgress_->setRange(0, 100);
    downloadLabel_ = makeLabel("Download idle", "muted");
    downloadProgress_ = new QProgressBar();
    downloadProgress_->setRange(0, 100);
    transferLayout->addWidget(uploadLabel_);
    transferLayout->addWidget(uploadProgress_);
    transferLayout->addSpacing(5);
    transferLayout->addWidget(downloadLabel_);
    transferLayout->addWidget(downloadProgress_);

    auto* activitySurface = makeSurface("sideSurface");
    auto* activityLayout = new QVBoxLayout(activitySurface);
    activityLayout->setContentsMargins(16, 15, 16, 16);
    activityLayout->setSpacing(9);
    activityLayout->addWidget(makeLabel("Activity", "sectionTitle"));
    log_ = new QTextEdit();
    log_->setReadOnly(true);
    log_->setPlaceholderText("Recent activity appears here");
    activityLayout->addWidget(log_);

    auto* previewSurface = makeSurface("sideSurface");
    auto* previewLayout = new QVBoxLayout(previewSurface);
    previewLayout->setContentsMargins(16, 15, 16, 16);
    previewLayout->setSpacing(6);
    previewLayout->addWidget(makeLabel("Preview", "sectionTitle"));
    previewTitle_ = makeLabel("Select a file", "muted");
    pdfControls_ = new QWidget();
    auto* pdfControlLayout = new QHBoxLayout(pdfControls_);
    pdfControlLayout->setContentsMargins(0, 0, 0, 0);
    previousPageButton_ = new QPushButton("Previous");
    previousPageButton_->setObjectName("secondaryButton");
    pdfPageLabel_ = makeLabel("Page 1", "muted");
    nextPageButton_ = new QPushButton("Next");
    nextPageButton_->setObjectName("secondaryButton");
    pdfControlLayout->addWidget(previousPageButton_);
    pdfControlLayout->addWidget(pdfPageLabel_, 1, Qt::AlignCenter);
    pdfControlLayout->addWidget(nextPageButton_);
    pdfControls_->hide();
    previewPages_ = new QStackedWidget();
    previewEmpty_ = makeLabel("Select a file to view its contents.", "muted");
    previewEmpty_->setAlignment(Qt::AlignCenter);
    previewText_ = new QTextEdit();
    previewText_->setReadOnly(true);
    previewText_->setPlaceholderText("Select a text or Markdown file to preview its content.");
    previewCode_ = new QPlainTextEdit();
    previewCode_->setReadOnly(true);
    previewCode_->setLineWrapMode(QPlainTextEdit::NoWrap);
    previewCode_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    previewTable_ = new QTableWidget();
    previewTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    previewTable_->setAlternatingRowColors(true);
    previewTable_->horizontalHeader()->setStretchLastSection(true);
    previewImage_ = new QLabel();
    previewImage_->setAlignment(Qt::AlignCenter);
    previewImageScroll_ = new QScrollArea();
    previewImageScroll_->setWidget(previewImage_);
    previewImageScroll_->setWidgetResizable(false);
    previewImageScroll_->setAlignment(Qt::AlignCenter);
    previewPages_->addWidget(previewEmpty_);
    previewPages_->addWidget(previewText_);
    previewPages_->addWidget(previewCode_);
    previewPages_->addWidget(previewTable_);
    previewPages_->addWidget(previewImageScroll_);
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(pdfControls_);
    previewLayout->addWidget(previewPages_, 1);
    sideColumn->addWidget(previewSurface, 5);
    sideColumn->addWidget(transferSurface);
    sideColumn->addWidget(activitySurface, 1);

    auto* contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->addWidget(fileSurface);
    contentSplitter->addWidget(previewColumn);
    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 2);
    contentSplitter->setSizes({690, 460});
    bodyLayout->addWidget(contentSplitter, 1);
    pageLayout->addWidget(workspaceBody, 1);

    connect(themeButton_, &QPushButton::clicked, this, &WorkspacePage::themeToggleRequested);
    connect(logoutButton, &QPushButton::clicked, this, &WorkspacePage::logoutRequested);

    connect(refreshAction, &QAction::triggered, this, [this]() { doList(currentParent_); });
    connect(upAction_, &QAction::triggered, this, [this]() {
        if (parentStack_.isEmpty()) return;
        currentParent_ = parentStack_.takeLast();
        if (!pathNames_.isEmpty()) pathNames_.removeLast();
        updateLocation();
        doList(currentParent_);
    });
    connect(newFolderAction, &QAction::triggered, this, [this]() {
        if (!client_) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New folder", "Folder name:",
                                                    QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty()) client_->mkdir(currentParent_, name);
    });
    connect(uploadFileAction, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, "Select file to upload");
        if (path.isEmpty() || !client_) return;
        uploadProgress_->setValue(0);
        uploadLabel_->setText("Preparing file upload...");
        logMessage(QStringLiteral("Uploading file %1").arg(path));
        client_->upload(path, currentParent_, QString());
    });
    connect(uploadFolderAction, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, "Select folder to upload");
        if (path.isEmpty() || !client_) return;
        uploadProgress_->setValue(0);
        uploadLabel_->setText("Preparing folder upload...");
        logMessage(QStringLiteral("Uploading folder %1").arg(path));
        client_->uploadDirectory(path, currentParent_);
    });
    connect(claimCodeAction, &QAction::triggered, this, [this]() {
        if (!client_) return;
        bool ok = false;
        const QString enteredCode = QInputDialog::getText(this, "Extract shared file",
                                                           "8-character code (for example: A1B2-C3D4):",
                                                           QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        const QString code = normalizedExtractionCode(enteredCode);
        static const QRegularExpression validCode(QStringLiteral("^[0-9A-F]{8}$"));
        if (!validCode.match(code).hasMatch()) {
            QMessageBox::warning(this, "Invalid extraction code",
                                 "Enter the 8-character code created by the sender. "
                                 "Only 0-9 and A-F are allowed; spaces and hyphens are optional.");
            return;
        }
        client_->claimShareCode(code);
    });

    bindFileBrowser();
    updateLocation();
}

void WorkspacePage::bindFileBrowser() {
    connect(browser_, &FileBrowser::enterDirectory, this, [this](qint64 id, const QString& name) {
        parentStack_.append(currentParent_);
        pathNames_.append(name);
        currentParent_ = id;
        updateLocation();
        doList(currentParent_);
    });
    connect(browser_, &FileBrowser::downloadNode, this,
            [this](qint64 id, const QString& name, bool directory) {
        if (!client_) return;
        downloadProgress_->setValue(0);
        if (directory) {
            const QString parent = QFileDialog::getExistingDirectory(
                this, "Choose where to save the folder");
            if (parent.isEmpty()) return;
            downloadLabel_->setText("Preparing folder download...");
            logMessage(QStringLiteral("Downloading folder %1 to %2").arg(name, parent));
            client_->downloadDirectory(id, name, parent);
            return;
        }
        const QString save = QFileDialog::getSaveFileName(this, "Save file as", name);
        if (save.isEmpty()) return;
        downloadLabel_->setText("Preparing file download...");
        logMessage(QStringLiteral("Downloading %1 to %2").arg(name, save));
        client_->download(id, save);
    });
    connect(browser_, &FileBrowser::renameNode, this, [this](qint64 id) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Rename", "New name:",
                                                    QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty() && client_) client_->renameNode(id, name);
    });
    connect(browser_, &FileBrowser::deleteNode, this, [this](qint64 id) {
        if (!client_) return;
        if (QMessageBox::question(this, "Delete item",
                                  "Delete this item from the server? This cannot be undone.")
            == QMessageBox::Yes) {
            client_->deleteNode(id);
        }
    });
    connect(browser_, &FileBrowser::createShareCode, this, [this](qint64 id) {
        if (client_) client_->createShareCode(id);
    });
    connect(browser_, &FileBrowser::nodeSelected, this,
            [this](qint64 id, const QString& name, bool directory, qint64 size) {
        previewTitle_->setText(name);
        if (directory) {
            previewingPdf_ = false;
            pdfControls_->hide();
            previewEmpty_->setText("Folder selected. Double-click to open it.");
            previewPages_->setCurrentWidget(previewEmpty_);
        } else if (client_) {
            previewNodeId_ = id;
            previewPdfPage_ = 1;
            previewingPdf_ = name.endsWith(".pdf", Qt::CaseInsensitive);
            if (visualPreviewable(name)) {
                previewEmpty_->setText(QStringLiteral("Loading visual preview (%1 bytes)...").arg(size));
                previewPages_->setCurrentWidget(previewEmpty_);
                client_->previewAsset(id, name, previewPdfPage_);
            } else {
                pdfControls_->hide();
                previewText_->setPlainText(QStringLiteral("Loading preview (%1 bytes)...").arg(size));
                previewPages_->setCurrentWidget(previewText_);
                client_->preview(id);
            }
        }
    });
    connect(browser_, &FileBrowser::convertToMarkdown, this, [this](qint64 id) {
        if (client_) client_->convertToMarkdown(id);
    });
    connect(browser_, &FileBrowser::refreshRequested, this, [this]() { doList(currentParent_); });
}

void WorkspacePage::setClient(QtClient* client) {
    client_ = client;
    if (!client_) return;

    connect(client_, &QtClient::listReady, this, [this](const QVariantList& entries) {
        browser_->setEntries(entries);
        itemCount_->setText(QStringLiteral("%1 item%2").arg(entries.size())
                                .arg(entries.size() == 1 ? "" : "s"));
        logMessage(QStringLiteral("Folder refreshed: %1 entries").arg(entries.size()));
    });
    connect(client_, &QtClient::mkdirFinished, this, [this](qint64) {
        logMessage("Folder created");
        doList(currentParent_);
    });
    connect(client_, &QtClient::renameFinished, this, [this]() {
        logMessage("Item renamed");
        doList(currentParent_);
    });
    connect(client_, &QtClient::deleteFinished, this, [this]() {
        logMessage("Item deleted");
        doList(currentParent_);
    });
    connect(client_, &QtClient::shareCodeCreated, this, [this](const QString& code) {
        const auto displayCode = code.toUpper();
        const auto groupedCode = QStringLiteral("%1-%2")
                                     .arg(displayCode.left(4), displayCode.mid(4));
        QApplication::clipboard()->setText(displayCode);
        logMessage(QStringLiteral("Extraction code created: %1").arg(displayCode));
        QMessageBox::information(this, "Extraction code",
                                 QStringLiteral("Send this code to the recipient:\n\n%1\n\n"
                                                "It has been copied to the clipboard. It is valid for 24 hours "
                                                "and can be claimed once.")
                                     .arg(groupedCode));
    });
    connect(client_, &QtClient::shareCodeClaimed, this, [this](qint64 nodeId) {
        currentParent_ = 0;
        parentStack_.clear();
        pathNames_.clear();
        updateLocation();
        logMessage(QStringLiteral("Extraction code claimed, new node %1").arg(nodeId));
        QMessageBox::information(this, "File received", "The shared file was added to My files.");
        doList(0);
    });
    connect(client_, &QtClient::previewReady, this,
            [this](const QString& title, const QString& content, bool markdown, bool truncated) {
        previewTitle_->setText(title);
        pdfControls_->hide();
        const auto visibleContent = content + (truncated
            ? QStringLiteral("\n\n[Preview limited to the first 512 KiB]")
            : QString());
        const auto extension = title.section('.', -1).toLower();
        if (codePreviewable(title)) {
            previewCode_->setPlainText(visibleContent);
            previewPages_->setCurrentWidget(previewCode_);
        } else if (extension == "csv" || extension == "tsv") {
            const auto rows = visibleContent.split('\n', Qt::SkipEmptyParts);
            const auto separator = extension == "tsv" ? QChar('\t') : QChar(',');
            const auto headers = rows.isEmpty() ? QList<QString>{} : splitTableRow(rows.first(), separator);
            const int columnCount = headers.size() > 50 ? 50 : static_cast<int>(headers.size());
            previewTable_->clear();
            previewTable_->setColumnCount(columnCount);
            const int rowCount = rows.size() > 1001 ? 1000
                                                    : static_cast<int>(rows.size() > 0 ? rows.size() - 1 : 0);
            previewTable_->setRowCount(rowCount);
            for (int column = 0; column < columnCount; ++column)
                previewTable_->setHorizontalHeaderItem(column, new QTableWidgetItem(headers.at(column)));
            for (int row = 0; row < previewTable_->rowCount(); ++row) {
                const auto fields = splitTableRow(rows.at(row + 1), separator);
                const int fieldCount = fields.size() > columnCount ? columnCount : static_cast<int>(fields.size());
                for (int column = 0; column < fieldCount; ++column)
                    previewTable_->setItem(row, column, new QTableWidgetItem(fields.at(column)));
            }
            previewPages_->setCurrentWidget(previewTable_);
        } else {
            if (markdown) previewText_->setMarkdown(visibleContent);
            else previewText_->setPlainText(visibleContent);
            previewPages_->setCurrentWidget(previewText_);
        }
        if (markdown) logMessage(QStringLiteral("Markdown preview loaded: %1").arg(title));
    });
    connect(client_, &QtClient::previewAssetReady, this,
            [this](const QString& title, const QByteArray& bytes) {
        QPixmap pixmap;
        if (!pixmap.loadFromData(bytes)) {
            previewTitle_->setText(title);
            previewText_->setPlainText("The server returned an unreadable preview image.");
            previewPages_->setCurrentWidget(previewText_);
            return;
        }
        previewTitle_->setText(title);
        const auto scaled = pixmap.scaled(previewImageScroll_->viewport()->size(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
        previewImage_->setPixmap(scaled);
        previewImage_->resize(scaled.size());
        previewPages_->setCurrentWidget(previewImageScroll_);
        pdfControls_->setVisible(previewingPdf_);
        previousPageButton_->setEnabled(previewPdfPage_ > 1);
        nextPageButton_->setEnabled(true);
        pdfPageLabel_->setText(QStringLiteral("Page %1").arg(previewPdfPage_));
    });
    connect(client_, &QtClient::markdownSaved, this, [this](qint64 nodeId) {
        logMessage(QStringLiteral("Markdown file saved, node %1").arg(nodeId));
        doList(currentParent_);
    });
    connect(client_, &QtClient::uploadProgress, this, [this](qint64 done, qint64 total) {
        const int percent = progressPercent(done, total);
        uploadProgress_->setValue(percent);
        uploadLabel_->setText(QStringLiteral("Uploading · %1%").arg(percent));
    });
    connect(client_, &QtClient::uploadFinished, this, [this](qint64 nodeId) {
        uploadProgress_->setValue(100);
        uploadLabel_->setText("Upload complete");
        logMessage(QStringLiteral("Upload finished, node %1").arg(nodeId));
        doList(currentParent_);
    });
    connect(client_, &QtClient::downloadProgress, this, [this](qint64 done, qint64 total) {
        const int percent = progressPercent(done, total);
        downloadProgress_->setValue(percent);
        downloadLabel_->setText(QStringLiteral("Downloading · %1%").arg(percent));
    });
    connect(client_, &QtClient::downloadFinished, this, [this]() {
        downloadProgress_->setValue(100);
        downloadLabel_->setText("Download complete");
        logMessage("Download finished");
    });
    connect(client_, &QtClient::errorOccurred, this,
            [this](const QString& code, const QString&) {
        if (previewingPdf_ && code == "BAD_REQUEST" && previewPdfPage_ > 1) {
            --previewPdfPage_;
            pdfPageLabel_->setText(QStringLiteral("Page %1").arg(previewPdfPage_));
            nextPageButton_->setEnabled(false);
        }
    });
}

void WorkspacePage::setAccount(const QString& host, const QString& port, const QString& username) {
    accountLabel_->setText(username);
    setServerOnline(host, port);
}

void WorkspacePage::setServerOnline(const QString& host, const QString& port) {
    serverBadge_->setText(QStringLiteral("%1:%2").arg(host, port));
    serverBadge_->setProperty("state", "ok");
    serverBadge_->style()->unpolish(serverBadge_);
    serverBadge_->style()->polish(serverBadge_);
}

void WorkspacePage::setServerOffline() {
    serverBadge_->setText(QStringLiteral("Offline"));
    serverBadge_->setProperty("state", "muted");
    serverBadge_->style()->unpolish(serverBadge_);
    serverBadge_->style()->polish(serverBadge_);
}

void WorkspacePage::setDark(bool dark) {
    themeButton_->setText(themeToggleLabel(dark));
}

void WorkspacePage::refresh() {
    currentParent_ = 0;
    parentStack_.clear();
    pathNames_.clear();
    updateLocation();
    doList(0);
}

void WorkspacePage::clear() {
    browser_->setEntries({});
}

void WorkspacePage::logMessage(const QString& message) {
    log_->append(message);
}

void WorkspacePage::logError(const QString& code, const QString& message) {
    log_->append(QStringLiteral("ERROR [%1] %2").arg(code, message));
}

void WorkspacePage::doList(qint64 parentId) {
    if (!client_) return;
    logMessage(QStringLiteral("Refreshing folder %1...").arg(parentId));
    client_->list(parentId);
}

void WorkspacePage::updateLocation() {
    locationLabel_->setText(pathNames_.isEmpty()
        ? QStringLiteral("My files")
        : QStringLiteral("My files / %1").arg(pathNames_.join(" / ")));
    upAction_->setEnabled(!parentStack_.isEmpty());
}

} // namespace client_qt
