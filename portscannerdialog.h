#ifndef PORTSCANNERDIALOG_H
#define PORTSCANNERDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <QTcpSocket>
#include <QTimer>
#include <QList>

class PortScannerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PortScannerDialog(QWidget *parent = nullptr);
    ~PortScannerDialog();

private slots:
    void onStartScan();
    void onStopScan();
    void onPingHost();
    void onSocketConnected();
    void onSocketError();
    void onSocketTimeout();
    void scanNextPort();

private:
    void setupUi();
    void startPortScan();
    void stopPortScan();
    void appendLog(const QString &message, const QString &color = "black");
    void pingHost(const QString &host);
    QString getServiceName(int port);

    QLineEdit *m_hostEdit;
    QSpinBox *m_startPortSpin;
    QSpinBox *m_endPortSpin;
    QCheckBox *m_commonPortsCheck;
    QTextEdit *m_logText;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QPushButton *m_pingButton;
    QProgressBar *m_progressBar;

    QTcpSocket *m_socket;
    QTimer *m_timeoutTimer;

    QString m_currentHost;
    QList<int> m_portsToScan;
    int m_currentPortIndex;
    int m_openPortCount;
    bool m_scanning;

    static const QList<int> COMMON_PORTS;
};

#endif // PORTSCANNERDIALOG_H
