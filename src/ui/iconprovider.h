#ifndef ICONPROVIDER_H
#define ICONPROVIDER_H

#include <QIcon>
#include <QColor>
#include <QPixmap>

// ============================================================================
// 图标提供者（GUI 重设计）：内嵌 Lucide 风格 SVG（24 网格，1.7px 描边），
// 按指定颜色实时着色渲染，带缓存。
//
// 用法：
//   action->setIcon(IconProvider::themedIcon("settings"));
//   button->setIcon(IconProvider::icon("new-session", QColor("#FFFFFF")));
//
// 图标在 16px / 20px 下设计，可用任意颜色渲染（含高 DPI）。
// ============================================================================
class IconProvider
{
public:
    // 名称 -> QIcon（color 无效时使用主题次文字色）
    static QIcon icon(const QString &name, const QColor &color = QColor());

    // 名称 -> QPixmap（指定逻辑尺寸与设备像素比）
    static QPixmap pixmap(const QString &name, const QColor &color,
                          int size = 16, qreal devicePixelRatio = 1.0);

    // 用当前应用主题的次文字色渲染
    static QIcon themedIcon(const QString &name);

    // 应用主题切换后清空缓存（颜色已变）
    static void clearCache();

    // 是否支持该图标名
    static bool contains(const QString &name);

private:
    static QByteArray svgData(const QString &name);
};

#endif // ICONPROVIDER_H
