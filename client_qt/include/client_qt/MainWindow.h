#pragma once

#include <QMainWindow>
#include <memory>

namespace cloud::client { class QtClient; }
class FileBrowser;
class TransferManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onLoginRequested(const QString &host, quint16 port, const QString &user, const QString &pass);
    void onListRequested(qint64 parentId);

private:
    cloud::client::QtClient *client_ = nullptr;
    FileBrowser *browser_ = nullptr;
    TransferManager *transfers_ = nullptr;
    qint64 currentParent_ = 0;
};
