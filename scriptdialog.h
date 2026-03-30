#ifndef SCRIPTDIALOG_H
#define SCRIPTDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class ScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptDialog(QWidget *parent = nullptr);
    ~ScriptDialog();

    QString selectedScriptPath() const { return m_selectedPath; }
    void setRecentScripts(const QStringList &scripts);

private slots:
    void onBrowseClicked();
    void onBuiltinScriptClicked(QListWidgetItem *item);
    void onRecentScriptClicked(QListWidgetItem *item);
    void onPathChanged(const QString &path);
    void onOkClicked();

private:
    void setupUI();
    void loadBuiltinScripts();
    QString getBuiltinScriptPath(const QString &scriptName) const;

    QLineEdit *m_pathEdit;
    QListWidget *m_recentList;
    QListWidget *m_builtinList;
    QPushButton *m_browseButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QLabel *m_descriptionLabel;

    QString m_selectedPath;
    QStringList m_recentScripts;
};

#endif // SCRIPTDIALOG_H
