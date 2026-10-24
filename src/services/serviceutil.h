#ifndef SERVICEUTIL_H
#define SERVICEUTIL_H

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// ============================================================================
// 文件类服务（TFTP/FTP/HTTP/NFS）共用的路径处理
//
// 所有"客户端给出的相对路径"都必须经过 safeJoin()：它拒绝 ".." 与绝对
// 路径，并保证结果仍位于根目录内，避免越权读写服务器根目录之外的文件。
// ============================================================================
namespace ServiceUtil {

// 根目录规范化为绝对路径；不存在/为空时返回空字符串
inline QString canonicalRoot(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return QString();
    }
    return QDir::cleanPath(info.canonicalFilePath());
}

inline bool pathInside(const QString &canonicalRoot, const QString &path)
{
    if (canonicalRoot.isEmpty()) {
        return false;
    }
    const QString root = QDir::cleanPath(canonicalRoot);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (path.compare(root, cs) == 0) {
        return true;
    }
    return path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'), cs);
}

// 把客户端路径解析到根目录内；越界/为空时返回空字符串。
// allowMissing=true 用于上传（目标文件尚不存在）。
inline QString safeJoin(const QString &canonicalRoot, const QString &relative,
                        bool allowMissing = false)
{
    if (canonicalRoot.isEmpty()) {
        return QString();
    }
    QString rel = relative;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (rel.startsWith(QLatin1Char('/'))) {
        rel.remove(0, 1);
    }
    if (rel.isEmpty()) {
        return canonicalRoot;
    }
    if (rel.contains(QLatin1Char(':'))) {   // 盘符 / NTFS 数据流
        return QString();
    }
    const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String("..")) {
            return QString();
        }
    }

    const QString joined = QDir::cleanPath(canonicalRoot + QLatin1Char('/') + rel);
    if (!pathInside(canonicalRoot, joined)) {
        return QString();
    }
    if (!allowMissing && !QFileInfo::exists(joined)) {
        return QString();
    }
    return joined;
}

// 目录项名是否安全（单层名称，不含路径分隔符）
inline bool safeName(const QString &name)
{
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
           && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
           && !name.contains(QLatin1Char(':'));
}

} // namespace ServiceUtil

#endif // SERVICEUTIL_H
