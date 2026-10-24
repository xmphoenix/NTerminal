#ifndef UPDATESETTINGSDIALOG_H
#define UPDATESETTINGSDIALOG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;

// ============================================================================
// 更新设置：更新源地址与启动时自动检查
// ============================================================================
class UpdateSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateSettingsDialog(QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    void openUpdaterLog();

    QLineEdit *m_feedEdit = nullptr;
    QCheckBox *m_autoCheck = nullptr;
};

#endif // UPDATESETTINGSDIALOG_H
