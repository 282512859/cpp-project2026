#include "client_qt/LoginPage.h"
#include "client_qt/Theme.h"
#include "client_qt/WorkspacePage.h"
#include "cloud/client/QtClient.h"

#include <QApplication>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWidget>

using namespace client_qt;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("LanCloudDrive");
    app.setOrganizationName("cpp-project2026");
    app.setStyle(QStyleFactory::create("Fusion"));

    QSettings settings;
    bool dark = settings.value("ui/dark", false).toBool();

    QWidget window;
    window.setWindowTitle("LanCloudDrive");
    window.setMinimumSize(1040, 700);
    window.resize(1240, 790);

    auto* pages = new QStackedWidget();
    auto* loginPage = new LoginPage(&window);
    auto* workspacePage = new WorkspacePage(&window);
    pages->addWidget(loginPage);
    pages->addWidget(workspacePage);
    pages->setCurrentWidget(loginPage);

    auto* windowLayout = new QVBoxLayout(&window);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->addWidget(pages);

    const auto applyTheme = [&]() {
        app.setStyleSheet(themeStyleSheet(dark));
        loginPage->setDark(dark);
        workspacePage->setDark(dark);
        settings.setValue("ui/dark", dark);
    };
    applyTheme();

    const auto toggleTheme = [&]() {
        dark = !dark;
        applyTheme();
    };
    QObject::connect(loginPage, &LoginPage::themeToggleRequested, &window, toggleTheme);
    QObject::connect(workspacePage, &WorkspacePage::themeToggleRequested, &window, toggleTheme);

    cloud::client::QtClient* client = nullptr;

    const auto connectClient = [&](cloud::client::QtClient* c) {
        QObject::connect(c, &cloud::client::QtClient::registerFinished, &window, [&]() {
            loginPage->setStatus("Account created. Sign in with the same credentials.",
                                 LoginPage::Status::Ok);
        });
        QObject::connect(c, &cloud::client::QtClient::loginFinished, &window, [&]() {
            workspacePage->setAccount(loginPage->host(), loginPage->port(), loginPage->username());
            pages->setCurrentWidget(workspacePage);
            workspacePage->refresh();
            loginPage->setStatus("Signed in.", LoginPage::Status::Ok);
        });
        QObject::connect(c, &cloud::client::QtClient::logoutFinished, &window, [&]() {
            workspacePage->clear();
            workspacePage->setServerOffline();
            pages->setCurrentWidget(loginPage);
            loginPage->setStatus("Signed out. Sign in to refresh another account.",
                                 LoginPage::Status::Info);
        });
        QObject::connect(c, &cloud::client::QtClient::errorOccurred, &window,
                         [&](const QString& code, const QString& message) {
            if (pages->currentWidget() == loginPage) {
                loginPage->setStatus(QStringLiteral("%1: %2").arg(code, message),
                                     LoginPage::Status::Error);
            } else {
                workspacePage->logError(code, message);
            }
        });
    };

    const auto rebuildClient = [&](const QString& host, quint16 port) {
        if (client) {
            delete client;
            client = nullptr;
        }
        settings.setValue("server/host", host);
        settings.setValue("server/port", QString::number(port));
        client = new cloud::client::QtClient(host, port, &window);
        workspacePage->setClient(client);
        connectClient(client);
        loginPage->setStatus(QStringLiteral("Connecting to %1:%2...").arg(host).arg(port),
                             LoginPage::Status::Info);
    };

    QObject::connect(loginPage, &LoginPage::loginRequested, &window,
                     [&](const QString& host, quint16 port, const QString& user, const QString& pass) {
        rebuildClient(host, port);
        client->login(user, pass);
    });
    QObject::connect(loginPage, &LoginPage::registerRequested, &window,
                     [&](const QString& host, quint16 port, const QString& user, const QString& pass) {
        rebuildClient(host, port);
        client->registerUser(user, pass);
    });
    QObject::connect(workspacePage, &WorkspacePage::logoutRequested, &window, [&]() {
        if (client) client->logout();
    });

    window.show();
    return app.exec();
}
