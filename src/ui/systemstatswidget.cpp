#include "ui/systemstatswidget.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>

namespace {

QString formatSize(double bytes)
{
    const double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) {
        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 2);
    }
    const double mb = bytes / (1024.0 * 1024.0);
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

QString formatUsage(double used, double total)
{
    const double gb = 1024.0 * 1024.0 * 1024.0;
    if (total >= gb) {
        return QStringLiteral("%1 / %2 GB")
            .arg(used / gb, 0, 'f', 2)
            .arg(total / gb, 0, 'f', 2);
    }
    return QStringLiteral("%1 / %2")
        .arg(formatSize(used), formatSize(total));
}

QString formatUptime(qint64 seconds)
{
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (days > 0) {
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    }
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QStringLiteral("%1 min").arg(minutes);
    }
    return QStringLiteral("<1 min");
}

} // namespace

SystemStatsWidget::SystemStatsWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("systemStats"));
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 0, 2, 0);
    m_layout->setSpacing(0);

    m_host = addMetric(QStringLiteral("server"), QStringLiteral("statHost"));
    m_cpu = addMetric(QStringLiteral("cpu"), QStringLiteral("statCpu"));
    m_memory = addMetric(QStringLiteral("memory"), QStringLiteral("statMemory"));
    m_network = addMetric(QStringLiteral("activity"), QStringLiteral("statNetwork"));
    m_processes = addMetric(QStringLiteral("list"), QStringLiteral("statProcesses"));
    m_uptime = addMetric(QStringLiteral("clock"), QStringLiteral("statUptime"));
    m_users = addMetric(QStringLiteral("users"), QStringLiteral("statUsers"));
    m_disk = addMetric(QStringLiteral("hard-drive"), QStringLiteral("statDisk"));

    hide();
}

SystemStatsWidget::Metric SystemStatsWidget::addMetric(const QString &iconName,
                                                       const QString &objectName)
{
    Metric metric;
    metric.iconName = iconName;

    // 指标之间用 1px 竖线分隔，与状态栏其余项的分隔风格一致
    if (m_layout->count() > 0) {
        QFrame *separator = new QFrame(this);
        separator->setFrameShape(QFrame::VLine);
        separator->setFixedHeight(12);
        m_layout->addWidget(separator, 0, Qt::AlignVCenter);
        metric.separator = separator;
    }

    QWidget *wrap = new QWidget(this);
    metric.wrap = wrap;
    QHBoxLayout *wrapLayout = new QHBoxLayout(wrap);
    wrapLayout->setContentsMargins(0, 0, 0, 0);
    wrapLayout->setSpacing(3);

    metric.icon = new QLabel(wrap);
    metric.icon->setObjectName(QStringLiteral("statIcon"));
    metric.icon->setFixedSize(16, 16);
    metric.icon->setAlignment(Qt::AlignCenter);
    applyIcon(metric);

    metric.text = new QLabel(wrap);
    metric.text->setObjectName(objectName);
    metric.text->setProperty("role", "value");
    metric.text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    wrapLayout->addWidget(metric.icon, 0, Qt::AlignVCenter);
    wrapLayout->addWidget(metric.text, 0, Qt::AlignVCenter);
    m_layout->addWidget(wrap, 0, Qt::AlignVCenter);

    return metric;
}

void SystemStatsWidget::applyIcon(Metric &metric)
{
    if (!metric.icon) {
        return;
    }
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    metric.icon->setPixmap(IconProvider::pixmap(metric.iconName, theme.textDim,
                                                13, devicePixelRatioF()));
}

void SystemStatsWidget::setValue(Metric &metric, const QString &text)
{
    if (metric.text) {
        metric.text->setText(text);
        metric.text->setToolTip(QString());
    }
}

void SystemStatsWidget::setStats(const QJsonObject &stats)
{
    if (stats.isEmpty()) {
        clear();
        return;
    }
    // 每 3 秒来一次；内容没变就不重排状态栏
    if (stats == m_stats) {
        return;
    }
    m_stats = stats;

    const QString host = stats.value(QStringLiteral("host")).toString();
    if (m_host.text) {
        const QString elided = m_host.text->fontMetrics().elidedText(
            host, Qt::ElideMiddle, 160);
        m_host.text->setText(elided);
        m_host.text->setToolTip(host);
    }

    // ---- CPU ----
    const int cpu = stats.value(QStringLiteral("cpu")).toInt(-1);
    setValue(m_cpu, cpu >= 0 ? QStringLiteral("%1%").arg(cpu) : QStringLiteral("—"));
    if (m_cpu.text) {
        QString tip = tr("CPU 使用率");
        if (stats.contains(QStringLiteral("load1"))) {
            tip += tr(" · 负载 %1 / %2 / %3")
                       .arg(stats.value(QStringLiteral("load1")).toDouble(), 0, 'f', 2)
                       .arg(stats.value(QStringLiteral("load5")).toDouble(), 0, 'f', 2)
                       .arg(stats.value(QStringLiteral("load15")).toDouble(), 0, 'f', 2);
        }
        m_cpu.text->setToolTip(tip);
    }

    // ---- 内存 ----
    const double memUsed = stats.value(QStringLiteral("memUsed")).toDouble();
    const double memTotal = stats.value(QStringLiteral("memTotal")).toDouble();
    setValue(m_memory, formatUsage(memUsed, memTotal));
    if (m_memory.text) {
        QString tip = tr("内存 %1 / %2").arg(formatSize(memUsed), formatSize(memTotal));
        const double swapTotal = stats.value(QStringLiteral("swapTotal")).toDouble();
        if (swapTotal > 0) {
            tip += tr(" · 交换 %1 / %2")
                       .arg(formatSize(stats.value(QStringLiteral("swapUsed")).toDouble()),
                            formatSize(swapTotal));
        }
        m_memory.text->setToolTip(tip);
    }

    // ---- 网络（上行/下行速率） ----
    const double rx = stats.value(QStringLiteral("netRx")).toDouble() / 1.0e6;
    const double tx = stats.value(QStringLiteral("netTx")).toDouble() / 1.0e6;
    setValue(m_network, QStringLiteral("↑ %1 ↓ %2 Mb/s")
                            .arg(tx, 0, 'f', 2)
                            .arg(rx, 0, 'f', 2));
    if (m_network.text) {
        m_network.text->setToolTip(tr("上行 %1 Mb/s · 下行 %2 Mb/s")
                                       .arg(tx, 0, 'f', 2)
                                       .arg(rx, 0, 'f', 2));
    }

    // ---- 进程 / 运行时长 / 登录用户 ----
    const int procs = stats.value(QStringLiteral("procs")).toInt();
    setValue(m_processes, procs > 0 ? QString::number(procs) : QStringLiteral("—"));
    if (m_processes.text) {
        m_processes.text->setToolTip(tr("运行中的进程数"));
    }

    const qint64 uptime = qint64(stats.value(QStringLiteral("uptime")).toDouble());
    setValue(m_uptime, formatUptime(uptime));
    if (m_uptime.text) {
        m_uptime.text->setToolTip(tr("系统已运行 %1").arg(formatUptime(uptime)));
    }

    const int users = stats.value(QStringLiteral("users")).toInt();
    QString names = stats.value(QStringLiteral("userNames")).toString();
    QString userText = names;
    if (!names.isEmpty() && users > 1 && !names.contains(QLatin1Char(','))) {
        // 单个用户开了多个会话时显示成 "ybzhang (x2)"（同 MobaXterm）
        userText = QStringLiteral("%1 (x%2)").arg(names).arg(users);
    } else if (names.isEmpty()) {
        userText = QString::number(users);
    }
    if (m_users.text) {
        const QString elided = m_users.text->fontMetrics().elidedText(
            userText, Qt::ElideRight, 110);
        m_users.text->setText(elided);
        m_users.text->setToolTip(names.isEmpty()
                                     ? tr("登录用户数")
                                     : tr("登录用户 %1 · %2 个会话").arg(names).arg(users));
    }

    // ---- 根分区 ----
    if (stats.contains(QStringLiteral("diskPercent"))) {
        const int percent = stats.value(QStringLiteral("diskPercent")).toInt();
        const QString mount = stats.value(QStringLiteral("diskMount"))
                                  .toString(QStringLiteral("/"));
        setValue(m_disk, QStringLiteral("%1 %2%").arg(mount).arg(percent));
        if (m_disk.text) {
            m_disk.text->setToolTip(tr("根分区已用 %1 / %2 · 可用 %3")
                                        .arg(formatSize(stats.value(QStringLiteral("diskUsed")).toDouble()),
                                             formatSize(stats.value(QStringLiteral("diskTotal")).toDouble()),
                                             formatSize(stats.value(QStringLiteral("diskAvail")).toDouble())));
        }
    } else {
        setValue(m_disk, QStringLiteral("—"));
    }

    if (isHidden() && m_detailLevel < 4) {
        show();
    }
}

void SystemStatsWidget::clear()
{
    m_stats = QJsonObject();
    hide();
}

void SystemStatsWidget::setDetailLevel(int level)
{
    if (level == m_detailLevel) {
        return;
    }
    m_detailLevel = level;

    // 每项指标的收起级别：host 最先收起，其次用户/运行时长，最后进程/磁盘；
    // CPU、内存、网络通常最有价值，保留到最后一档。
    const struct {
        Metric *metric;
        int hideAt;
    } entries[] = {
        { &m_host, 1 },
        { &m_cpu, 99 },
        { &m_memory, 99 },
        { &m_network, 99 },
        { &m_processes, 3 },
        { &m_uptime, 2 },
        { &m_users, 2 },
        { &m_disk, 3 },
    };

    // 第一项可见指标不需要前置分隔线，否则资源条左缘会多出一条孤立竖线
    bool firstVisible = true;
    for (const auto &entry : entries) {
        const bool visible = m_detailLevel < entry.hideAt;
        if (entry.metric->wrap) {
            entry.metric->wrap->setVisible(visible);
        }
        if (entry.metric->separator) {
            entry.metric->separator->setVisible(visible && !firstVisible);
        }
        if (visible) {
            firstVisible = false;
        }
    }

    if (m_detailLevel >= 4) {
        hide();
    } else if (!m_stats.isEmpty()) {
        show();
    }
    updateGeometry();
}

void SystemStatsWidget::refreshIcons()
{
    for (Metric *metric : {&m_host, &m_cpu, &m_memory, &m_network,
                           &m_processes, &m_uptime, &m_users, &m_disk}) {
        applyIcon(*metric);
    }
}
