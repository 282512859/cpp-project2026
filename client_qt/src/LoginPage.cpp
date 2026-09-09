#include "client_qt/LoginPage.h"
#include "client_qt/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

namespace client_qt {

namespace {

QFrame* makeSurface(const char* name) {
    auto* surface = new QFrame();
    surface->setObjectName(name);
    return surface;
}

QLabel* makeLabel(const QString& text, const char* name) {
    auto* label = new QLabel(text);
    label->setObjectName(name);
    return label;
}

const char* statusProperty(LoginPage::Status status) {
    switch (status) {
    case LoginPage::Status::Ok: return "ok";
    case LoginPage::Status::Warn: return "warn";
    case LoginPage::Status::Error: return "error";
    case LoginPage::Status::Info: return "info";
    }
    return "info";
}

} // namespace

LoginPage::LoginPage(QWidget* parent) : QWidget(parent) {
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(52, 34, 52, 52);

    auto* brandRow = new QHBoxLayout();
    brandRow->addWidget(makeLabel(QStringLiteral("LanCloudDrive"), "brand"));
    brandRow->addStretch();
    brandRow->addWidget(makeLabel(QStringLiteral("LAN file workspace"), "statusPill"));
    themeButton_ = new QPushButton();
    themeButton_->setObjectName("secondaryButton");
    themeButton_->setToolTip(QStringLiteral("Toggle light or dark theme"));
    brandRow->addSpacing(8);
    brandRow->addWidget(themeButton_);
    pageLayout->addLayout(brandRow);
    pageLayout->addStretch();

    auto* content = new QWidget();
    content->setMaximumWidth(960);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(64);

    auto* intro = makeSurface("introPanel");
    auto* introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(0, 12, 20, 12);
    introLayout->setSpacing(12);
    introLayout->addWidget(makeLabel("PRIVATE CLOUD · LOCAL NETWORK", "eyebrow"));
    introLayout->addWidget(makeLabel("Files for your team,\nkept on your network.", "loginTitle"));
    auto* introText = makeLabel(
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
    for (const auto& feature : features) {
        auto* row = new QHBoxLayout();
        auto* icon = makeLabel(QStringLiteral("\u2713"), "featureIcon");
        icon->setFixedSize(32, 32);
        icon->setAlignment(Qt::AlignCenter);
        auto* textLabel = new QLabel(feature);
        textLabel->setWordWrap(true);
        row->addWidget(icon);
        row->addSpacing(8);
        row->addWidget(textLabel, 1);
        introLayout->addLayout(row);
    }
    introLayout->addStretch();

    QSettings settings;
    auto* form = makeSurface("loginForm");
    form->setFixedWidth(390);
    auto* formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(30, 30, 30, 30);
    formLayout->setSpacing(10);
    formLayout->addWidget(makeLabel("Sign in", "pageTitle"));
    formLayout->addWidget(makeLabel("Connect to your LanCloudDrive server", "muted"));
    formLayout->addSpacing(10);
    formLayout->addWidget(makeLabel("Server", "sectionTitle"));
    auto* serverRow = new QHBoxLayout();
    hostEdit_ = new QLineEdit(settings.value("server/host", "127.0.0.1").toString());
    hostEdit_->setPlaceholderText("Server IP address");
    portEdit_ = new QLineEdit(settings.value("server/port", "9000").toString());
    portEdit_->setPlaceholderText("Port");
    portEdit_->setMaximumWidth(88);
    serverRow->addWidget(hostEdit_, 1);
    serverRow->addWidget(portEdit_);
    formLayout->addLayout(serverRow);
    formLayout->addWidget(makeLabel("Account", "sectionTitle"));
    userEdit_ = new QLineEdit();
    userEdit_->setPlaceholderText("Username");
    passEdit_ = new QLineEdit();
    passEdit_->setPlaceholderText("Password");
    passEdit_->setEchoMode(QLineEdit::Password);
    auto* loginButton = new QPushButton("Sign in");
    auto* registerButton = new QPushButton("Create account");
    registerButton->setObjectName("secondaryButton");
    statusLabel_ = makeLabel(
        "Use this computer's LAN IP when signing in from another device.", "muted");
    statusLabel_->setProperty("state", "info");
    statusLabel_->setWordWrap(true);
    formLayout->addWidget(userEdit_);
    formLayout->addWidget(passEdit_);
    formLayout->addSpacing(4);
    formLayout->addWidget(loginButton);
    formLayout->addWidget(registerButton);
    formLayout->addSpacing(6);
    formLayout->addWidget(statusLabel_);
    contentLayout->addWidget(intro, 1);
    contentLayout->addWidget(form);
    pageLayout->addWidget(content, 0, Qt::AlignHCenter);
    pageLayout->addStretch();

    connect(themeButton_, &QPushButton::clicked, this, &LoginPage::themeToggleRequested);

    const auto ensureValid = [this](bool& ok) {
        if (hostEdit_->text().trimmed().isEmpty() || portEdit_->text().toUShort() == 0 ||
            userEdit_->text().trimmed().isEmpty() || passEdit_->text().isEmpty()) {
            setStatus("Enter a server address, username, and password.", Status::Error);
            ok = false;
            return;
        }
        ok = true;
    };

    connect(loginButton, &QPushButton::clicked, this, [this, ensureValid]() {
        bool ok = false;
        ensureValid(ok);
        if (!ok) return;
        emit loginRequested(hostEdit_->text().trimmed(),
                            static_cast<quint16>(portEdit_->text().toUShort()),
                            userEdit_->text(), passEdit_->text());
    });

    connect(registerButton, &QPushButton::clicked, this, [this, ensureValid]() {
        bool ok = false;
        ensureValid(ok);
        if (!ok) return;
        emit registerRequested(hostEdit_->text().trimmed(),
                               static_cast<quint16>(portEdit_->text().toUShort()),
                               userEdit_->text(), passEdit_->text());
    });

    connect(passEdit_, &QLineEdit::returnPressed, loginButton, &QPushButton::click);
}

void LoginPage::setStatus(const QString& text, Status status) {
    statusLabel_->setText(text);
    statusLabel_->setProperty("state", statusProperty(status));
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
}

void LoginPage::setDark(bool dark) {
    themeButton_->setText(themeToggleLabel(dark));
}

QString LoginPage::host() const { return hostEdit_->text().trimmed(); }
QString LoginPage::port() const { return portEdit_->text().trimmed(); }
QString LoginPage::username() const { return userEdit_->text().trimmed(); }
QString LoginPage::password() const { return passEdit_->text(); }

} // namespace client_qt
