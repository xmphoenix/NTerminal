#ifndef NETWORKSERVICESDIALOG_H
#define NETWORKSERVICESDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <functional>

class ServerService;
class QStackedWidget;
class QFrame;
class QLabel;
class QPushButton;
class QToolButton;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPlainTextEdit;

// ============================================================================
// 网络服务对话框（工具 → 网络服务）
//
// 左侧列出本机可启动的 TFTP/FTP/HTTP/Telnet/NFS 服务，右侧是该服务的
// 配置与运行日志。服务对象来自 ServerManager（每种服务全进程只有一个）：
// 对话框反复打开/关闭、反复启动停止，都不会产生新实例，运行状态与配置
// 在窗口关闭后继续保留。
// ============================================================================
class NetworkServicesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NetworkServicesDialog(QWidget *parent = nullptr);

private:
    struct Row {
        ServerService *service = nullptr;
        QFrame *frame = nullptr;
        QLabel *status = nullptr;
        QPushButton *toggle = nullptr;
    };

    struct Page {
        ServerService *service = nullptr;
        QWidget *page = nullptr;
        QLabel *statusChip = nullptr;
        QLineEdit *rootEdit = nullptr;
        QPushButton *browseButton = nullptr;
        QSpinBox *portSpin = nullptr;
        QCheckBox *option1 = nullptr;      // TFTP: 允许上传 / HTTP: 目录浏览 / FTP: 允许匿名
        QCheckBox *option2 = nullptr;      // FTP: 允许写入
        QLineEdit *userEdit = nullptr;
        QLineEdit *passwordEdit = nullptr;
        QLabel *userLabel = nullptr;
        QLabel *passwordLabel = nullptr;
        QPushButton *startButton = nullptr;
        QPushButton *stopButton = nullptr;
        QPlainTextEdit *log = nullptr;
        std::function<QJsonObject()> collect;
        std::function<void(const QJsonObject &)> load;
        std::function<void(bool)> setEditable;
    };

    void buildUi();
    QWidget *buildSidebar();
    QFrame *buildRow(ServerService *service);
    QWidget *buildPage(ServerService *service);
    void selectService(int index);
    void refreshService(ServerService *service);
    void refreshAll();
    void startService(const Page &page);
    void stopService(const Page &page);
    void appendLog(ServerService *service, const QString &line);
    Page *pageFor(ServerService *service);

    QList<Row> m_rows;
    QList<Page> m_pages;
    QStackedWidget *m_stack = nullptr;
    int m_selected = 0;
};

#endif // NETWORKSERVICESDIALOG_H
