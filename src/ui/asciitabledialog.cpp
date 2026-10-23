#include "ui/asciitabledialog.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QPushButton>
#include <QClipboard>
#include <QApplication>

AsciiTableDialog::AsciiTableDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    populateTable();
    setWindowTitle(tr("ASCII 码表"));
    resize(700, 600);
}

AsciiTableDialog::~AsciiTableDialog()
{
}

void AsciiTableDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);

    // 头部：标题 + 说明
    QLabel *titleLabel = new QLabel(tr("ASCII 码表"), this);
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(tr("查询 0–255 的十进制 / 十六进制 / 字符对照，点击行即可复制"), this);
    hintLabel->setProperty("class", "hint");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    // 搜索框
    QHBoxLayout *searchLayout = new QHBoxLayout();
    searchLayout->setSpacing(8);
    searchLayout->addWidget(new QLabel(tr("搜索:"), this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("输入十进制或十六进制值..."));
    m_searchEdit->addAction(IconProvider::themedIcon("search"), QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &AsciiTableDialog::onSearchTextChanged);
    searchLayout->addWidget(m_searchEdit, 1);
    mainLayout->addLayout(searchLayout);

    // ASCII表格
    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("十进制"), tr("十六进制"), tr("字符"), tr("描述")});
    m_table->setRowCount(256);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 行号与"十进制"列重复，隐藏纵向表头；关闭网格线，改用留白分隔（更接近移动端列表）
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    connect(m_table, &QTableWidget::cellClicked,
            this, &AsciiTableDialog::onCellClicked);

    mainLayout->addWidget(m_table, 1);

    // 底部：说明 + 关闭
    QHBoxLayout *footerLayout = new QHBoxLayout();
    footerLayout->setSpacing(8);
    QLabel *infoLabel = new QLabel(
        tr("单击某一行可将该字符复制到剪贴板。控制字符（0-31、127）以名称显示。"),
        this);
    infoLabel->setWordWrap(true);
    infoLabel->setProperty("class", "hint");
    QPushButton *closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setProperty("class", "primary");
    closeButton->setDefault(true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    footerLayout->addWidget(infoLabel, 1);
    footerLayout->addWidget(closeButton, 0, Qt::AlignVCenter);
    mainLayout->addLayout(footerLayout);
}

void AsciiTableDialog::populateTable()
{
    for (int i = 0; i < 256; ++i) {
        // DEC列
        QTableWidgetItem *decItem = new QTableWidgetItem(QString::number(i));
        decItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 0, decItem);

        // HEX列
        QTableWidgetItem *hexItem = new QTableWidgetItem(
            QString("0x%1").arg(i, 2, 16, QChar('0')).toUpper());
        hexItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 1, hexItem);

        // Char列
        QString charStr;
        if (i < 32 || i == 127) {
            // 控制字符
            charStr = QString("<%1>").arg(getControlCharName(i));
        } else if (i >= 32 && i < 127) {
            // 可打印ASCII字符
            charStr = QString(QChar(i));
        } else {
            // 扩展ASCII
            charStr = QString(QChar(i));
        }
        QTableWidgetItem *charItem = new QTableWidgetItem(charStr);
        charItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 2, charItem);

        // Description列
        QString desc;
        if (i < 32 || i == 127) {
            desc = getControlCharName(i);
        } else if (i >= 32 && i < 127) {
            desc = tr("可打印字符");
        } else {
            desc = tr("扩展 ASCII");
        }
        QTableWidgetItem *descItem = new QTableWidgetItem(desc);
        m_table->setItem(i, 3, descItem);
    }

    m_table->resizeColumnsToContents();
}

QString AsciiTableDialog::getControlCharName(int code)
{
    static const QMap<int, QString> controlChars = {
        {0, "NUL (Null)"},
        {1, "SOH (Start of Heading)"},
        {2, "STX (Start of Text)"},
        {3, "ETX (End of Text)"},
        {4, "EOT (End of Transmission)"},
        {5, "ENQ (Enquiry)"},
        {6, "ACK (Acknowledge)"},
        {7, "BEL (Bell)"},
        {8, "BS (Backspace)"},
        {9, "HT (Horizontal Tab)"},
        {10, "LF (Line Feed)"},
        {11, "VT (Vertical Tab)"},
        {12, "FF (Form Feed)"},
        {13, "CR (Carriage Return)"},
        {14, "SO (Shift Out)"},
        {15, "SI (Shift In)"},
        {16, "DLE (Data Link Escape)"},
        {17, "DC1 (Device Control 1)"},
        {18, "DC2 (Device Control 2)"},
        {19, "DC3 (Device Control 3)"},
        {20, "DC4 (Device Control 4)"},
        {21, "NAK (Negative Acknowledge)"},
        {22, "SYN (Synchronous Idle)"},
        {23, "ETB (End of Trans. Block)"},
        {24, "CAN (Cancel)"},
        {25, "EM (End of Medium)"},
        {26, "SUB (Substitute)"},
        {27, "ESC (Escape)"},
        {28, "FS (File Separator)"},
        {29, "GS (Group Separator)"},
        {30, "RS (Record Separator)"},
        {31, "US (Unit Separator)"},
        {127, "DEL (Delete)"}
    };

    return controlChars.value(code, tr("未知"));
}

void AsciiTableDialog::onSearchTextChanged(const QString &text)
{
    if (text.isEmpty()) {
        // 清除所有高亮
        for (int i = 0; i < m_table->rowCount(); ++i) {
            for (int j = 0; j < m_table->columnCount(); ++j) {
                QTableWidgetItem *item = m_table->item(i, j);
                if (item) {
                    item->setData(Qt::BackgroundRole, QVariant());
                }
            }
        }
        return;
    }

    bool ok;
    int value = -1;

    // 尝试解析为十进制
    value = text.toInt(&ok, 10);
    if (!ok || value < 0 || value > 255) {
        // 尝试解析为十六进制
        QString hexText = text;
        if (hexText.startsWith("0x", Qt::CaseInsensitive)) {
            hexText = hexText.mid(2);
        }
        value = hexText.toInt(&ok, 16);
    }

    if (ok && value >= 0 && value < 256) {
        // 高亮对应行（跟随主题强调色）
        const QColor accent = ThemeManager::instance()->currentAppTheme().accent;
        QColor highlight = accent;
        highlight.setAlphaF(0.22);
        for (int i = 0; i < m_table->rowCount(); ++i) {
            for (int j = 0; j < m_table->columnCount(); ++j) {
                QTableWidgetItem *item = m_table->item(i, j);
                if (item) {
                    item->setData(Qt::BackgroundRole,
                                  (i == value) ? QVariant(highlight) : QVariant());
                }
            }
        }
        m_table->scrollToItem(m_table->item(value, 0));
    }
}

void AsciiTableDialog::onCellClicked(int row, int column)
{
    Q_UNUSED(column);

    if (row >= 0 && row < 256) {
        // 复制字符到剪贴板
        QString charStr;
        if (row >= 32 && row < 127) {
            charStr = QString(QChar(row));
        } else {
            charStr = QString::number(row);
        }

        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(charStr);

        // 显示提示
        m_table->setToolTip(tr("已复制: %1 (十进制: %2, 十六进制: 0x%3)")
                            .arg(charStr)
                            .arg(row)
                            .arg(row, 2, 16, QChar('0')).toUpper());
    }
}
