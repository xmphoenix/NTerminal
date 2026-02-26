#include "themesettingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QScrollArea>

ThemeSettingsDialog::ThemeSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_appThemeCombo(new QComboBox(this))
    , m_terminalThemeCombo(new QComboBox(this))
    , m_appThemePreview(new QLabel(this))
    , m_terminalPreviewGroup(new QGroupBox(tr("Terminal Theme Preview"), this))
    , m_colorLayout(new QGridLayout())
    , m_applyButton(new QPushButton(tr("Apply"), this))
    , m_okButton(new QPushButton(tr("OK"), this))
    , m_cancelButton(new QPushButton(tr("Cancel"), this))
{
    setupUi();
    loadThemes();
    updatePreview();
}

ThemeSettingsDialog::~ThemeSettingsDialog()
{
}

void ThemeSettingsDialog::setupUi()
{
    setWindowTitle(tr("Theme Settings"));
    setMinimumSize(600, 500);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QGroupBox *appThemeGroup = new QGroupBox(tr("Application Theme"), this);
    QFormLayout *appThemeLayout = new QFormLayout(appThemeGroup);
    appThemeLayout->addRow(tr("Theme:"), m_appThemeCombo);
    
    m_appThemePreview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_appThemePreview->setMinimumHeight(60);
    m_appThemePreview->setAlignment(Qt::AlignCenter);
    appThemeLayout->addRow(tr("Preview:"), m_appThemePreview);
    
    mainLayout->addWidget(appThemeGroup);
    
    QGroupBox *terminalThemeGroup = new QGroupBox(tr("Terminal Theme"), this);
    QFormLayout *terminalThemeLayout = new QFormLayout(terminalThemeGroup);
    terminalThemeLayout->addRow(tr("Theme:"), m_terminalThemeCombo);
    
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
    
    QLabel *bgLabel = new QLabel(tr("Background"));
    bgLabel->setAlignment(Qt::AlignCenter);
    bgLabel->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #666; padding: 4px;")
                           .arg(theme.defaultBackground.name())
                           .arg(theme.defaultForeground.name()));
    layout->addWidget(bgLabel, 0, 0, 1, 2);
    
    QLabel *fgLabel = new QLabel(tr("Foreground"));
    fgLabel->setAlignment(Qt::AlignCenter);
    fgLabel->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #666; padding: 4px;")
                           .arg(theme.defaultForeground.name())
                           .arg(theme.defaultBackground.name()));
    layout->addWidget(fgLabel, 0, 2, 1, 2);
    
    QStringList colorNames = {
        tr("Black"), tr("Red"), tr("Green"), tr("Yellow"),
        tr("Blue"), tr("Magenta"), tr("Cyan"), tr("White")
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
    
    QLabel *brightTitle = new QLabel(tr("Bright Colors"));
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

void ThemeSettingsDialog::onApply()
{
    ThemeManager *tm = ThemeManager::instance();
    tm->setAppTheme(m_selectedAppThemeId);
    tm->setTerminalTheme(m_selectedTerminalThemeId);
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
