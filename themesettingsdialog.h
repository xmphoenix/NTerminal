#ifndef THEMESETTINGSDIALOG_H
#define THEMESETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>
#include "thememanager.h"

class ThemeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeSettingsDialog(QWidget *parent = nullptr);
    ~ThemeSettingsDialog();

private slots:
    void onAppThemeChanged(int index);
    void onTerminalThemeChanged(int index);
    void onApply();
    void onOk();
    void onCancel();

private:
    void setupUi();
    void loadThemes();
    void updatePreview();
    void updateColorPreview(QGridLayout *layout, const TerminalTheme &theme);
    
    QComboBox *m_appThemeCombo;
    QComboBox *m_terminalThemeCombo;
    
    QLabel *m_appThemePreview;
    QGroupBox *m_terminalPreviewGroup;
    QGridLayout *m_colorLayout;
    
    QPushButton *m_applyButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    
    QString m_selectedAppThemeId;
    QString m_selectedTerminalThemeId;
};

#endif // THEMESETTINGSDIALOG_H
