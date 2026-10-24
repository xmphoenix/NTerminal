#include "ui/rdpwidget.h"

#include <QDebug>
#include <QLabel>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <QAxObject>
#include <QAxWidget>

namespace {

// Windows 自带的 RDP ActiveX 控件。版本 12 在 Windows 10/11 上可用；
// 旧系统回退到可再发行版本 11。
constexpr char kRdpControlV12[] =
    "{1DF7C823-B2D4-4B54-975A-F2AC5D7CF8B8}";
constexpr char kRdpControlV11[] =
    "{c0efa91a-eeb7-41c7-97fa-f0ed645efb24}";

} // namespace
#endif

RdpWidget::RdpWidget(const Common::ConnectionSettings &settings,
                     const QString &sessionName, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_sessionName(sessionName)
{
    m_title = QStringLiteral("RDP - %1")
                  .arg(m_sessionName.isEmpty() ? settings.host : m_sessionName);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setProperty("class", "hint");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setText(tr("正在启动 RDP 控件..."));
    root->addWidget(m_statusLabel);

#ifdef Q_OS_WIN
    m_control = new QAxWidget(this);
    m_control->setControl(QString::fromLatin1(kRdpControlV12));
    if (m_control->isNull()) {
        m_control->setControl(QString::fromLatin1(kRdpControlV11));
    }
    if (m_control->isNull()) {
        root->addStretch(1);
        setStatus(tr("无法加载 Microsoft RDP 控件（MsTscAx）。"
                     "请确认系统已安装远程桌面客户端。"),
                  true);
        return;
    }

    m_control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(m_control, 1);

    connect(m_control, SIGNAL(OnConnecting()), this, SLOT(onConnecting()));
    connect(m_control, SIGNAL(OnConnected()), this, SLOT(onConnected()));
    connect(m_control, SIGNAL(OnDisconnected(int)), this,
            SLOT(onDisconnected(int)));
    connect(m_control, SIGNAL(OnFatalError(int)), this,
            SLOT(onFatalError(int)));
    connect(m_control, SIGNAL(OnWarning(int)), this, SLOT(onWarning(int)));
    connect(m_control, SIGNAL(OnLoginComplete()), this,
            SLOT(onLoginComplete()));

    configureControl();
#else
    Q_UNUSED(settings);
    root->addStretch(1);
    setStatus(tr("RDP 会话仅支持 Windows 客户端。"), true);
#endif
}

RdpWidget::~RdpWidget()
{
    disconnectSession();
}

QString RdpWidget::tabTitle() const
{
    return m_title;
}

bool RdpWidget::isConnected() const
{
    return m_connected;
}

QString RdpWidget::connectionSummary() const
{
    const QString user = m_settings.username.isEmpty()
                             ? QStringLiteral("--")
                             : m_settings.username;
    return QStringLiteral("%1@%2:%3").arg(user, m_settings.host)
        .arg(m_settings.port);
}

void RdpWidget::configureControl()
{
#ifdef Q_OS_WIN
    if (!m_control || m_control->isNull()) {
        return;
    }

    m_control->setProperty("Server", m_settings.host);
    m_control->setProperty("UserName", m_settings.username);
    m_control->setProperty("Domain", m_settings.rdpDomain);
    m_control->setProperty("ColorDepth", 32);
    m_control->setProperty("DesktopWidth", 1600);
    m_control->setProperty("DesktopHeight", 900);
    m_control->setProperty("FullScreen", false);
    m_control->setProperty("DisableConnectionBar", true);
    m_control->setProperty("AllowPromptingForCredentials", true);
    m_control->setProperty("ConnectingText", tr("正在连接 RDP..."));
    m_control->setProperty("DisconnectedText", tr("RDP 连接已断开"));
    if (!m_settings.password.isEmpty()) {
        m_control->setProperty("ClearTextPassword", m_settings.password);
    }

    if (auto *advanced2 = m_control->querySubObject("AdvancedSettings2")) {
        advanced2->setProperty("RDPPort", m_settings.port);
        advanced2->setProperty("SmartSizing", true);
        advanced2->setProperty("GrabFocusOnConnect", true);
        if (!m_settings.password.isEmpty()) {
            advanced2->setProperty("ClearTextPassword", m_settings.password);
        }
    }
    if (auto *advanced5 = m_control->querySubObject("AdvancedSettings5")) {
        // 0 = 连接但不因服务器证书警告而中断（xrdp 常见自签名证书）
        advanced5->setProperty("AuthenticationLevel", 0);
    }
    if (auto *advanced7 = m_control->querySubObject("AdvancedSettings7")) {
        advanced7->setProperty("EnableCredSspSupport", m_settings.rdpUseNla);
        if (!m_settings.password.isEmpty()) {
            advanced7->setProperty("ClearTextPassword", m_settings.password);
        }
    }
#endif
}

void RdpWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_started) {
        m_started = true;
        QTimer::singleShot(50, this, &RdpWidget::startSession);
    }
}

void RdpWidget::startSession()
{
#ifdef Q_OS_WIN
    if (!m_control || m_control->isNull()) {
        return;
    }
    setStatus(tr("正在连接 RDP..."));
    const QVariant result = m_control->dynamicCall("Connect()");
    if (result.isValid() && !result.toBool()) {
        setStatus(tr("无法启动 RDP 连接。"), true);
    }
    qDebug() << "[RdpWidget] Connect called for" << m_settings.host;
#endif
}

void RdpWidget::disconnectSession()
{
#ifdef Q_OS_WIN
    if (m_control && !m_control->isNull() && m_connected) {
        m_control->dynamicCall("Disconnect()");
    }
#endif
    m_connected = false;
}

void RdpWidget::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("state", error ? QStringLiteral("error")
                                              : QString());
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->setVisible(!text.isEmpty());
}

void RdpWidget::onConnecting()
{
    m_connected = false;
    setStatus(tr("正在连接 RDP..."));
    emit connectionStateChanged(false);
}

void RdpWidget::onConnected()
{
    m_connected = true;
    setStatus(QString());
#ifdef Q_OS_WIN
    if (m_control) {
        m_control->setFocus();
    }
#endif
    emit connectionStateChanged(true);
    qDebug() << "[RdpWidget] Connected to" << m_settings.host;
}

void RdpWidget::onDisconnected(int reason)
{
    m_connected = false;
    setStatus(tr("RDP 连接已断开（代码 %1）").arg(reason));
    emit connectionStateChanged(false);
    qDebug() << "[RdpWidget] Disconnected, reason" << reason;
}

void RdpWidget::onFatalError(int code)
{
    m_connected = false;
    setStatus(tr("RDP 连接失败（错误 %1）").arg(code), true);
    emit connectionStateChanged(false);
    qWarning() << "[RdpWidget] Fatal error" << code;
}

void RdpWidget::onWarning(int code)
{
    qWarning() << "[RdpWidget] Warning" << code;
}

void RdpWidget::onLoginComplete()
{
    m_connected = true;
    setStatus(QString());
    emit connectionStateChanged(true);
}
