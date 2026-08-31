#include "client_qt/MainWindow.h"
#include "client_qt/FileBrowser.h"
#include "client_qt/TransferManager.h"
#include "cloud/client/QtClient.h"

#include <QToolBar>
#include <QAction>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDockWidget>
#include <QInputDialog>
#include <QFileDialog>

using namespace cloud::client;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto central = new QWidget(this);
    auto layout = new QVBoxLayout(central);

    // connection row
    auto connLayout = new QHBoxLayout();
    auto hostEdit = new QLineEdit("127.0.0.1");
    auto portEdit = new QLineEdit("9000");
    auto userEdit = new QLineEdit(); userEdit->setPlaceholderText("username");
    auto passEdit = new QLineEdit(); passEdit->setPlaceholderText("password"); passEdit->setEchoMode(QLineEdit::Password);
    auto loginBtn = new QPushButton("Connect/Login");
    connLayout->addWidget(hostEdit); connLayout->addWidget(portEdit); connLayout->addWidget(userEdit); connLayout->addWidget(passEdit); connLayout->addWidget(loginBtn);

    layout->addLayout(connLayout);

    // toolbar
    auto toolbar = addToolBar("Main");
    auto refreshAct = toolbar->addAction("Refresh");
    auto upAct = toolbar->addAction("Up");
    auto uploadAct = toolbar->addAction("Upload");

    // browser
    browser_ = new FileBrowser();
    layout->addWidget(browser_);

    central->setLayout(layout);
    setCentralWidget(central);

    // transfers dock
    transfers_ = new TransferManager();
    auto dock = new QDockWidget("Transfers", this);
    dock->setWidget(transfers_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    // client
    client_ = new QtClient(hostEdit->text(), static_cast<quint16>(portEdit->text().toUShort()), this);

    // connections
    connect(loginBtn, &QPushButton::clicked, this, [=]() {
        client_->login(userEdit->text(), passEdit->text());
    });

    connect(refreshAct, &QAction::triggered, this, [=](){ client_->list(currentParent_); });
    connect(upAct, &QAction::triggered, this, [=](){ /* pop stack omitted for brevity */ client_->list(0); });
    connect(uploadAct, &QAction::triggered, this, [=](){
        QString path = QFileDialog::getOpenFileName(this, "Select file to upload");
        if(path.isEmpty()) return;
        client_->upload(path, currentParent_, QString());
    });

    connect(browser_, &FileBrowser::enterDirectory, this, [&](qint64 id){ currentParent_ = id; client_->list(currentParent_); });
    connect(browser_, &FileBrowser::downloadNode, this, [&](qint64 id){
        QString save = QFileDialog::getSaveFileName(this, "Save file as");
        if(save.isEmpty()) return;
        client_->download(id, save);
    });

    connect(client_, &QtClient::listReady, this, [&](const QVariantList &entries){ browser_->setEntries(entries); });
    connect(client_, &QtClient::uploadProgress, transfers_, &TransferManager::updateUploadProgress);
    connect(client_, &QtClient::downloadProgress, transfers_, &TransferManager::updateDownloadProgress);
    connect(client_, &QtClient::uploadFinished, transfers_, &TransferManager::markUploadFinished);
    connect(client_, &QtClient::downloadFinished, transfers_, &TransferManager::markDownloadFinished);
}

MainWindow::~MainWindow() {}

void MainWindow::onLoginRequested(const QString &host, quint16 port, const QString &user, const QString &pass) {
    // not used in this simplified example
}

void MainWindow::onListRequested(qint64 parentId) {
    client_->list(parentId);
}
