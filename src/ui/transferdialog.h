#ifndef TRANSFERDIALOG_H
#define TRANSFERDIALOG_H

#include <QDialog>
#include <QElapsedTimer>

class FileTransfer;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QTimer;

// ============================================================================
// 协议传输进度窗口（ZMODEM/YMODEM/XMODEM/Kermit/ASCII 共用）
//
// 模态显示：传输期间终端不接受键盘输入，避免误击键混入协议字节流。
// 成功时自动关闭；失败时保留错误信息，等待用户关闭。
// ============================================================================
class TransferDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransferDialog(FileTransfer *transfer, QWidget *parent = nullptr);

protected:
    void reject() override;

private:
    void updateDetails();
    void appendLog(const QString &line);
    void onFinished(bool success, const QString &message);

    FileTransfer *m_transfer = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_details = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_speedTimer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastBytes = 0;
    qint64 m_lastTickMs = 0;
    double m_speed = 0;
};

#endif // TRANSFERDIALOG_H
