#include "ui/testwindow.h"
#include "terminal/terminalproxy.h"
#include <QFileDialog>
#include <QDateTime>
#include <QScrollBar>

TestWindow::TestWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tester(new TerminalTester(this))
    , m_centralWidget(new QWidget(this))
    , m_mainLayout(new QVBoxLayout(m_centralWidget))
    , m_tabWidget(new QTabWidget(this))
    , m_logEdit(new QTextEdit(this))
    , m_reportEdit(new QTextEdit(this))
    , m_sequenceEdit(new QPlainTextEdit(this))
    , m_controlWidget(new QWidget(this))
    , m_summaryLabel(new QLabel(this))
    , m_standardLabel(new QLabel(this))
    , m_standardCombo(new QComboBox(this))
    , m_runButton(new QPushButton(tr("运行全部测试"), this))
    , m_exportButton(new QPushButton(tr("导出 HTML 报告"), this))
    , m_currentTestIndex(0)
    , m_passedCount(0)
    , m_failedCount(0)
    , m_currentTerminal(nullptr)
{
    setWindowTitle(tr("NTerminal 终端兼容性测试"));
    resize(1200, 800);

    // 初始化终端标准选择
    m_standardCombo->addItem(tr("VT100 (1978) - 基本 ANSI/VT100"), TerminalTester::VT100);
    m_standardCombo->addItem(tr("VT102 (1978) - 在 VT100 基础上增加功能"), TerminalTester::VT102);
    m_standardCombo->addItem(tr("VT220 (1983) - 增强字符集与编码"), TerminalTester::VT220);
    m_standardCombo->addItem(tr("VT320 (1987) - 增加编辑功能"), TerminalTester::VT320);
    m_standardCombo->addItem(tr("Linux console - 扩展颜色与 Unicode"), TerminalTester::Linux);
    m_standardCombo->addItem(tr("xterm - X 终端模拟器（功能丰富）"), TerminalTester::XTerm);
    m_standardCombo->addItem(tr("SCOANSI - SCO Unix 控制台"), TerminalTester::SCOANSI);
    m_standardCombo->addItem(tr("ANSI X3.64 / ECMA-48 - 标准终端"), TerminalTester::ANSI);
    m_standardCombo->setCurrentIndex(0);
    
    // 连接信号
    connect(m_tester, &TerminalTester::testStarted,
            this, &TestWindow::onTestStarted);
    connect(m_tester, &TerminalTester::testCompleted,
            this, &TestWindow::onTestCompleted);
    connect(m_tester, &TerminalTester::sequenceSent,
            this, &TestWindow::onSequenceSent);
    connect(m_runButton, &QPushButton::clicked,
            this, &TestWindow::runAllTests);
    connect(m_exportButton, &QPushButton::clicked,
            this, &TestWindow::exportHtmlReport);
    connect(m_standardCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                TerminalTester::TerminalStandard std = static_cast<TerminalTester::TerminalStandard>(
                    m_standardCombo->itemData(index).toInt());
                m_tester->setTestStandard(std);
                updateStandardInfo();
            });
    
    setupUI();
    
    setCentralWidget(m_centralWidget);
    
    // 初始化日志
    m_logEdit->setReadOnly(true);
    m_logEdit->setFont(QFont("Consolas", 10));
    
    m_reportEdit->setReadOnly(true);
    m_reportEdit->setFont(QFont("Consolas", 9));
    
    m_sequenceEdit->setReadOnly(true);
    m_sequenceEdit->setFont(QFont("Consolas", 10));
    m_sequenceEdit->setMaximumBlockCount(200);
    
    m_summaryLabel->setStyleSheet("QLabel { background-color: #e7f3fe; padding: 10px; font-size: 14px; }");
    m_standardLabel->setStyleSheet("QLabel { background-color: #fff3cd; padding: 10px; font-size: 12px; }");
    
    m_runButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 10px; font-size: 14px; }");
    m_exportButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; padding: 10px; font-size: 14px; }");
    
    updateStandardInfo();
}

TestWindow::~TestWindow()
{
}

void TestWindow::setTerminal(TerminalProxy *terminal)
{
    m_currentTerminal = terminal;
    m_tester->setTerminal(terminal);
    
    if (terminal) {
        m_logEdit->append(tr("[INFO] 已连接终端会话，可进行实测\n"));
    }
}

void TestWindow::setupUI()
{
    // 顶部控制区
    QHBoxLayout *controlLayout = new QHBoxLayout(m_controlWidget);
    controlLayout->addWidget(new QLabel(tr("测试标准:"), this));
    controlLayout->addWidget(m_standardCombo);
    controlLayout->addStretch();
    controlLayout->addWidget(m_runButton);
    controlLayout->addWidget(m_exportButton);
    controlLayout->addWidget(m_summaryLabel);
    
    m_mainLayout->addWidget(m_controlWidget);
    m_mainLayout->addWidget(m_standardLabel);
    
    // 标签页
    m_tabWidget->addTab(m_logEdit, tr("测试日志"));
    m_tabWidget->addTab(m_reportEdit, tr("文本报告"));
    m_tabWidget->addTab(m_sequenceEdit, tr("序列"));
    
    m_mainLayout->addWidget(m_tabWidget);
    
    m_summaryLabel->setText(tr("准备就绪。选择终端标准后点击“运行全部测试”开始。"));
}

void TestWindow::runAllTests()
{
    m_logEdit->clear();
    m_sequenceEdit->clear();
    m_currentTestIndex = 0;
    m_passedCount = 0;
    m_failedCount = 0;
    
    TerminalTester::TerminalStandard std = static_cast<TerminalTester::TerminalStandard>(
        m_standardCombo->currentData().toInt());
    
    m_logEdit->append("===========================================\n");
    m_logEdit->append(tr("     NTerminal 终端兼容性测试\n"));
    m_logEdit->append("===========================================\n");
    m_logEdit->append(tr("标准: %1\n").arg(TerminalTester::standardName(std)));
    m_logEdit->append(tr("描述: %1\n").arg(TerminalTester::standardDescription(std)));
    m_logEdit->append(tr("开始时间: %1\n\n").arg(QDateTime::currentDateTime().toString()));

    if (m_currentTerminal) {
        m_logEdit->append(tr("[INFO] 正在测试已连接的终端会话\n\n"));
    } else {
        m_logEdit->append(tr("[INFO] 正在测试终端模拟功能（当前无连接）\n\n"));
    }
    
    m_tester->runAllTests();
    
    m_reportEdit->setPlainText(m_tester->generateReport());
    
    updateSummary();
    
    m_logEdit->append("\n===========================================\n");
    m_logEdit->append(tr("全部测试完成！\n"));
    m_logEdit->verticalScrollBar()->setValue(m_logEdit->verticalScrollBar()->maximum());
}

void TestWindow::onTestStarted(const QString &testName)
{
    m_logEdit->append("\n[TEST] " + testName);
    m_logEdit->verticalScrollBar()->setValue(m_logEdit->verticalScrollBar()->maximum());
}

void TestWindow::onTestCompleted(const QString &testName, bool passed)
{
    QString status = passed ? "[PASS]" : "[FAIL]";
    QColor color = passed ? Qt::darkGreen : Qt::red;
    
    m_logEdit->append(QString("<span style='color: %1;'>%2 %3</span>")
                         .arg(color.name())
                         .arg(status)
                         .arg(testName));
    
    if (passed) {
        m_passedCount++;
    } else {
        m_failedCount++;
    }
    m_currentTestIndex++;
    
    updateSummary();
    m_logEdit->verticalScrollBar()->setValue(m_logEdit->verticalScrollBar()->maximum());
}

void TestWindow::onSequenceSent(const QByteArray &sequence)
{
    QString hexStr = sequence.toHex(' ').toUpper();
    QString asciiStr;
    
    for (char c : sequence) {
        if (c >= 0x20 && c <= 0x7E) {
            asciiStr += QChar(c);
        } else if (c == 0x1B) {
            asciiStr += "ESC";
        } else if (c == 0x07) {
            asciiStr += "BEL";
        } else if (c == 0x0D) {
            asciiStr += "CR";
        } else if (c == 0x0A) {
            asciiStr += "LF";
        } else {
            asciiStr += ".";
        }
    }
    
    m_sequenceEdit->appendPlainText(QString("HEX:  %1").arg(hexStr));
    m_sequenceEdit->appendPlainText(QString("ASCII: %1\n").arg(asciiStr));
    m_sequenceEdit->verticalScrollBar()->setValue(m_sequenceEdit->verticalScrollBar()->maximum());
}

void TestWindow::updateSummary()
{
    int total = m_passedCount + m_failedCount;
    double passRate = total > 0 ? 100.0 * m_passedCount / total : 0.0;
    
    m_summaryLabel->setText(
        tr("测试: %1 | 通过: %2 | 失败: %3 | 通过率: %4%")
            .arg(total)
            .arg(m_passedCount)
            .arg(m_failedCount)
            .arg(passRate, 0, 'f', 1)
    );
}

void TestWindow::updateStandardInfo()
{
    TerminalTester::TerminalStandard std = static_cast<TerminalTester::TerminalStandard>(
        m_standardCombo->currentData().toInt());
    
    m_standardLabel->setText(
        QString("<b>%1</b> - %2")
            .arg(TerminalTester::standardName(std))
            .arg(TerminalTester::standardDescription(std))
    );
}

void TestWindow::exportHtmlReport()
{
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("导出 HTML 报告"), "", tr("HTML 文件 (*.html);;所有文件 (*)"));
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << m_tester->generateHtmlReport();
            file.close();
            m_logEdit->append(tr("\n[INFO] HTML 报告已导出到: %1").arg(fileName));
        }
    }
}
