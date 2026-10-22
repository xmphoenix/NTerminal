#ifndef CALCULATORDIALOG_H
#define CALCULATORDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>

class CalculatorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CalculatorDialog(QWidget *parent = nullptr);
    ~CalculatorDialog();

private slots:
    void onNumberClicked();
    void onOperatorClicked();
    void onEqualClicked();
    void onClearClicked();
    void onBaseChanged(int index);
    void onBackspaceClicked();

private:
    void setupUi();
    void updateDisplay();
    QString convertBase(const QString &value, int fromBase, int toBase);
    long long parseValue(const QString &value, int base);
    QString formatValue(long long value, int base);

    QLineEdit *m_display;
    QComboBox *m_baseCombo;
    QLabel *m_hexLabel;
    QLabel *m_decLabel;
    QLabel *m_octLabel;
    QLabel *m_binLabel;

    QString m_currentValue;
    QString m_operator;
    long long m_operand;
    int m_currentBase;
    bool m_newInput;
};

#endif // CALCULATORDIALOG_H
