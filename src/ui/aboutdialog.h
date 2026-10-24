#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

// ============================================================================
// 关于对话框：应用名称、标语、版本与运行环境。
// 版本号来自 NTerminal.pro 的 VERSION（APP_VERSION 宏），
// 与主窗口标题、Windows 可执行文件属性保持一致。
// ============================================================================
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

#endif // ABOUTDIALOG_H
