#include "testwindow.h"
#include "terminalproxy.h"
#include <QFileDialog>
#include <QDateTime>
#include <QScrollBar>

TestWindow::TestWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tester(new VT100Tester(this))
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
    , m_runButton(new QPushButton("Run All Tests", this))
    , m_exportButton(new QPushButton("Export HTML Report", this))
    , m_currentTestIndex(0)
    , m_passedCount(0)
    , m_failedCount(0)
    , m_currentTerminal(nullptr)
{
    setWindowTitle("NTerminal Emulation Tester");
    resize(1200, 800);
    
    // 初始化终端标准选择
    m_standardCombo->addItem("VT100 (1978) - Basic ANSI/VT100", VT100Tester::VT100);
    m_standardCombo->addItem("VT102 (1978) - VT100 with additional features", VT100Tester::VT102);
    m_standardCombo->addItem("VT220 (1983) - Enhanced character sets and codes", VT100Tester::VT220);
    m_standardCombo->addItem("VT320 (1987) - Additional editing features", VT100Tester::VT320);
    m_standardCombo->addItem("Linux console - Extended colors and Unicode", VT100Tester::Linux);
    m_standardCombo->addItem("xterm - X Terminal Emulator (extensive features)", VT100Tester::XTerm);
    m_standardCombo->addItem("SCOANSI - SCO Unix console", VT100Tester::SCOANSI);
    m_standardCombo->addItem("ANSI X3.64 / ECMA-48 - Standard terminal", VT100Tester::ANSI);
    m_standardCombo->setCurrentIndex(0);
    
    // 连接信号
    connect(m_tester, &VT100Tester::testStarted,
            this, &TestWindow::onTestStarted);
    connect(m_tester, &VT100Tester::testCompleted,
            this, &TestWindow::onTestCompleted);
    connect(m_tester, &VT100Tester::sequenceSent,
            this, &TestWindow::onSequenceSent);
    connect(m_runButton, &QPushButton::clicked,
            this, &TestWindow::runAllTests);
    connect(m_exportButton, &QPushButton::clicked,
            this, &TestWindow::exportHtmlReport);
    connect(m_standardCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                VT100Tester::TerminalStandard std = static_cast<VT100Tester::TerminalStandard>(
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
        m_logEdit->append("[INFO] Connected to terminal session for testing\n");
    }
}

void TestWindow::setupUI()
{
    // 顶部控制区
    QHBoxLayout *controlLayout = new QHBoxLayout(m_controlWidget);
    controlLayout->addWidget(new QLabel("Test Standard:", this));
    controlLayout->addWidget(m_standardCombo);
    controlLayout->addStretch();
    controlLayout->addWidget(m_runButton);
    controlLayout->addWidget(m_exportButton);
    controlLayout->addWidget(m_summaryLabel);
    
    m_mainLayout->addWidget(m_controlWidget);
    m_mainLayout->addWidget(m_standardLabel);
    
    // 标签页
    m_tabWidget->addTab(m_logEdit, "Test Log");
    m_tabWidget->addTab(m_reportEdit, "Text Report");
    m_tabWidget->addTab(m_sequenceEdit, "Sequences");
    
    m_mainLayout->addWidget(m_tabWidget);
    
    m_summaryLabel->setText("Ready to test. Select terminal standard and click 'Run All Tests' to start.");
}

void TestWindow::runAllTests()
{
    m_logEdit->clear();
    m_sequenceEdit->clear();
    m_currentTestIndex = 0;
    m_passedCount = 0;
    m_failedCount = 0;
    
    VT100Tester::TerminalStandard std = static_cast<VT100Tester::TerminalStandard>(
        m_standardCombo->currentData().toInt());
    
    m_logEdit->append("===========================================\n");
    m_logEdit->append("     NTerminal Emulation Test\n");
    m_logEdit->append("===========================================\n");
    m_logEdit->append("Standard: " + VT100Tester::standardName(std) + "\n");
    m_logEdit->append("Description: " + VT100Tester::standardDescription(std) + "\n");
    m_logEdit->append("Started: " + QDateTime::currentDateTime().toString() + "\n\n");
    
    if (m_currentTerminal) {
        m_logEdit->append("[INFO] Testing connected terminal session\n\n");
    } else {
        m_logEdit->append("[INFO] Testing terminal emulation (no active connection)\n\n");
    }
    
    m_tester->runAllTests();
    
    m_reportEdit->setPlainText(m_tester->generateReport());
    
    updateSummary();
    
    m_logEdit->append("\n===========================================\n");
    m_logEdit->append("All tests completed!\n");
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
        QString("Tests: %1 | Passed: %2 | Failed: %3 | Pass Rate: %4%")
            .arg(total)
            .arg(m_passedCount)
            .arg(m_failedCount)
            .arg(passRate, 0, 'f', 1)
    );
}

void TestWindow::updateStandardInfo()
{
    VT100Tester::TerminalStandard std = static_cast<VT100Tester::TerminalStandard>(
        m_standardCombo->currentData().toInt());
    
    m_standardLabel->setText(
        QString("<b>%1</b> - %2")
            .arg(VT100Tester::standardName(std))
            .arg(VT100Tester::standardDescription(std))
    );
}

void TestWindow::exportHtmlReport()
{
    QString fileName = QFileDialog::getSaveFileName(
        this, "Export HTML Report", "", "HTML Files (*.html);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << m_tester->generateHtmlReport();
            file.close();
            m_logEdit->append("\n[INFO] HTML report exported to: " + fileName);
        }
    }
}
