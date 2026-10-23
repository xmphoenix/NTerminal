#ifndef SESSIONITEMDELEGATE_H
#define SESSIONITEMDELEGATE_H

#include <QStyledItemDelegate>

// ============================================================================
// 会话树项委托（GUI 重设计）
//
// 会话项（两行，高 44）：
//   [状态点] [协议图标] 名称            [协议徽标]
//                      主机:端口
//
// 分组项（单行，高 28）：
//   [chevron] [folder] 分组名          (数量)
//
// 数据角色（由 MainWindow 填充）：
//   SessionNameRole  会话名（用于打开连接）
//   SessionTypeRole  int(Common::ConnectionType)
//   SessionHostRole  副文本（host:port 或 COMx 参数）
//   SessionActiveRole 是否已连接（bool）
//   SessionGroupRole 是否为分组项（bool）
// ============================================================================
class SessionItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    enum DataRole {
        SessionNameRole = Qt::UserRole + 1,
        SessionTypeRole,
        SessionHostRole,
        SessionActiveRole,
        SessionGroupRole,
    };

    explicit SessionItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    void paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                    const QModelIndex &index) const;
    void paintSession(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index) const;
};

#endif // SESSIONITEMDELEGATE_H
