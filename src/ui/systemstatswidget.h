#ifndef SYSTEMSTATSWIDGET_H
#define SYSTEMSTATSWIDGET_H

#include <QJsonObject>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QFrame;

// ============================================================================
// 状态栏远端资源条
//
// 显示 SSH 会话对端的主机名、CPU、内存、网络速率、进程数、运行时长、
// 登录用户数与根分区占用。数据由会话进程每秒推送一次（见 SshHandler），
// 本控件只负责格式化与布局；无数据时整体隐藏。
//
// 窗口变窄时按 detailLevel 逐级收起次要指标，避免状态栏文字被裁切。
// ============================================================================
class SystemStatsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SystemStatsWidget(QWidget *parent = nullptr);

    void setStats(const QJsonObject &stats);
    void clear();
    // 主题切换后按新的次要文字色重新渲染图标
    void refreshIcons();
    // 0=全部，1=隐藏主机名，2=再隐藏用户/运行时长，
    // 3=只留 CPU/内存/网络，4=整体隐藏
    void setDetailLevel(int level);

private:
    struct Metric {
        QLabel *icon = nullptr;
        QLabel *text = nullptr;
        QWidget *wrap = nullptr;      // 图标 + 数值的容器，用于整组隐藏
        QFrame *separator = nullptr;  // 本指标前的分隔竖线（首个指标没有）
        QString iconName;
    };

    Metric addMetric(const QString &iconName, const QString &objectName);
    void applyIcon(Metric &metric);
    void setValue(Metric &metric, const QString &text);

    QHBoxLayout *m_layout = nullptr;
    QJsonObject m_stats;
    int m_detailLevel = 0;

    Metric m_host;
    Metric m_cpu;
    Metric m_memory;
    Metric m_network;
    Metric m_processes;
    Metric m_uptime;
    Metric m_users;
    Metric m_disk;
};

#endif // SYSTEMSTATSWIDGET_H
