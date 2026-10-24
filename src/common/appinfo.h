#ifndef APPINFO_H
#define APPINFO_H

#include <QString>

// ============================================================================
// 应用元信息（单一来源）
//
// 版本号由 NTerminal.pro 的 VERSION 通过 APP_VERSION 宏注入，
// 同时用于 Windows 可执行文件属性、主窗口标题与"关于"对话框，
// 避免三处各写一份字符串而出现不一致。
// ============================================================================

namespace AppInfo {

inline constexpr const char *ProductName = "NTerminal";

// 未通过 qmake 注入时（例如单独编译某个源文件）保留工程当前版本
#ifndef APP_VERSION
#define APP_VERSION "2.0.0"
#endif

inline QString version()
{
    return QStringLiteral(APP_VERSION);
}

// 主窗口标题：产品名 + 版本
inline QString windowTitle()
{
    return QStringLiteral("%1 %2").arg(QLatin1String(ProductName), version());
}

} // namespace AppInfo

#endif // APPINFO_H
