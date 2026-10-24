#ifndef RDPWIDGET_H
#define RDPWIDGET_H

#include <QWidget>

#include "common/connectionsettings.h"

class QAxWidget;
class QLabel;
class QShowEvent;

// 内嵌 Microsoft Remote Desktop ActiveX 控件的 RDP 会话页。
// 远端桌面直接绘制在当前标签页中，不打开外部 mstsc 窗口。
class RdpWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RdpWidget(const Common::ConnectionSettings &settings,
                       const QString &sessionName = QString(),
                       QWidget *parent = nullptr);
    ~RdpWidget() override;

    QString tabTitle() const;
    bool isConnected() const;
    QString connectionSummary() const;
    void disconnectSession();

signals:
    void connectionStateChanged(bool connected);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onConnecting();
    void onConnected();
    void onDisconnected(int reason);
    void onFatalError(int code);
    void onWarning(int code);
    void onLoginComplete();

private:
    void configureControl();
    void startSession();
    void setStatus(const QString &text, bool error = false);

    Common::ConnectionSettings m_settings;
    QString m_sessionName;
    QString m_title;
    QAxWidget *m_control = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_connected = false;
    bool m_started = false;
};

#endif // RDPWIDGET_H
