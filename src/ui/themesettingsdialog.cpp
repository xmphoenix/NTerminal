#include "ui/themesettingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QScrollArea>
#include <QFontDatabase>

ThemeSettingsDialog::ThemeSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_appThemeCombo(new QComboBox(this))
    , m_terminalThemeCombo(new QComboBox(this))
    , m_fontCombo(new QComboBox(this))
    , m_fontSizeSpinBox(new QSpinBox(this))
    , m_appThemePreview(new QLabel(this))
    , m_fontPreview(new QLabel(this))
    , m_terminalPreviewGroup(new QGroupBox(tr("终端主题预览"), this))
    , m_colorLayout(new QGridLayout())
    , m_applyButton(new QPushButton(tr("应用"), this))
    , m_okButton(new QPushButton(tr("确定"), this))
    , m_cancelButton(new QPushButton(tr("取消"), this))
    , m_selectedFontSize(10)
{
    setupUi();
    loadThemes();
    loadFonts();
    updatePreview();
}

ThemeSettingsDialog::~ThemeSettingsDialog()
{
}

void ThemeSettingsDialog::setupUi()
{
    setWindowTitle(tr("主题设置"));
    setMinimumSize(650, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *appThemeGroup = new QGroupBox(tr("应用主题"), this);
    QFormLayout *appThemeLayout = new QFormLayout(appThemeGroup);
    appThemeLayout->addRow(tr("主题:"), m_appThemeCombo);

    m_appThemePreview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_appThemePreview->setMinimumHeight(60);
    m_appThemePreview->setAlignment(Qt::AlignCenter);
    appThemeLayout->addRow(tr("预览:"), m_appThemePreview);

    mainLayout->addWidget(appThemeGroup);

    QGroupBox *fontGroup = new QGroupBox(tr("字体设置"), this);
    QFormLayout *fontLayout = new QFormLayout(fontGroup);

    m_fontSizeSpinBox->setRange(8, 72);
    m_fontSizeSpinBox->setValue(10);

    QHBoxLayout *fontSizeLayout = new QHBoxLayout();
    fontSizeLayout->addWidget(m_fontCombo);
    fontSizeLayout->addWidget(new QLabel(tr("字号:")));
    fontSizeLayout->addWidget(m_fontSizeSpinBox);

    fontLayout->addRow(tr("字体:"), fontSizeLayout);

    m_fontPreview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_fontPreview->setMinimumHeight(60);
    m_fontPreview->setAlignment(Qt::AlignCenter);
    m_fontPreview->setText("The quick brown fox jumps over the lazy dog. 0123456789");
    fontLayout->addRow(tr("预览:"), m_fontPreview);

    mainLayout->addWidget(fontGroup);

    QGroupBox *terminalThemeGroup = new QGroupBox(tr("终端主题"), this);
    QFormLayout *terminalThemeLayout = new QFormLayout(terminalThemeGroup);
    terminalThemeLayout->addRow(tr("主题:"), m_terminalThemeCombo);

    mainLayout->addWidget(terminalThemeGroup);

    QWidget *colorPreviewWidget = new QWidget();
    m_colorLayout->setSpacing(4);
    m_colorLayout->setContentsMargins(8, 8, 8, 8);
    colorPreviewWidget->setLayout(m_colorLayout);

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(colorPreviewWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(200);

    QVBoxLayout *previewLayout = new QVBoxLayout(m_terminalPreviewGroup);
    previewLayout->addWidget(scrollArea);

    mainLayout->addWidget(m_terminalPreviewGroup);

    mainLayout->addStretch();

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(buttonLayout);

    connect(m_appThemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThemeSettingsDialog::onAppThemeChanged);
    connect(m_terminalThemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThemeSettingsDialog::onTerminalThemeChanged);
    connect(m_fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThemeSettingsDialog::onFontChanged);
    connect(m_fontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ThemeSettingsDialog::onFontSizeChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &ThemeSettingsDialog::onApply);
    connect(m_okButton, &QPushButton::clicked, this, &ThemeSettingsDialog::onOk);
    connect(m_cancelButton, &QPushButton::clicked, this, &ThemeSettingsDialog::onCancel);
}

void ThemeSettingsDialog::loadThemes()
{
    ThemeManager *tm = ThemeManager::instance();

    m_appThemeCombo->clear();
    QVector<AppTheme> appThemes = tm->appThemes();
    for (const AppTheme &theme : appThemes) {
        m_appThemeCombo->addItem(theme.name, theme.id);
        if (theme.id == tm->currentAppThemeId()) {
            m_appThemeCombo->setCurrentIndex(m_appThemeCombo->count() - 1);
        }
    }

    m_terminalThemeCombo->clear();
    QVector<TerminalTheme> terminalThemes = tm->terminalThemes();
    for (const TerminalTheme &theme : terminalThemes) {
        m_terminalThemeCombo->addItem(theme.name, theme.id);
        if (theme.id == tm->currentTerminalThemeId()) {
            m_terminalThemeCombo->setCurrentIndex(m_terminalThemeCombo->count() - 1);
        }
    }

    m_selectedAppThemeId = tm->currentAppThemeId();
    m_selectedTerminalThemeId = tm->currentTerminalThemeId();
}

void ThemeSettingsDialog::loadFonts()
{
    FontManager *fm = FontManager::instance();

    m_fontCombo->clear();
    QVector<FontInfo> fonts = fm->availableFonts();
    for (const FontInfo &font : fonts) {
        m_fontCombo->addItem(font.displayName, font.id);
        if (font.id == fm->currentFontId()) {
            m_fontCombo->setCurrentIndex(m_fontCombo->count() - 1);
        }
    }

    m_fontSizeSpinBox->setValue(fm->currentFontSize());

    m_selectedFontId = fm->currentFontId();
    m_selectedFontSize = fm->currentFontSize();

    updateFontPreview();
}

void ThemeSettingsDialog::updateFontPreview()
{
    FontManager *fm = FontManager::instance();

    if (m_fontCombo->count() > 0) {
        QString fontId = m_fontCombo->currentData().toString();
        int fontSize = m_fontSizeSpinBox->value();

        QFont font = fm->font(fontId, fontSize);
        m_fontPreview->setFont(font);

        m_selectedFontId = fontId;
        m_selectedFontSize = fontSize;
    }
}

void ThemeSettingsDialog::updatePreview()
{
    ThemeManager *tm = ThemeManager::instance();
    
    if (m_appThemeCombo->count() > 0) {
        QString appThemeId = m_appThemeCombo->currentData().toString();
        AppTheme appTheme = tm->appTheme(appThemeId);
        
        QString previewStyle = QString(
            "QLabel {"
            "  background-color: %1;"
            "  color: %2;"
            "  border: 2px solid %3;"
            "  border-radius: 4px;"
            "  padding: 8px;"
            "}"
        )
        .arg(appTheme.windowBackground.name())
        .arg(appTheme.windowText.name())
        .arg(appTheme.highlight.name());
        
        m_appThemePreview->setStyleSheet(previewStyle);
        m_appThemePreview->setText(appTheme.name);
    }
    
    if (m_terminalThemeCombo->count() > 0) {
        QString terminalThemeId = m_terminalThemeCombo->currentData().toString();
        TerminalTheme terminalTheme = tm->terminalTheme(terminalThemeId);
        updateColorPreview(m_colorLayout, terminalTheme);
    }
}

void ThemeSettingsDialog::updateColorPreview(QGridLayout *layout, const TerminalTheme &theme)
{
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    QLabel *bgLabel = new QLabel(tr("背景"));
    bgLabel->setAlignment(Qt::AlignCenter);
    bgLabel->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #666; padding: 4px;")
                           .arg(theme.defaultBackground.name())
                           .arg(theme.defaultForeground.name()));
    layout->addWidget(bgLabel, 0, 0, 1, 2);
    
    QLabel *fgLabel = new QLabel(tr("前景"));
    fgLabel->setAlignment(Qt::AlignCenter);
    fgLabel->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #666; padding: 4px;")
                           .arg(theme.defaultForeground.name())
                           .arg(theme.defaultBackground.name()));
    layout->addWidget(fgLabel, 0, 2, 1, 2);
    
    QStringList colorNames = {
        tr("黑"), tr("红"), tr("绿"), tr("黄"),
        tr("蓝"), tr("品红"), tr("青"), tr("白")
    };
    
    for (int i = 0; i < 8; ++i) {
        QLabel *label = new QLabel(colorNames[i]);
        label->setAlignment(Qt::AlignCenter);
        
        QColor bgColor = theme.ansiColors[i];
        QColor fgColor = (bgColor.lightness() > 128) ? QColor(0, 0, 0) : QColor(255, 255, 255);
        
        label->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #666; padding: 4px;")
                            .arg(bgColor.name())
                            .arg(fgColor.name()));
        
        layout->addWidget(label, 1 + i / 4, i % 4);
    }
    
    QLabel *brightTitle = new QLabel(tr("亮色"));
    brightTitle->setAlignment(Qt::AlignCenter);
    brightTitle->setStyleSheet("font-weight: bold; padding: 4px;");
    layout->addWidget(brightTitle, 3, 0, 1, 4);
    
    for (int i = 0; i < 8; ++i) {
        QLabel *label = new QLabel(QString("%1").arg(i + 8));
        label->setAlignment(Qt::AlignCenter);
        
        QColor bgColor = theme.ansiColors[i + 8];
        QColor fgColor = (bgColor.lightness() > 128) ? QColor(0, 0, 0) : QColor(255, 255, 255);
        
        label->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #666; padding: 4px;")
                            .arg(bgColor.name())
                            .arg(fgColor.name()));
        
        layout->addWidget(label, 4 + i / 4, i % 4);
    }
}

void ThemeSettingsDialog::onAppThemeChanged(int index)
{
    Q_UNUSED(index);
    m_selectedAppThemeId = m_appThemeCombo->currentData().toString();
    updatePreview();
}

void ThemeSettingsDialog::onTerminalThemeChanged(int index)
{
    Q_UNUSED(index);
    m_selectedTerminalThemeId = m_terminalThemeCombo->currentData().toString();
    updatePreview();
}

void ThemeSettingsDialog::onFontChanged(int index)
{
    Q_UNUSED(index);
    m_selectedFontId = m_fontCombo->currentData().toString();
    updateFontPreview();
}

void ThemeSettingsDialog::onFontSizeChanged(int size)
{
    m_selectedFontSize = size;
    updateFontPreview();
}

void ThemeSettingsDialog::onApply()
{
    ThemeManager *tm = ThemeManager::instance();
    FontManager *fm = FontManager::instance();

    tm->setAppTheme(m_selectedAppThemeId);
    tm->setTerminalTheme(m_selectedTerminalThemeId);
    fm->setCurrentFont(m_selectedFontId);
    fm->setCurrentFontSize(m_selectedFontSize);
}

void ThemeSettingsDialog::onOk()
{
    onApply();
    accept();
}

void ThemeSettingsDialog::onCancel()
{
    reject();
}
