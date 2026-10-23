#include "ui/calculatordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QVector>
#include <QPair>
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
    setWindowTitle(tr("计算器"));
    setMinimumWidth(420);
    // 收起为内容的自然高度，避免键盘下方留出大片空白
    adjustSize();
}

CalculatorDialog::~CalculatorDialog()
{
}

void CalculatorDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    // 头部：标题 + 说明
    QLabel *titleLabel = new QLabel(tr("程序员计算器"), this);
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(tr("支持二 / 八 / 十 / 十六进制运算与即时进制换算"), this);
    hintLabel->setProperty("class", "hint");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    // 显示区域：大号等宽数字，模拟计算器屏幕
    m_display = new QLineEdit(this);
    m_display->setObjectName("calcDisplay");
    m_display->setReadOnly(true);
    m_display->setAlignment(Qt::AlignRight);
    m_display->setText("0");
    QFont displayFont = m_display->font();
    displayFont.setPixelSize(28);
    displayFont.setWeight(QFont::DemiBold);
    m_display->setFont(displayFont);
    m_display->setMinimumHeight(50);
    mainLayout->addWidget(m_display);

    // 进制选择
    QHBoxLayout *baseLayout = new QHBoxLayout();
    baseLayout->setSpacing(8);
    baseLayout->addWidget(new QLabel(tr("进制:"), this));
    m_baseCombo = new QComboBox(this);
    m_baseCombo->addItem(tr("十六进制 (16)"), 16);
    m_baseCombo->addItem(tr("十进制 (10)"), 10);
    m_baseCombo->addItem(tr("八进制 (8)"), 8);
    m_baseCombo->addItem(tr("二进制 (2)"), 2);
    m_baseCombo->setCurrentIndex(1); // 默认十进制
    connect(m_baseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CalculatorDialog::onBaseChanged);
    baseLayout->addWidget(m_baseCombo, 1);
    mainLayout->addLayout(baseLayout);

    // 进制转换显示：每行「标签 + 数值」，数值使用次级容器样式
    QGroupBox *conversionGroup = new QGroupBox(tr("进制转换"), this);
    QGridLayout *convLayout = new QGridLayout(conversionGroup);
    convLayout->setContentsMargins(0, 0, 0, 0);
    convLayout->setHorizontalSpacing(12);
    convLayout->setVerticalSpacing(8);
    convLayout->setColumnStretch(1, 1);

    const QVector<QPair<QString, QLabel *>> conversionRows = {
        { tr("十六进制"), m_hexLabel = new QLabel("0", this) },
        { tr("十进制"),   m_decLabel = new QLabel("0", this) },
        { tr("八进制"),   m_octLabel = new QLabel("0", this) },
        { tr("二进制"),   m_binLabel = new QLabel("0", this) },
    };

    int convRow = 0;
    for (const auto &row : conversionRows) {
        row.second->setObjectName("calcValue");
        row.second->setMinimumWidth(160);
        row.second->setTextInteractionFlags(Qt::TextSelectableByMouse);
        convLayout->addWidget(new QLabel(row.first + ":", this), convRow, 0);
        convLayout->addWidget(row.second, convRow, 1);
        ++convRow;
    }

    mainLayout->addWidget(conversionGroup);

    // 按钮区域：三个按键网格靠左，运算区靠右
    QGridLayout *buttonLayout = new QGridLayout();
    buttonLayout->setHorizontalSpacing(8);
    buttonLayout->setVerticalSpacing(8);

    // 数字按钮 (根据当前进制动态启用/禁用)
    QStringList numbers = {"7", "8", "9", "4", "5", "6", "1", "2", "3", "0"};
    QStringList hexDigits = {"A", "B", "C", "D", "E", "F"};

    int row = 0, col = 0;
    for (const QString &num : numbers) {
        QPushButton *btn = new QPushButton(num, this);
        btn->setProperty("class", "tonal");
        btn->setMinimumSize(48, 36);
        connect(btn, &QPushButton::clicked, this, &CalculatorDialog::onNumberClicked);
        // 0 键跨三列，与上方数字区等宽，避免底行出现空洞
        if (num == "0") {
            buttonLayout->addWidget(btn, row, col, 1, 3);
        } else {
            buttonLayout->addWidget(btn, row, col);
        }
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
        btn->setProperty("class", "tonal");
        btn->setMinimumSize(48, 36);
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
        btn->setMinimumSize(48, 36);
        connect(btn, &QPushButton::clicked, this, &CalculatorDialog::onOperatorClicked);
        buttonLayout->addWidget(btn, row++, col);
    }

    // 功能按钮
    QPushButton *clearBtn = new QPushButton("C", this);
    clearBtn->setProperty("class", "ghost");
    clearBtn->setMinimumSize(48, 36);
    connect(clearBtn, &QPushButton::clicked, this, &CalculatorDialog::onClearClicked);
    buttonLayout->addWidget(clearBtn, 3, 3);

    QPushButton *backspaceBtn = new QPushButton("←", this);
    backspaceBtn->setProperty("class", "ghost");
    backspaceBtn->setMinimumSize(48, 36);
    connect(backspaceBtn, &QPushButton::clicked, this, &CalculatorDialog::onBackspaceClicked);
    buttonLayout->addWidget(backspaceBtn, 3, 4);

    QPushButton *equalBtn = new QPushButton("=", this);
    equalBtn->setProperty("class", "primary");
    equalBtn->setMinimumSize(48, 36);
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
            m_currentValue = tr("错误");
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

