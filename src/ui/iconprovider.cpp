#include "ui/iconprovider.h"
#include "theme/thememanager.h"

#include <QSvgRenderer>
#include <QPainter>
#include <QHash>

namespace {

// 内嵌 SVG（Lucide 风格：24 网格 / stroke 1.7 / 圆头线帽）
// 注意：SVG 不支持 8 位 hex 颜色（#AARRGGBB 仅为 Qt/CSS 扩展），
// 必须使用 6 位 #FFFFFF，否则 QSvgRenderer 渲染为空。
const char *svgTemplate =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" )"
    R"(stroke="#FFFFFF" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">)"
    "%1"
    R"(</svg>)";

QHash<QString, QString> buildPaths()
{
    QHash<QString, QString> p;
    p["new-session"]   = R"(<path d="M12 5v14M5 12h14"/>)";
    p["disconnect"]    = R"(<path d="M12 3v9"/><path d="M6.4 6.4a8 8 0 1 0 11.2 0"/>)";
    p["reconnect"]     = R"(<path d="M21 12a9 9 0 1 1-2.6-6.4"/><path d="M21 3v6h-6"/>)";
    p["duplicate"]     = R"(<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V5a2 2 0 0 1 2-2h10"/>)";
    p["transfer"]      = R"(<path d="M12 15V3"/><path d="m7 8 5-5 5 5"/><path d="M5 21h14"/>)";
    p["script"]        = R"(<path d="m8 6-6 6 6 6"/><path d="m16 6 6 6-6 6"/>)";
    p["tools"]         = R"(<path d="M14.7 6.3a4.5 4.5 0 0 0-6 6L3 18v3h3l5.7-5.7a4.5 4.5 0 0 0 6-6z"/>)";
    p["theme"]         = R"(<path d="M21 12.8A9 9 0 1 1 11.2 3 7 7 0 0 0 21 12.8z"/>)";
    p["settings"]      = R"(<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1 1.7 1.7 0 0 0-.3-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3h.1a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9v.1a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>)";
    p["search"]        = R"(<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>)";
    p["folder"]        = R"(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>)";
    p["folder-open"]   = R"(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v1"/><path d="M3 9h18l-2.2 9.3a2 2 0 0 1-2 1.7H5a2 2 0 0 1-2-2z"/>)";
    p["server"]        = R"(<rect x="3" y="4" width="18" height="7" rx="1.5"/><rect x="3" y="13" width="18" height="7" rx="1.5"/><path d="M7 7.5h.01M7 16.5h.01"/>)";
    p["terminal"]      = R"(<path d="m4 17 6-6-6-6"/><path d="M12 19h8"/>)";
    p["serial"]        = R"(<path d="M4 9h16v6H4z"/><path d="M8 12h.01M12 12h.01M16 12h.01"/>)";
    p["telnet"]        = R"(<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a14 14 0 0 1 0 18 14 14 0 0 1 0-18z"/>)";
    p["send"]          = R"(<path d="m22 2-7 20-4-9-9-4z"/>)";
    p["key"]           = R"(<circle cx="7.5" cy="15.5" r="4.5"/><path d="m11 12 8-8 3 3-2 2-2-2"/>)";
    p["eye"]           = R"(<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>)";
    p["scroll-bottom"] = R"(<path d="M12 5v14"/><path d="m6 13 6 6 6-6"/>)";
    p["print"]         = R"(<path d="M6 9V3h12v6"/><path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2"/><rect x="6" y="14" width="12" height="8"/>)";
    p["log"]           = R"(<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><path d="M8 13h8M8 17h5"/>)";
    p["sftp"]          = R"(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M12 17v-5"/><path d="m9 14 3-3 3 3"/>)";
    p["check"]         = R"(<path d="m5 12 5 5L20 7"/>)";
    p["close"]         = R"(<path d="M18 6 6 18M6 6l12 12"/>)";
    p["chevron-down"]  = R"(<path d="m6 9 6 6 6-6"/>)";
    p["chevron-right"] = R"(<path d="m9 6 6 6-6 6"/>)";
    p["split"]         = R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M12 4v16"/>)";
    return p;
}

const QHash<QString, QString> &paths()
{
    static const QHash<QString, QString> all = buildPaths();
    return all;
}

QHash<QString, QIcon> &iconCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

} // namespace

void IconProvider::clearCache()
{
    iconCache().clear();
}

bool IconProvider::contains(const QString &name)
{
    return paths().contains(name);
}

QByteArray IconProvider::svgData(const QString &name)
{
    const auto it = paths().constFind(name);
    if (it == paths().constEnd()) {
        return QByteArray();
    }
    return QString::fromUtf8(svgTemplate).arg(it.value()).toUtf8();
}

QPixmap IconProvider::pixmap(const QString &name, const QColor &color,
                             int size, qreal devicePixelRatio)
{
    const QByteArray svg = svgData(name);
    if (svg.isEmpty()) {
        return QPixmap();
    }

    const int px = qMax(1, qRound(size * devicePixelRatio));
    QImage image(px, px, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QSvgRenderer renderer(svg);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0, 0, px, px));
        // 将白色描边整体替换为目标颜色（保留 alpha 抗锯齿边缘）
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color.isValid() ? color : Qt::white);
    }

    QPixmap result = QPixmap::fromImage(image);
    result.setDevicePixelRatio(devicePixelRatio);
    return result;
}

QIcon IconProvider::icon(const QString &name, const QColor &color)
{
    ThemeManager *themeManager = ThemeManager::instance();
    const QColor effective = color.isValid()
        ? color
        : (themeManager ? themeManager->iconColor() : QColor(0x8B, 0x91, 0x9A));

    const QString key = QStringLiteral("%1@%2").arg(name, effective.name());
    auto it = iconCache().constFind(key);
    if (it != iconCache().constEnd()) {
        return it.value();
    }

    QIcon result;
    result.addPixmap(pixmap(name, effective, 16, 1.0));
    result.addPixmap(pixmap(name, effective, 16, 1.5));
    result.addPixmap(pixmap(name, effective, 16, 2.0));
    iconCache().insert(key, result);
    return result;
}

QIcon IconProvider::themedIcon(const QString &name)
{
    return icon(name, QColor());
}
