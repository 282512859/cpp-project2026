#include <QApplication>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QWidget>

#include "cloud/client/QtClient.h"

using namespace cloud::client;

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("LanCloudDrive - Qt Demo");
    QVBoxLayout *layout = new QVBoxLayout(&window);

    QLabel *hostLabel = new QLabel("Server host:");
    QLineEdit *hostEdit = new QLineEdit("127.0.0.1");
    QLabel *portLabel = new QLabel("Port:");
    QLineEdit *portEdit = new QLineEdit("9000");

    QLineEdit *userEdit = new QLineEdit();
    userEdit->setPlaceholderText("username");
    QLineEdit *passEdit = new QLineEdit();
    passEdit->setPlaceholderText("password");
    passEdit->setEchoMode(QLineEdit::Password);

    QPushButton *connectBtn = new QPushButton("Connect/Login");
    QPushButton *listBtn = new QPushButton("List Root");

    QTextEdit *log = new QTextEdit();
    log->setReadOnly(true);

    layout->addWidget(hostLabel);
    layout->addWidget(hostEdit);
    layout->addWidget(portLabel);
    layout->addWidget(portEdit);
    layout->addWidget(userEdit);
    layout->addWidget(passEdit);
    layout->addWidget(connectBtn);
    layout->addWidget(listBtn);
    layout->addWidget(log);

    window.setLayout(layout);
    window.resize(480, 600);

    // Create QtClient with default host/port; will be recreated when Connect pressed
    QtClient *client = new QtClient(hostEdit->text(), static_cast<quint16>(portEdit->text().toUShort()), &window);

    QObject::connect(connectBtn, &QPushButton::clicked, [&]() {
        QString host = hostEdit->text();
        quint16 port = static_cast<quint16>(portEdit->text().toUShort());
        // Recreate client if host/port changed
        static QString lastHost = host; static quint16 lastPort = port;
        if(host!=lastHost || port!=lastPort) {
            delete client;
            client = new QtClient(host, port, &window);
            lastHost = host; lastPort = port;
        }
        log->append("Logging in...");
        client->login(userEdit->text(), passEdit->text());
    });

    QObject::connect(listBtn, &QPushButton::clicked, [&]() {
        log->append("Requesting root list...");
        client->list(0);
    });

    QObject::connect(client, &QtClient::errorOccurred, [&](const QString &code, const QString &message){
        log->append(QStringLiteral("ERROR [%1] %2").arg(code, message));
    });

    QObject::connect(client, &QtClient::listReady, [&](const QVariantList &entries){
        log->append("List result:");
        for(const auto &v: entries) {
            const auto m = v.toMap();
            log->append(QString::asprintf("%lld: %s %s", m.value("id").toLongLong(), m.value("name").toString().toUtf8().constData(), m.value("directory").toBool()?"[DIR]":"[FILE]"));
        }
    });

    window.show();
    return app.exec();
}
