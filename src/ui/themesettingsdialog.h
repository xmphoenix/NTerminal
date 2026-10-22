#ifndef THEMESETTINGSDIALOG_H
#define THEMESETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>
#include <QSpinBox>
#include "theme/thememanager.h"
#include "theme/fontmanager.h"

class ThemeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeSettingsDialog(QWidget *parent = nullptr);
    ~ThemeSettingsDialog();

private slots:
    void onAppThemeChanged(int index);
    void onTerminalThemeChanged(int index);
    void onFontChanged(int index);
    void onFontSizeChanged(int size);
    void onApply();
    void onOk();
    void onCancel();

private:
    void setupUi();
    void loadThemes();
    void loadFonts();
    void updatePreview();
    void updateColorPreview(QGridLayout *layout, const TerminalTheme &theme);
    void updateFontPreview();

    QComboBox *m_appThemeCombo;
    QComboBox *m_terminalThemeCombo;
    QComboBox *m_fontCombo;
    QSpinBox *m_fontSizeSpinBox;

    QLabel *m_appThemePreview;
    QLabel *m_fontPreview;
    QGroupBox *m_terminalPreviewGroup;
    QGridLayout *m_colorLayout;

    QPushButton *m_applyButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;

    QString m_selectedAppThemeId;
    QString m_selectedTerminalThemeId;
    QString m_selectedFontId;
    int m_selectedFontSize;
};

#endif // THEMESETTINGSDIALOG_H
