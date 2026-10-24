#include "ui/aboutdialog.h"
#include "common/appinfo.h"
#include "theme/thememanager.h"
#include "ui/iconprovider.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

namespace {

QLabel *makeCaption(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("class", "subheading");
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QLabel *makeValue(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    // 版本/平台等字符串允许选中复制，便于反馈问题时直接粘进邮件或工单
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QFrame *makeDivider(QWidget *parent)
{
    QFrame *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("divider"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    return line;
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("关于 NTerminal"));
    setModal(true);

    const AppTheme theme = ThemeManager::instance()->currentAppTheme();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);

    // ---- 头部：图标 + 名称 / 标语 / 版本 ----
    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(14);

    QLabel *iconChip = new QLabel(this);
    iconChip->setObjectName(QStringLiteral("aboutIcon"));
    iconChip->setAlignment(Qt::AlignCenter);
    iconChip->setFixedSize(56, 56);
    iconChip->setPixmap(IconProvider::pixmap(QStringLiteral("terminal"),
                                             theme.accentText, 28,
                                             devicePixelRatioF()));
    header->addWidget(iconChip, 0, Qt::AlignTop);

    QVBoxLayout *identity = new QVBoxLayout;
    identity->setSpacing(2);

    QLabel *title = new QLabel(QStringLiteral("NTerminal"), this);
    title->setObjectName(QStringLiteral("aboutTitle"));
    identity->addWidget(title);

    QLabel *subtitle = new QLabel(tr("多协议终端工具"), this);
    subtitle->setProperty("class", "hint");
    identity->addWidget(subtitle);

    QLabel *version = new QLabel(tr("版本 %1").arg(AppInfo::version()), this);
    version->setObjectName(QStringLiteral("aboutVersion"));
    identity->addWidget(version);

    identity->addStretch();
    header->addLayout(identity, 1);
    layout->addLayout(header);

    layout->addWidget(makeDivider(this));

    // ---- 运行环境（标签 / 值 两列对齐） ----
    QGridLayout *info = new QGridLayout;
    info->setHorizontalSpacing(20);
    info->setVerticalSpacing(7);
    info->setColumnStretch(1, 1);

    int row = 0;
    info->addWidget(makeCaption(tr("Qt 版本"), this), row, 0);
    info->addWidget(makeValue(QString::fromLatin1(qVersion()), this), row++, 1);

    info->addWidget(makeCaption(tr("运行平台"), this), row, 0);
    info->addWidget(makeValue(QSysInfo::prettyProductName(), this), row++, 1);

    info->addWidget(makeCaption(tr("支持协议"), this), row, 0);
    info->addWidget(makeValue(QStringLiteral("Serial / Telnet / SSH / RLogin / SFTP / FTP / TFTP"),
                              this),
                    row++, 1);

    layout->addLayout(info);

    layout->addWidget(makeDivider(this));

    // ---- 底部：确定的默认按钮（QSS 按 :default 套用强调色） ----
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    connect(buttons, &QDialogButtonBox::accepted, this, &AboutDialog::accept);
    layout->addWidget(buttons);

    setFixedWidth(460);
    adjustSize();
}
