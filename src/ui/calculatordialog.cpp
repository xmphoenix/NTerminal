#include "ui/calculatordialog.h"
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPair>
#include <QVBoxLayout>
#include <QVector>

CalculatorDialog::CalculatorDialog(QWidget *parent)
    : QDialog(parent)
    , m_baseGroup(nullptr)
    , m_currentValue("0")
    , m_operator("")
    , m_operand(0)
    , m_currentBase(10)
    , m_newInput(true)
{
    setupUi();
    setWindowTitle(tr("计算器"));
    setMinimumWidth(340);
    // 收起为内容的自然高度，避免键盘下方留出大片空白
    adjustSize();
}

CalculatorDialog::~CalculatorDialog()
{
}

void CalculatorDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 16);
    mainLayout->setSpacing(8);

    // 头部只保留标题，说明放进提示气泡，省下一行高度
    QLabel *titleLabel = new QLabel(tr("程序员计算器"), this);
    titleLabel->setProperty("class", "sectionTitle");
    titleLabel->setToolTip(tr("支持二 / 八 / 十 / 十六进制运算与即时进制换算"));
    mainLayout->addWidget(titleLabel);

    // 显示区域：大号等宽数字，模拟计算器屏幕
    m_display = new QLineEdit(this);
    m_display->setObjectName("calcDisplay");
    m_display->setReadOnly(true);
    m_display->setAlignment(Qt::AlignRight);
    m_display->setText("0");
    QFont displayFont = m_display->font();
    displayFont.setPixelSize(26);
    displayFont.setWeight(QFont::DemiBold);
    m_display->setFont(displayFont);
    m_display->setMinimumHeight(46);
    mainLayout->addWidget(m_display);

    // 进制开关：分段控件，一次点击直达目标进制
    QFrame *baseBar = new QFrame(this);
    baseBar->setObjectName("calcBaseBar");
    QHBoxLayout *baseLayout = new QHBoxLayout(baseBar);
    baseLayout->setContentsMargins(2, 2, 2, 2);
    baseLayout->setSpacing(2);
    m_baseGroup = new QButtonGroup(this);
    const QVector<QPair<QString, int>> bases = {
        { QStringLiteral("HEX"), 16 },
        { QStringLiteral("DEC"), 10 },
        { QStringLiteral("OCT"), 8 },
        { QStringLiteral("BIN"), 2 },
    };
    for (const QPair<QString, int> &entry : bases) {
        QPushButton *button = new QPushButton(entry.first, baseBar);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("class", "seg");
        button->setChecked(entry.second == m_currentBase);
        m_baseGroup->addButton(button, entry.second);
        baseLayout->addWidget(button, 1);
    }
    // idToggled 同时覆盖鼠标点击和辅助功能接口切换，避免状态与进制不同步
    connect(m_baseGroup, &QButtonGroup::idToggled, this, [this](int base, bool checked) {
        if (checked) {
            onBaseSelected(base);
        }
    });
    mainLayout->addWidget(baseBar);

    // 进制换算：两行两列的紧凑卡片，数值可选中复制
    QFrame *conversionCard = new QFrame(this);
    conversionCard->setObjectName("calcConversions");
    QGridLayout *convLayout = new QGridLayout(conversionCard);
    convLayout->setContentsMargins(9, 6, 9, 6);
    convLayout->setHorizontalSpacing(10);
    convLayout->setVerticalSpacing(3);
    convLayout->setColumnStretch(1, 1);
    convLayout->setColumnStretch(3, 1);

    auto addConversion = [&](const QString &tag, QLabel *value, int row, int column) {
        value->setObjectName("calcValue");
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // 内容不参与宽度计算：二进制再长也不会把对话框撑开
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QLabel *tagLabel = new QLabel(tag, conversionCard);
        tagLabel->setProperty("class", "calcTag");
        tagLabel->setFixedWidth(46);
        convLayout->addWidget(tagLabel, row, column * 2);
        convLayout->addWidget(value, row, column * 2 + 1);
    };

    addConversion(tr("十六进制"), m_hexLabel = new QLabel("0", conversionCard), 0, 0);
    addConversion(tr("十进制"), m_decLabel = new QLabel("0", conversionCard), 0, 1);
    addConversion(tr("八进制"), m_octLabel = new QLabel("0", conversionCard), 1, 0);
    addConversion(tr("二进制"), m_binLabel = new QLabel("0", conversionCard), 1, 1);
    mainLayout->addWidget(conversionCard);

    // 键盘：5 列 × 5 行，字母区在上、功能键在左、运算键在右
    QGridLayout *keys = new QGridLayout();
    keys->setHorizontalSpacing(6);
    keys->setVerticalSpacing(6);

    auto addKey = [&](const QString &text, const QString &cls, int row, int column,
                      int rowSpan, int columnSpan) {
        QPushButton *button = new QPushButton(text, this);
        button->setProperty("class", cls);
        button->setMinimumSize(52, 38);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        keys->addWidget(button, row, column, rowSpan, columnSpan);
        return button;
    };

    const QStringList hexDigits = {"A", "B", "C", "D", "E", "F"};
    for (int i = 0; i < hexDigits.size(); ++i) {
        QPushButton *button = addKey(hexDigits.at(i), QStringLiteral("calcHex"),
                                     i / 5, i % 5, 1, 1);
        button->setProperty("digit", true);
        connect(button, &QPushButton::clicked, this, &CalculatorDialog::onNumberClicked);
    }

    const QVector<QPair<QString, QPair<int, int>>> digits = {
        { "7", { 1, 1 } }, { "8", { 1, 2 } }, { "9", { 1, 3 } },
        { "4", { 2, 1 } }, { "5", { 2, 2 } }, { "6", { 2, 3 } },
        { "1", { 3, 1 } }, { "2", { 3, 2 } }, { "3", { 3, 3 } },
    };
    for (const auto &digit : digits) {
        QPushButton *button = addKey(digit.first, QStringLiteral("calcNum"),
                                     digit.second.first, digit.second.second, 1, 1);
        button->setProperty("digit", true);
        connect(button, &QPushButton::clicked, this, &CalculatorDialog::onNumberClicked);
    }

    QPushButton *zeroButton = addKey("0", QStringLiteral("calcNum"), 4, 1, 1, 3);
    zeroButton->setProperty("digit", true);
    connect(zeroButton, &QPushButton::clicked, this, &CalculatorDialog::onNumberClicked);

    const QVector<QPair<QString, QPair<QString, int>>> operators = {
        { QStringLiteral("÷"), { "/", 1 } },
        { QStringLiteral("×"), { "*", 2 } },
        { QStringLiteral("−"), { "-", 3 } },
        { QStringLiteral("+"), { "+", 4 } },
    };
    for (const auto &entry : operators) {
        QPushButton *button = addKey(entry.first, QStringLiteral("calcOp"),
                                     entry.second.second, 4, 1, 1);
        button->setProperty("op", entry.second.first);
        connect(button, &QPushButton::clicked, this, &CalculatorDialog::onOperatorClicked);
    }

    QPushButton *clearButton = addKey("AC", QStringLiteral("calcFn"), 2, 0, 1, 1);
    connect(clearButton, &QPushButton::clicked, this, &CalculatorDialog::onClearClicked);

    QPushButton *backspaceButton = addKey(QStringLiteral("←"), QStringLiteral("calcFn"),
                                          3, 0, 1, 1);
    connect(backspaceButton, &QPushButton::clicked,
            this, &CalculatorDialog::onBackspaceClicked);

    QPushButton *equalButton = addKey("=", QStringLiteral("calcEq"), 4, 0, 1, 1);
    connect(equalButton, &QPushButton::clicked, this, &CalculatorDialog::onEqualClicked);

    mainLayout->addLayout(keys, 1);

    updateDigitButtons();
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

    // 按钮显示 ÷ × − 等排版符号，实际运算仍用 ASCII 运算符
    const QString op = btn->property("op").toString();
    m_operator = op.isEmpty() ? btn->text() : op;
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

void CalculatorDialog::onBaseSelected(int base)
{
    if (base == m_currentBase) {
        return;
    }

    // 转换当前值到新进制
    long long value = parseValue(m_currentValue, m_currentBase);
    m_currentBase = base;
    m_currentValue = formatValue(value, m_currentBase);

    updateDigitButtons();
    updateDisplay();
}

void CalculatorDialog::updateDigitButtons()
{
    // 只按数字键上的标记判断有效性，避免把 AC 之类的功能键当成十六进制数字
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
        if (!btn->property("digit").toBool()) {
            continue;
        }
        btn->setEnabled(btn->text().toInt(nullptr, 16) < m_currentBase);
    }
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

