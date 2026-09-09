#pragma once

#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;

namespace client_qt {

// Standalone authentication page. It owns only its own widgets and emits
// high-level signals; the coordinator in main.cpp decides when to (re)build the
// client connection and which page to show. Status text is driven via the
// "state" dynamic property so it follows the active theme.
class LoginPage : public QWidget {
    Q_OBJECT
public:
    enum class Status { Info, Ok, Warn, Error };

    explicit LoginPage(QWidget* parent = nullptr);

    void setStatus(const QString& text, Status status = Status::Info);
    void setDark(bool dark);

    QString host() const;
    QString port() const;
    QString username() const;
    QString password() const;

signals:
    void loginRequested(const QString& host, quint16 port,
                        const QString& username, const QString& password);
    void registerRequested(const QString& host, quint16 port,
                           const QString& username, const QString& password);
    void themeToggleRequested();

private:
    QLineEdit* hostEdit_ = nullptr;
    QLineEdit* portEdit_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* themeButton_ = nullptr;
};

} // namespace client_qt
