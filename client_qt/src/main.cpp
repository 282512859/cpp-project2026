#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QWidget>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QInputDialog>

#include "cloud/client/QtClient.h"
#include "client_qt/FileBrowser.h"

using namespace cloud::client;

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("LanCloudDrive - Qt Demo");
    QVBoxLayout *mainLayout = new QVBoxLayout(&window);

    // connection controls
    QHBoxLayout *connLayout = new QHBoxLayout();
    QLabel *hostLabel = new QLabel("Server:");
    QLineEdit *hostEdit = new QLineEdit("127.0.0.1");
    QLabel *portLabel = new QLabel("Port:");
    QLineEdit *portEdit = new QLineEdit("9000");
    QLineEdit *userEdit = new QLineEdit(); userEdit->setPlaceholderText("username");
    QLineEdit *passEdit = new QLineEdit(); passEdit->setPlaceholderText("password"); passEdit->setEchoMode(QLineEdit::Password);
    QPushButton *connectBtn = new QPushButton("Connect/Login");

    connLayout->addWidget(hostLabel);
    connLayout->addWidget(hostEdit);
    connLayout->addWidget(portLabel);
    connLayout->addWidget(portEdit);
    connLayout->addWidget(userEdit);
    connLayout->addWidget(passEdit);
    connLayout->addWidget(connectBtn);

    // toolbar
    QToolBar *toolbar = new QToolBar();
    QAction *refreshAct = toolbar->addAction("Refresh");
    QAction *upAct = toolbar->addAction("Up");
    QAction *uploadAct = toolbar->addAction("Upload");

    // file browser and log
    FileBrowser *browser = new FileBrowser();
    QTextEdit *log = new QTextEdit(); log->setReadOnly(true);

    mainLayout->addLayout(connLayout);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(browser, 1);
    mainLayout->addWidget(log, 1);

    window.setLayout(mainLayout);
    window.resize(800, 600);

    // Client and navigation state
    QtClient *client = new QtClient(hostEdit->text(), static_cast<quint16>(portEdit->text().toUShort()), &window);
    qint64 currentParent = 0;
    QVector<qint64> parentStack;

    auto doList = [&](qint64 parentId){
        log->append(QStringLiteral("Listing %1...").arg(parentId));
        client->list(parentId);
    };

    // connect actions
    QObject::connect(connectBtn, &QPushButton::clicked, [&]() {
        QString host = hostEdit->text();
        quint16 port = static_cast<quint16>(portEdit->text().toUShort());
        static QString lastHost = host; static quint16 lastPort = port;
        if(host!=lastHost || port!=lastPort) {
            delete client;
            client = new QtClient(host, port, &window);
            lastHost = host; lastPort = port;
        }
        log->append("Logging in...");
        client->login(userEdit->text(), passEdit->text());
    });

    QObject::connect(refreshAct, &QAction::triggered, [&](){ doList(currentParent); });
    QObject::connect(upAct, &QAction::triggered, [&](){
        if(parentStack.isEmpty()) { log->append("Already at root"); return; }
        currentParent = parentStack.takeLast();
        doList(currentParent);
    });
    QObject::connect(uploadAct, &QAction::triggered, [&](){
        QString path = QFileDialog::getOpenFileName(&window, "Select file to upload");
        if(path.isEmpty()) return;
        log->append(QStringLiteral("Uploading %1 to %2").arg(path, QString::number(currentParent)));
        client->upload(path, currentParent, QString());
    });

    // FileBrowser interactions
    QObject::connect(browser, &FileBrowser::enterDirectory, [&](qint64 id){
        parentStack.append(currentParent);
        currentParent = id;
        doList(currentParent);
    });
    QObject::connect(browser, &FileBrowser::downloadNode, [&](qint64 id){
        QString save = QFileDialog::getSaveFileName(&window, "Save file as");
        if(save.isEmpty()) return;
        log->append(QStringLiteral("Downloading %1 to %2").arg(QString::number(id), save));
        client->download(id, save);
    });
    QObject::connect(browser, &FileBrowser::renameNode, [&](qint64 id){
        bool ok=false; QString name = QInputDialog::getText(&window, "Rename", "New name:", QLineEdit::Normal, {}, &ok);
        if(ok && !name.isEmpty()){
            client->renameNode(id, name);
            doList(currentParent);
        }
    });
    QObject::connect(browser, &FileBrowser::deleteNode, [&](qint64 id){
        client->deleteNode(id);
        doList(currentParent);
    });
    QObject::connect(browser, &FileBrowser::refreshRequested, [&](){ doList(currentParent); });

    // Client signals
    QObject::connect(client, &QtClient::errorOccurred, [&](const QString &code, const QString &message){
        log->append(QStringLiteral("ERROR [%1] %2").arg(code, message));
    });
    QObject::connect(client, &QtClient::listReady, [&](const QVariantList &entries){
        browser->setEntries(entries);
        log->append(QStringLiteral("List received: %1 entries").arg(entries.size()));
    });
    QObject::connect(client, &QtClient::uploadProgress, [&](qint64 done, qint64 total){
        log->append(QStringLiteral("Upload: %1/%2").arg(done).arg(total));
    });
    QObject::connect(client, &QtClient::uploadFinished, [&](qint64 nodeId){
        log->append(QStringLiteral("Upload finished, nodeId=%1").arg(nodeId));
        doList(currentParent);
    });
    QObject::connect(client, &QtClient::downloadProgress, [&](qint64 done, qint64 total){
        log->append(QStringLiteral("Download: %1/%2").arg(done).arg(total));
    });
    QObject::connect(client, &QtClient::downloadFinished, [&](){ log->append("Download finished"); });

    // initial list root
    doList(0);

    window.show();
    return app.exec();
}
