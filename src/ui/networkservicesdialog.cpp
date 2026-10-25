#include "ui/networkservicesdialog.h"
#include "services/servermanager.h"
#include "services/serverservice.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QUdpSocket>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString iconForService(const QString &id)
{
    if (id == QLatin1String("tftp")) {
        return QStringLiteral("transfer");
    }
    if (id == QLatin1String("ftp")) {
        return QStringLiteral("sftp");
    }
    if (id == QLatin1String("http")) {
        return QStringLiteral("globe");
    }
    if (id == QLatin1String("telnet")) {
        return QStringLiteral("telnet");
    }
    return QStringLiteral("hard-drive");
}

QString localIPv4()
{
    // 选择"局域网里其他设备能访问到"的地址：
    //  - 跳过 WSL/Hyper-V/VMware/虚拟网卡/VPN（singbox、TAP…）；
    //  - 优先 192.168/16，其次 10/8、172.16/12；
    //  - 同档地址里取接口索引小的（主网卡通常先枚举）。
    const QHostAddress lan192(QStringLiteral("192.168.0.0"));
    const QHostAddress lan10(QStringLiteral("10.0.0.0"));
    const QHostAddress lan172(QStringLiteral("172.16.0.0"));

    QString best;
    int bestScore = -1;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const QString human = iface.humanReadableName();
        const bool virtualAdapter =
            human.contains(QLatin1String("vEthernet"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("WSL"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("Hyper-V"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("VMware"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("VirtualBox"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("TAP-"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("tun"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("singbox"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("tailscale"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("zerotier"), Qt::CaseInsensitive)
            || human.contains(QLatin1String("vpn"), Qt::CaseInsensitive);
        if (virtualAdapter) {
            continue;
        }

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()
                || address.isLinkLocal()) {
                continue;
            }
            int score = 500;
            if (address.isInSubnet(lan192, 16)) {
                score = 3000;
            } else if (address.isInSubnet(lan10, 8)) {
                score = 2000;
            } else if (address.isInSubnet(lan172, 12)) {
                score = 1000;
            }
            score += 255 - qMin(255, int(iface.index()));
            if (score > bestScore) {
                bestScore = score;
                best = address.toString();
            }
        }
    }
    if (!best.isEmpty()) {
        return best;
    }

    // 没有合适的私网地址时，让系统按默认路由选一个源地址
    QUdpSocket probe;
    probe.connectToHost(QHostAddress(QStringLiteral("223.5.5.5")), 53);
    if (probe.state() == QAbstractSocket::ConnectedState) {
        const QHostAddress local = probe.localAddress();
        if (local.protocol() == QAbstractSocket::IPv4Protocol && !local.isLoopback()) {
            return local.toString();
        }
    }
    return QStringLiteral("127.0.0.1");
}

void setStateLabel(QLabel *label, const QString &text, bool on)
{
    label->setText(text);
    const QString state = on ? QStringLiteral("on") : QStringLiteral("off");
    if (label->property("state").toString() != state) {
        label->setProperty("state", state);
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
}

// 可点击的服务行（QFrame 本身没有 clicked 信号）
class ServiceRowFrame : public QFrame
{
    Q_OBJECT

public:
    explicit ServiceRowFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QFrame::mousePressEvent(event);
        emit clicked();
    }
};

} // namespace

NetworkServicesDialog::NetworkServicesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("网络服务"));
    buildUi();
    // 布局构建完再定尺寸：日志区最小高度小一些，保证 720p 屏幕上完整显示
    resize(820, 600);
    refreshAll();
    selectService(0);
}

void NetworkServicesDialog::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 12);
    mainLayout->setSpacing(10);

    // 标题与说明同排，把纵向空间留给服务设置和日志
    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(12);
    QLabel *title = new QLabel(tr("网络服务"), this);
    title->setProperty("class", "sectionTitle");
    QLabel *hint = new QLabel(
        tr("在本机启动 TFTP / FTP / HTTP / Telnet / NFS 服务，供其他设备访问；"
           "服务只实例化一份，关闭窗口后继续运行。"),
        this);
    hint->setProperty("class", "hint");
    // 不换行：wordWrap 的 QLabel 最小高度会按窄宽度计算，反而把对话框撑高
    hint->setWordWrap(false);
    header->addWidget(title);
    header->addWidget(hint, 1);
    mainLayout->addLayout(header);

    QHBoxLayout *body = new QHBoxLayout;
    body->setSpacing(12);

    m_stack = new QStackedWidget(this);
    QWidget *sidebar = buildSidebar();
    body->addWidget(sidebar, 0);
    body->addWidget(m_stack, 1);
    mainLayout->addLayout(body, 1);

    for (ServerService *service : ServerManager::instance()->services()) {
        m_stack->addWidget(buildPage(service));
    }

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

QWidget *NetworkServicesDialog::buildSidebar()
{
    QWidget *sidebar = new QWidget(this);
    sidebar->setFixedWidth(236);
    QVBoxLayout *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    QLabel *heading = new QLabel(tr("本机服务"), sidebar);
    heading->setProperty("class", "subheading");
    layout->addWidget(heading);

    const QList<ServerService *> services = ServerManager::instance()->services();
    for (int index = 0; index < services.size(); ++index) {
        ServerService *service = services.at(index);
        QFrame *row = buildRow(service);
        connect(static_cast<ServiceRowFrame *>(row), &ServiceRowFrame::clicked, this,
                [this, index] { selectService(index); });
        layout->addWidget(row);
    }
    layout->addStretch();

    QLabel *note = new QLabel(
        tr("提示：启动时如弹出防火墙提示，请选择允许，否则只有本机能访问；"
           "配置在服务启动时生效。"),
        sidebar);
    note->setProperty("class", "hint");
    note->setWordWrap(true);
    layout->addWidget(note);
    return sidebar;
}

QFrame *NetworkServicesDialog::buildRow(ServerService *service)
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();

    auto *frame = new ServiceRowFrame(this);
    frame->setObjectName(QStringLiteral("serviceRow"));
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(9, 5, 6, 5);
    layout->setSpacing(8);

    QLabel *icon = new QLabel(frame);
    icon->setPixmap(IconProvider::pixmap(iconForService(service->id()), theme.accentText,
                                         17, devicePixelRatioF()));
    icon->setFixedSize(20, 20);
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon, 0, Qt::AlignVCenter);

    QVBoxLayout *textLayout = new QVBoxLayout;
    textLayout->setSpacing(1);
    QLabel *name = new QLabel(service->displayName(), frame);
    name->setObjectName(QStringLiteral("serviceName"));
    QLabel *status = new QLabel(tr("● 已停止"), frame);
    status->setObjectName(QStringLiteral("serviceStatus"));
    status->setProperty("state", QStringLiteral("off"));
    textLayout->addWidget(name);
    textLayout->addWidget(status);
    layout->addLayout(textLayout, 1);

    QPushButton *toggle = new QPushButton(tr("启动"), frame);
    toggle->setProperty("class", "tonal");
    toggle->setFixedWidth(52);
    layout->addWidget(toggle, 0, Qt::AlignVCenter);

    QToolButton *settings = new QToolButton(frame);
    settings->setIcon(IconProvider::themedIcon(QStringLiteral("settings")));
    settings->setToolTip(tr("设置"));
    layout->addWidget(settings, 0, Qt::AlignVCenter);

    connect(toggle, &QPushButton::clicked, this, [this, service] {
        Page *page = pageFor(service);
        if (!page) {
            return;
        }
        const QList<ServerService *> services = ServerManager::instance()->services();
        selectService(int(services.indexOf(service)));
        if (service->isRunning()) {
            stopService(*page);
        } else {
            startService(*page);
        }
    });
    connect(settings, &QToolButton::clicked, this, [this, service] {
        const QList<ServerService *> services = ServerManager::instance()->services();
        selectService(int(services.indexOf(service)));
    });

    m_rows.append({service, frame, status, toggle});
    return frame;
}

QWidget *NetworkServicesDialog::buildPage(ServerService *service)
{
    const QString id = service->id();
    const bool hasRoot = id != QLatin1String("telnet");
    const QString address = localIPv4();

    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("servicePage"));
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 启停按钮放在页头右侧：主操作始终在第一屏可见
    QPushButton *startButton = new QPushButton(IconProvider::themedIcon(QStringLiteral("play")),
                                               tr("启动服务"), page);
    startButton->setProperty("class", "primary");
    QPushButton *stopButton = new QPushButton(tr("停止服务"), page);
    stopButton->setProperty("class", "ghost");
    stopButton->setEnabled(false);

    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(8);
    QLabel *title = new QLabel(service->displayName(), page);
    title->setProperty("class", "sectionTitle");
    QLabel *chip = new QLabel(tr("● 已停止"), page);
    chip->setObjectName(QStringLiteral("serviceStatus"));
    chip->setProperty("state", QStringLiteral("off"));
    header->addWidget(title);
    header->addWidget(chip);
    header->addStretch();
    header->addWidget(startButton);
    header->addWidget(stopButton);
    layout->addLayout(header);

    QLabel *description = new QLabel(service->description(), page);
    description->setProperty("class", "hint");
    description->setWordWrap(false);
    layout->addWidget(description);

    QGroupBox *settings = new QGroupBox(tr("服务设置"), page);
    QGridLayout *grid = new QGridLayout(settings);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    Page entry;
    entry.service = service;
    entry.page = page;
    entry.statusChip = chip;

    int rowIndex = 0;
    if (hasRoot) {
        grid->addWidget(new QLabel(tr("根目录:"), settings), rowIndex, 0);
        QLineEdit *rootEdit = new QLineEdit(settings);
        rootEdit->setPlaceholderText(tr("选择要共享的文件夹"));
        QPushButton *browse = new QPushButton(tr("浏览..."), settings);
        browse->setProperty("class", "ghost");
        QHBoxLayout *rootLayout = new QHBoxLayout;
        rootLayout->setSpacing(6);
        rootLayout->addWidget(rootEdit, 1);
        rootLayout->addWidget(browse);
        grid->addLayout(rootLayout, rowIndex, 1);
        entry.rootEdit = rootEdit;
        entry.browseButton = browse;
        connect(browse, &QPushButton::clicked, this, [this, rootEdit] {
            const QString dir = QFileDialog::getExistingDirectory(
                this, tr("选择根目录"), rootEdit->text().isEmpty() ? QDir::homePath()
                                                                  : rootEdit->text());
            if (!dir.isEmpty()) {
                rootEdit->setText(QDir::toNativeSeparators(dir));
            }
        });
        ++rowIndex;
    }

    grid->addWidget(new QLabel(tr("监听端口:"), settings), rowIndex, 0);
    QSpinBox *portSpin = new QSpinBox(settings);
    portSpin->setRange(1, 65535);
    portSpin->setFixedWidth(96);
    QHBoxLayout *portLayout = new QHBoxLayout;
    portLayout->setSpacing(8);
    portLayout->addWidget(portSpin);
    portLayout->addStretch();
    grid->addLayout(portLayout, rowIndex, 1);
    entry.portSpin = portSpin;
    ++rowIndex;

    if (id == QLatin1String("tftp")) {
        QCheckBox *allowWrite = new QCheckBox(tr("允许上传（客户端可写入根目录）"), settings);
        grid->addWidget(allowWrite, rowIndex++, 1);
        entry.option1 = allowWrite;
    } else if (id == QLatin1String("http")) {
        QCheckBox *listDirectory = new QCheckBox(tr("允许目录浏览（没有 index.html 时）"), settings);
        grid->addWidget(listDirectory, rowIndex++, 1);
        entry.option1 = listDirectory;
    } else if (id == QLatin1String("ftp")) {
        QCheckBox *anonymous = new QCheckBox(tr("允许匿名访问（anonymous / ftp）"), settings);
        QCheckBox *allowWrite = new QCheckBox(tr("允许上传、删除与重命名"), settings);
        grid->addWidget(anonymous, rowIndex++, 1);
        grid->addWidget(allowWrite, rowIndex++, 1);
        entry.option1 = anonymous;
        entry.option2 = allowWrite;

        QLabel *userLabel = new QLabel(tr("用户名:"), settings);
        QLineEdit *userEdit = new QLineEdit(settings);
        grid->addWidget(userLabel, rowIndex, 0);
        grid->addWidget(userEdit, rowIndex++, 1);
        QLabel *passwordLabel = new QLabel(tr("密码:"), settings);
        QLineEdit *passwordEdit = new QLineEdit(settings);
        passwordEdit->setEchoMode(QLineEdit::Password);
        QToolButton *revealButton = new QToolButton(settings);
        revealButton->setCheckable(true);
        revealButton->setIcon(IconProvider::themedIcon(QStringLiteral("eye")));
        revealButton->setToolTip(tr("显示密码"));
        revealButton->setCursor(Qt::PointingHandCursor);
        connect(revealButton, &QToolButton::toggled, this,
                [passwordEdit, revealButton](bool shown) {
                    passwordEdit->setEchoMode(shown ? QLineEdit::Normal
                                                    : QLineEdit::Password);
                    revealButton->setToolTip(shown ? tr("隐藏密码") : tr("显示密码"));
                });
        QHBoxLayout *passwordLayout = new QHBoxLayout;
        passwordLayout->setSpacing(6);
        passwordLayout->addWidget(passwordEdit, 1);
        passwordLayout->addWidget(revealButton);
        grid->addWidget(passwordLabel, rowIndex, 0);
        grid->addLayout(passwordLayout, rowIndex++, 1);
        entry.userLabel = userLabel;
        entry.userEdit = userEdit;
        entry.passwordLabel = passwordLabel;
        entry.passwordEdit = passwordEdit;
    }

    QLabel *usage = new QLabel(page);
    usage->setObjectName(QStringLiteral("serviceHint"));
    usage->setWordWrap(false);
    usage->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // NFS 的挂载命令单独放在只读文本框里：自动换行、可选中复制
    QPlainTextEdit *commandEdit = nullptr;
    if (id == QLatin1String("nfs")) {
        commandEdit = new QPlainTextEdit(page);
        commandEdit->setObjectName(QStringLiteral("serviceCommand"));
        commandEdit->setReadOnly(true);
        commandEdit->setFixedHeight(58);
        commandEdit->setFrameShape(QFrame::NoFrame);
        commandEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        commandEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    auto updateUsage = [usage, commandEdit, id, address, portSpin] {
        const int listenPort = portSpin->value();
        if (id == QLatin1String("tftp")) {
            usage->setText(tr("客户端示例：tftp -i %1 GET 文件名（Windows 自带 tftp 客户端）")
                               .arg(address));
        } else if (id == QLatin1String("ftp")) {
            usage->setText(tr("访问示例：ftp://%1:%2/ ，或在资源管理器地址栏输入该地址")
                               .arg(address)
                               .arg(listenPort));
        } else if (id == QLatin1String("http")) {
            usage->setText(tr("访问示例：http://%1:%2/").arg(address).arg(listenPort));
        } else if (id == QLatin1String("telnet")) {
            usage->setText(tr("连接示例：telnet %1 %2 ；命令以当前 Windows 用户权限执行，"
                              "请仅在可信网络中使用")
                               .arg(address)
                               .arg(listenPort));
        } else if (id == QLatin1String("nfs")) {
            usage->setText(tr("在 Linux 客户端执行（只读挂载）："));
            if (commandEdit) {
                commandEdit->setPlainText(
                    tr("sudo mount -t nfs -o vers=3,proto=tcp,port=%1,mountport=%1,addr=%2 "
                       "%2:/ /mnt/nfs")
                        .arg(listenPort)
                        .arg(address));
            }
        }
    };
    connect(portSpin, &QSpinBox::valueChanged, this, [updateUsage](int) { updateUsage(); });
    grid->addWidget(usage, rowIndex++, 1);
    if (commandEdit) {
        grid->addWidget(commandEdit, rowIndex++, 1);
    }

    layout->addWidget(settings);

    // 输出区不套分组卡片：标题行 + 等宽日志，省掉一圈边框
    QLabel *outputHeading = new QLabel(tr("服务器输出"), page);
    outputHeading->setProperty("class", "subheading");
    layout->addWidget(outputHeading);

    QPlainTextEdit *log = new QPlainTextEdit(page);
    log->setObjectName(QStringLiteral("serviceLog"));
    log->setReadOnly(true);
    log->setMinimumHeight(64);
    log->setMaximumBlockCount(1000);
    log->setPlaceholderText(tr("服务启动后在这里显示运行日志"));
    // 服务实例长期存活：重新打开对话框时回显之前的日志
    log->setPlainText(service->recentLog().join(QLatin1Char('\n')));
    layout->addWidget(log, 1);

    entry.startButton = startButton;
    entry.stopButton = stopButton;
    entry.log = log;

    connect(startButton, &QPushButton::clicked, this, [this, service] {
        Page *page = pageFor(service);
        if (page) {
            startService(*page);
        }
    });
    connect(stopButton, &QPushButton::clicked, this, [this, service] {
        Page *page = pageFor(service);
        if (page) {
            stopService(*page);
        }
    });

    // 配置读写：每个服务只认识自己的字段
    entry.collect = [entry, id, hasRoot]() {
        QJsonObject config;
        config.insert(QStringLiteral("port"), entry.portSpin->value());
        if (hasRoot) {
            config.insert(QStringLiteral("root"), entry.rootEdit->text().trimmed());
        }
        if (id == QLatin1String("tftp")) {
            config.insert(QStringLiteral("allowWrite"), entry.option1->isChecked());
        } else if (id == QLatin1String("http")) {
            config.insert(QStringLiteral("listDirectory"), entry.option1->isChecked());
        } else if (id == QLatin1String("ftp")) {
            config.insert(QStringLiteral("allowAnonymous"), entry.option1->isChecked());
            config.insert(QStringLiteral("allowWrite"), entry.option2->isChecked());
            config.insert(QStringLiteral("user"), entry.userEdit->text());
            config.insert(QStringLiteral("password"), entry.passwordEdit->text());
        }
        return config;
    };

    entry.load = [entry, hasRoot](const QJsonObject &config) {
        entry.portSpin->setValue(config.value(QStringLiteral("port")).toInt(0));
        if (hasRoot) {
            QString root = config.value(QStringLiteral("root")).toString();
            if (root.isEmpty()) {
                root = QDir::toNativeSeparators(QDir::homePath());
            }
            entry.rootEdit->setText(root);
        }
    };

    entry.setEditable = [entry](bool editable) {
        if (entry.rootEdit) {
            entry.rootEdit->setEnabled(editable);
            entry.browseButton->setEnabled(editable);
        }
        entry.portSpin->setEnabled(editable);
        if (entry.option1) {
            entry.option1->setEnabled(editable);
        }
        if (entry.option2) {
            entry.option2->setEnabled(editable);
        }
        if (entry.userEdit) {
            // 账号密码与"允许匿名访问"互不影响：开着匿名也可以先配好账号，
            // 关掉匿名后配置立即生效
            entry.userLabel->setEnabled(editable);
            entry.userEdit->setEnabled(editable);
            entry.passwordLabel->setEnabled(editable);
            entry.passwordEdit->setEnabled(editable);
        }
    };

    connect(service, &ServerService::logMessage, this,
            [this, service](const QString &line) { appendLog(service, line); });
    connect(service, &ServerService::runningChanged, this,
            [this, service](bool) { refreshService(service); });

    m_pages.append(entry);
    m_pages.last().load(service->config());
    updateUsage();

    const QJsonObject config = service->config();
    if (id == QLatin1String("tftp")) {
        entry.option1->setChecked(config.value(QStringLiteral("allowWrite")).toBool());
    } else if (id == QLatin1String("http")) {
        entry.option1->setChecked(config.value(QStringLiteral("listDirectory")).toBool());
    } else if (id == QLatin1String("ftp")) {
        entry.option1->setChecked(config.value(QStringLiteral("allowAnonymous")).toBool());
        entry.option2->setChecked(config.value(QStringLiteral("allowWrite")).toBool());
        entry.userEdit->setText(config.value(QStringLiteral("user")).toString());
        entry.passwordEdit->setText(config.value(QStringLiteral("password")).toString());
    }

    return page;
}

NetworkServicesDialog::Page *NetworkServicesDialog::pageFor(ServerService *service)
{
    for (Page &page : m_pages) {
        if (page.service == service) {
            return &page;
        }
    }
    return nullptr;
}

void NetworkServicesDialog::selectService(int index)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    m_selected = index;
    m_stack->setCurrentIndex(index);
    for (int i = 0; i < m_rows.size(); ++i) {
        QFrame *frame = m_rows.at(i).frame;
        const QString selected = i == index ? QStringLiteral("true") : QStringLiteral("false");
        if (frame->property("selected").toString() != selected) {
            frame->setProperty("selected", selected);
            frame->style()->unpolish(frame);
            frame->style()->polish(frame);
        }
    }
}

void NetworkServicesDialog::refreshService(ServerService *service)
{
    const bool running = service->isRunning();
    for (const Row &row : m_rows) {
        if (row.service != service) {
            continue;
        }
        setStateLabel(row.status,
                      running ? tr("● 运行中 · 端口 %1").arg(service->port())
                              : tr("● 已停止"),
                      running);
        row.toggle->setText(running ? tr("停止") : tr("启动"));
        row.toggle->setToolTip(running ? tr("停止 %1").arg(service->displayName())
                                       : tr("启动 %1").arg(service->displayName()));
    }
    if (Page *page = pageFor(service)) {
        setStateLabel(page->statusChip, running ? tr("● 运行中") : tr("● 已停止"), running);
        page->startButton->setEnabled(!running);
        page->stopButton->setEnabled(running);
        page->setEditable(!running);
    }
}

void NetworkServicesDialog::refreshAll()
{
    for (ServerService *service : ServerManager::instance()->services()) {
        refreshService(service);
    }
}

void NetworkServicesDialog::startService(const Page &page)
{
    const QJsonObject config = page.collect ? page.collect() : QJsonObject();
    if (!page.service->start(config)) {
        QMessageBox::warning(
            this, tr("启动失败"),
            tr("%1 启动失败：%2").arg(page.service->displayName(),
                                      page.service->errorString()));
    } else if (Page *updated = pageFor(page.service)) {
        updated->load(page.service->config());
    }
    refreshService(page.service);
}

void NetworkServicesDialog::stopService(const Page &page)
{
    page.service->stop();
    refreshService(page.service);
}

void NetworkServicesDialog::appendLog(ServerService *service, const QString &line)
{
    if (Page *page = pageFor(service)) {
        if (page->log) {
            page->log->appendPlainText(line);
        }
    }
}

#include "networkservicesdialog.moc"
