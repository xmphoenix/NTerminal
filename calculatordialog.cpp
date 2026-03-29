#include "calculatordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDebug>

CalculatorDialog::CalculatorDialog(QWidget *parent)
    : QDialog(parent)
    , m_currentValue("0")
    , m_operator("")
    , m_operand(0)
    , m_currentBase(10)
    , m_newInput(true)
{
    setupUi();
    setWindowTitle("Calculator");
    resize(400, 500);
}

CalculatorDialog::~CalculatorDialog()
{
}

void CalculatorDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 显示区域
    m_display = new QLineEdit(this);
    m_display->setReadOnly(true);
    m_display->setAlignment(Qt::AlignRight);
    m_display->setText("0");
    QFont displayFont = m_display->font();
    displayFont.setPointSize(16);
    m_display->setFont(displayFont);
    mainLayout->addWidget(m_display);

    // 进制选择
    QHBoxLayout *baseLayout = new QHBoxLayout();
    baseLayout->addWidget(new QLabel("Base:", this));
    m_baseCombo = new QComboBox(this);
    m_baseCombo->addItem("HEX (16)", 16);
    m_baseCombo->addItem("DEC (10)", 10);
    m_baseCombo->addItem("OCT (8)", 8);
    m_baseCombo->addItem("BIN (2)", 2);
    m_baseCombo->setCurrentIndex(1); // 默认十进制
    connect(m_baseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CalculatorDialog::onBaseChanged);
    baseLayout->addWidget(m_baseCombo);
    baseLayout->addStretch();
    mainLayout->addLayout(baseLayout);

    // 进制转换显示
    QGroupBox *conversionGroup = new QGroupBox("Conversions", this);
    QVBoxLayout *convLayout = new QVBoxLayout(conversionGroup);

    QHBoxLayout *hexLayout = new QHBoxLayout();
    hexLayout->addWidget(new QLabel("HEX:", this));
    m_hexLabel = new QLabel("0", this);
    m_hexLabel->setStyleSheet("QLabel { background-color: white; padding: 5px; }");
    hexLayout->addWidget(m_hexLabel, 1);
    convLayout->addLayout(hexLayout);

    QHBoxLayout *decLayout = new QHBoxLayout();
    decLayout->addWidget(new QLabel("DEC:", this));
    m_decLabel = new QLabel("0", this);
    m_decLabel->setStyleSheet("QLabel { background-color: white; padding: 5px; }");
    decLayout->addWidget(m_decLabel, 1);
    convLayout->addLayout(decLayout);

    QHBoxLayout *octLayout = new QHBoxLayout();
    octLayout->addWidget(new QLabel("OCT:", this));
    m_octLabel = new QLabel("0", this);
    m_octLabel->setStyleSheet("QLabel { background-color: white; padding: 5px; }");
    octLayout->addWidget(m_octLabel, 1);
    convLayout->addLayout(octLayout);

    QHBoxLayout *binLayout = new QHBoxLayout();
    binLayout->addWidget(new QLabel("BIN:", this));
    m_binLabel = new QLabel("0", this);
    m_binLabel->setStyleSheet("QLabel { background-color: white; padding: 5px; }");
    binLayout->addWidget(m_binLabel, 1);
    convLayout->addLayout(binLayout);

    mainLayout->addWidget(conversionGroup);

    // 按钮区域
    QGridLayout *buttonLayout = new QGridLayout();

    // 数字按钮 (根据当前进制动态启用/禁用)
    QStringList numbers = {"7", "8", "9", "4", "5", "6", "1", "2", "3", "0"};
    QStringList hexDigits = {"A", "B", "C", "D", "E", "F"};

    int row = 0, col = 0;
    for (const QString &num : numbers) {
        QPushButton *btn = new QPushButton(num, this);
        btn->setMinimumSize(50, 40);
        connect(btn, &QPushButton::clicked, this, &CalculatorDialog::onNumberClicked);
        buttonLayout->addWidget(btn, row, col);
        col++;
        if (col > 2) {
            col = 0;
            row++;
        }
    }

    // 十六进制字母
    row = 0;
    col = 3;
    for (const QString &hex : hexDigits) {
        QPushButton *btn = new QPushButton(hex, this);
        btn->setMinimumSize(50, 40);
        btn->setObjectName("hexButton");
        connect(btn, &QPushButton::clicked, this, &CalculatorDialog::onNumberClicked);
        buttonLayout->addWidget(btn, row, col);
        row++;
        if (row > 2) {
            row = 0;
            col++;
        }
    }

    // 运算符按钮
    QStringList operators = {"+", "-", "*", "/"};
    row = 0;
    col = 5;
    for (const QString &op : operators) {
        QPushButton *btn = new QPushButton(op, this);
        btn->setMinimumSize(50, 40);
        connect(btn, &QPushButton::clicked, this, &CalculatorDialog::onOperatorClicked);
        buttonLayout->addWidget(btn, row++, col);
    }

    // 功能按钮
    QPushButton *clearBtn = new QPushButton("C", this);
    clearBtn->setMinimumSize(50, 40);
    connect(clearBtn, &QPushButton::clicked, this, &CalculatorDialog::onClearClicked);
    buttonLayout->addWidget(clearBtn, 3, 3);

    QPushButton *backspaceBtn = new QPushButton("←", this);
    backspaceBtn->setMinimumSize(50, 40);
    connect(backspaceBtn, &QPushButton::clicked, this, &CalculatorDialog::onBackspaceClicked);
    buttonLayout->addWidget(backspaceBtn, 3, 4);

    QPushButton *equalBtn = new QPushButton("=", this);
    equalBtn->setMinimumSize(50, 40);
    connect(equalBtn, &QPushButton::clicked, this, &CalculatorDialog::onEqualClicked);
    buttonLayout->addWidget(equalBtn, 3, 5);

    mainLayout->addLayout(buttonLayout);

    updateDisplay();
}

void CalculatorDialog::onNumberClicked()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString digit = btn->text();

    // 检查数字是否在当前进制下有效
    int digitValue = digit.toInt(nullptr, 16);
    if (digitValue >= m_currentBase) {
        return; // 无效数字
    }

    if (m_newInput) {
        m_currentValue = digit;
        m_newInput = false;
    } else {
        if (m_currentValue == "0") {
            m_currentValue = digit;
        } else {
            m_currentValue += digit;
        }
    }

    updateDisplay();
}

void CalculatorDialog::onOperatorClicked()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    if (!m_operator.isEmpty()) {
        onEqualClicked();
    }

    m_operator = btn->text();
    m_operand = parseValue(m_currentValue, m_currentBase);
    m_newInput = true;
}

void CalculatorDialog::onEqualClicked()
{
    if (m_operator.isEmpty()) return;

    long long currentVal = parseValue(m_currentValue, m_currentBase);
    long long result = 0;

    if (m_operator == "+") {
        result = m_operand + currentVal;
    } else if (m_operator == "-") {
        result = m_operand - currentVal;
    } else if (m_operator == "*") {
        result = m_operand * currentVal;
    } else if (m_operator == "/") {
        if (currentVal != 0) {
            result = m_operand / currentVal;
        } else {
            m_currentValue = "Error";
            m_display->setText(m_currentValue);
            m_operator.clear();
            m_newInput = true;
            return;
        }
    }

    m_currentValue = formatValue(result, m_currentBase);
    m_operator.clear();
    m_newInput = true;
    updateDisplay();
}

void CalculatorDialog::onClearClicked()
{
    m_currentValue = "0";
    m_operator.clear();
    m_operand = 0;
    m_newInput = true;
    updateDisplay();
}

void CalculatorDialog::onBackspaceClicked()
{
    if (m_currentValue.length() > 1) {
        m_currentValue.chop(1);
    } else {
        m_currentValue = "0";
    }
    updateDisplay();
}

void CalculatorDialog::onBaseChanged(int index)
{
    int newBase = m_baseCombo->itemData(index).toInt();

    // 转换当前值到新进制
    long long value = parseValue(m_currentValue, m_currentBase);
    m_currentBase = newBase;
    m_currentValue = formatValue(value, m_currentBase);

    // 更新按钮启用状态
    QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton *btn : buttons) {
        QString text = btn->text();
        if (text.length() == 1 && (text[0].isDigit() || text[0].isLetter())) {
            int digitValue = text.toInt(nullptr, 16);
            btn->setEnabled(digitValue < m_currentBase);
        }
    }

    updateDisplay();
}

void CalculatorDialog::updateDisplay()
{
    m_display->setText(m_currentValue);

    // 更新所有进制的显示
    long long value = parseValue(m_currentValue, m_currentBase);
    m_hexLabel->setText(formatValue(value, 16));
    m_decLabel->setText(formatValue(value, 10));
    m_octLabel->setText(formatValue(value, 8));
    m_binLabel->setText(formatValue(value, 2));
}

long long CalculatorDialog::parseValue(const QString &value, int base)
{
    bool ok;
    long long result = value.toLongLong(&ok, base);
    return ok ? result : 0;
}

QString CalculatorDialog::formatValue(long long value, int base)
{
    return QString::number(value, base).toUpper();
}

QString CalculatorDialog::convertBase(const QString &value, int fromBase, int toBase)
{
    long long num = parseValue(value, fromBase);
    return formatValue(num, toBase);
}

