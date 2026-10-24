#include "ui/updatesettingsdialog.h"

#include "update/updatemanager.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

UpdateSettingsDialog::UpdateSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("更新设置"));
    setModal(true);
    resize(620, 360);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);

    QLabel *title = new QLabel(tr("更新设置"), this);
    title->setProperty("class", "sectionTitle");
    layout->addWidget(title);

    QLabel *hint = new QLabel(
        tr("NTerminal 通过 HTTPS 读取一个 update.json 清单，比较版本后下载 zip 更新包。"
           "更新包只替换程序文件，用户配置保存在 ~/.nterminal，不会丢失。"),
        this);
    hint->setProperty("class", "hint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QFormLayout *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    m_feedEdit = new QLineEdit(this);
    m_feedEdit->setPlaceholderText(
        QStringLiteral("https://example.com/nterminal/update.json"));
    m_feedEdit->setText(UpdateManager::feedUrl());
    form->addRow(tr("更新源地址:"), m_feedEdit);

    QLabel *version = new QLabel(UpdateManager::currentVersion(), this);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("当前版本:"), version);

    m_autoCheck = new QCheckBox(tr("启动时自动检查更新"), this);
    m_autoCheck->setChecked(UpdateManager::autoCheckEnabled());
    form->addRow(QString(), m_autoCheck);

    layout->addLayout(form);

    QLabel *format = new QLabel(
        tr("清单格式：{\"version\":\"2.1.0\",\"notes\":\"更新说明\","
           "\"platforms\":{\"windows-x86_64\":{\"url\":\"https://.../NTerminal-2.1.0.zip\","
           "\"sha256\":\"<64位十六进制>\",\"size\":123456}}}"),
        this);
    format->setProperty("class", "hint");
    format->setWordWrap(true);
    format->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(format);

    QPushButton *logButton = new QPushButton(tr("查看更新日志"), this);
    logButton->setProperty("class", "ghost");
    connect(logButton, &QPushButton::clicked, this,
            &UpdateSettingsDialog::openUpdaterLog);

    layout->addStretch();

    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->addWidget(logButton);
    bottom->addStretch();
    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this,
            &UpdateSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this,
            &UpdateSettingsDialog::reject);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
}

void UpdateSettingsDialog::accept()
{
    const QString text = m_feedEdit->text().trimmed();
    if (!text.isEmpty()) {
        const QUrl url(text);
        if (!url.isValid() || !UpdateManager::isSecureUrl(url)) {
            QMessageBox::warning(
                this, tr("更新设置"),
                tr("更新源必须是 HTTPS 地址；本地测试可使用 "
                   "http://127.0.0.1 或 file:// 地址。"));
            return;
        }
    }

    UpdateManager::setFeedUrl(text);
    UpdateManager::setAutoCheckEnabled(m_autoCheck->isChecked());
    QDialog::accept();
}

void UpdateSettingsDialog::openUpdaterLog()
{
    const QString path = UpdateManager::updaterLogPath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
