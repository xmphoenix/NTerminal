#ifndef ASCIITABLEDIALOG_H
#define ASCIITABLEDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>

class AsciiTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AsciiTableDialog(QWidget *parent = nullptr);
    ~AsciiTableDialog();

private slots:
    void onSearchTextChanged(const QString &text);
    void onCellClicked(int row, int column);

private:
    void setupUi();
    void populateTable();
    QString getControlCharName(int code);

    QTableWidget *m_table;
    QLineEdit *m_searchEdit;
};

#endif // ASCIITABLEDIALOG_H
