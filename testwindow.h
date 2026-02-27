#ifndef TESTWINDOW_H
#define TESTWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QLabel>
#include <QComboBox>
#include "terminaltester.h"

class TerminalProxy;

/**
 * @brief VT100/VT220/xterm 终端仿真测试窗口
 * 
 * 测试 NTerminal 程序本身的终端仿真能力
 */
class TestWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TestWindow(QWidget *parent = nullptr);
    ~TestWindow();
    
    // 设置终端代理
    void setTerminal(TerminalProxy *terminal);

public slots:
    void runAllTests();
    void onTestStarted(const QString &testName);
    void onTestCompleted(const QString &testName, bool passed);
    void onSequenceSent(const QByteArray &sequence);
    void exportHtmlReport();

private:
    void setupUI();
    void updateSummary();
    void updateStandardInfo();

    TerminalTester *m_tester;

    // UI 组件
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;

    QTabWidget *m_tabWidget;

    QTextEdit *m_logEdit;           // 测试日志
    QTextEdit *m_reportEdit;        // 文本报告
    QPlainTextEdit *m_sequenceEdit; // 显示的序列
    
    QWidget *m_controlWidget;       // 控制面板
    QLabel *m_summaryLabel;         // 摘要信息
    QLabel *m_standardLabel;        // 标准信息
    QComboBox *m_standardCombo;     // 终端标准选择
    QPushButton *m_runButton;       // 运行测试按钮
    QPushButton *m_exportButton;    // 导出 HTML 按钮

    // 统计
    int m_currentTestIndex;
    int m_passedCount;
    int m_failedCount;
    
    // 终端连接
    TerminalProxy *m_currentTerminal;
};

#endif // TESTWINDOW_H
