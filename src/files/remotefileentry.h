#ifndef REMOTEFILEENTRY_H
#define REMOTEFILEENTRY_H

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

// 远端目录项（SFTP / FTP / SCP 文件管理器共用）
struct RemoteFileEntry {
    QString name;
    QString path;           // 完整远端路径
    bool isDir = false;
    bool isSymlink = false;
    qint64 size = 0;
    QDateTime modified;
    QString permissions;
    QString owner;
    QString group;
    int mode = 0;
};

// 下载/删除等操作的目标；由 UI 从当前列表直接构造，避免再次 stat
struct RemoteTransferItem {
    QString name;
    QString path;
    bool isDir = false;
    qint64 size = 0;
    int mode = 0;
};

Q_DECLARE_METATYPE(RemoteFileEntry)
Q_DECLARE_METATYPE(QVector<RemoteFileEntry>)
Q_DECLARE_METATYPE(RemoteTransferItem)
Q_DECLARE_METATYPE(QVector<RemoteTransferItem>)

#endif // REMOTEFILEENTRY_H
