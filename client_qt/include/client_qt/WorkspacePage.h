#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QPlainTextEdit;
class QTableWidget;
class QScrollArea;
class QToolBar;
class QAction;
class FileBrowser;

namespace cloud::client { class QtClient; }

namespace client_qt {

// The authenticated file workspace: top bar, file browser, and the right-hand
// preview / transfer / activity column. It connects directly to a QtClient and
// keeps all browsing state (current directory, navigation stack, preview page)
// internal to this widget.
class WorkspacePage : public QWidget {
    Q_OBJECT
public:
    explicit WorkspacePage(QWidget* parent = nullptr);

    void setClient(cloud::client::QtClient* client);
    void setAccount(const QString& host, const QString& port, const QString& username);
    void setServerOnline(const QString& host, const QString& port);
    void setServerOffline();
    void setDark(bool dark);

    // Reset to the root directory and refresh; called after a successful login.
    void refresh();
    // Clear the file list; called after logout.
    void clear();

    void logMessage(const QString& message);
    void logError(const QString& code, const QString& message);

signals:
    void logoutRequested();
    void themeToggleRequested();

private:
    void doList(qint64 parentId);
    void updateLocation();
    void bindFileBrowser();

    cloud::client::QtClient* client_ = nullptr;
    FileBrowser* browser_ = nullptr;

    QLabel* locationLabel_ = nullptr;
    QLabel* itemCount_ = nullptr;
    QLabel* accountLabel_ = nullptr;
    QLabel* serverBadge_ = nullptr;
    QPushButton* themeButton_ = nullptr;

    QToolBar* toolbar_ = nullptr;
    QAction* upAction_ = nullptr;

    QLabel* uploadLabel_ = nullptr;
    QLabel* downloadLabel_ = nullptr;
    QProgressBar* uploadProgress_ = nullptr;
    QProgressBar* downloadProgress_ = nullptr;

    QTextEdit* log_ = nullptr;

    QLabel* previewTitle_ = nullptr;
    QWidget* pdfControls_ = nullptr;
    QLabel* pdfPageLabel_ = nullptr;
    QPushButton* previousPageButton_ = nullptr;
    QPushButton* nextPageButton_ = nullptr;
    QStackedWidget* previewPages_ = nullptr;
    QLabel* previewEmpty_ = nullptr;
    QTextEdit* previewText_ = nullptr;
    QPlainTextEdit* previewCode_ = nullptr;
    QTableWidget* previewTable_ = nullptr;
    QLabel* previewImage_ = nullptr;
    QScrollArea* previewImageScroll_ = nullptr;

    qint64 currentParent_ = 0;
    QVector<qint64> parentStack_;
    QStringList pathNames_;
    qint64 previewNodeId_ = 0;
    qint64 previewPdfPage_ = 1;
    bool previewingPdf_ = false;
    bool previewPending_ = false;
};

} // namespace client_qt
