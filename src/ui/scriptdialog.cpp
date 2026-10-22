#include "ui/scriptdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDebug>

ScriptDialog::ScriptDialog(QWidget *parent)
    : QDialog(parent)
    , m_pathEdit(nullptr)
    , m_recentList(nullptr)
    , m_builtinList(nullptr)
    , m_browseButton(nullptr)
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
    , m_descriptionLabel(nullptr)
{
    setWindowTitle(tr("选择 Python 脚本"));
    setMinimumWidth(500);
    setMinimumHeight(400);

    setupUI();
    loadBuiltinScripts();
}

ScriptDialog::~ScriptDialog()
{
}

void ScriptDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 最近使用的脚本
    QGroupBox *recentGroup = new QGroupBox(tr("最近使用"), this);
    QVBoxLayout *recentLayout = new QVBoxLayout(recentGroup);

    m_recentList = new QListWidget(this);
    m_recentList->setMaximumHeight(100);
    connect(m_recentList, &QListWidget::itemClicked,
            this, &ScriptDialog::onRecentScriptClicked);
    recentLayout->addWidget(m_recentList);

    mainLayout->addWidget(recentGroup);

    // 内置脚本
    QGroupBox *builtinGroup = new QGroupBox(tr("内置脚本"), this);
    QVBoxLayout *builtinLayout = new QVBoxLayout(builtinGroup);

    m_builtinList = new QListWidget(this);
    connect(m_builtinList, &QListWidget::itemClicked,
            this, &ScriptDialog::onBuiltinScriptClicked);
    builtinLayout->addWidget(m_builtinList);

    mainLayout->addWidget(builtinGroup);

    // 脚本描述
    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet("QLabel { color: gray; font-style: italic; }");
    mainLayout->addWidget(m_descriptionLabel);

    // 浏览文件
    QHBoxLayout *browseLayout = new QHBoxLayout();
    m_browseButton = new QPushButton(tr("浏览文件..."), this);
    connect(m_browseButton, &QPushButton::clicked,
            this, &ScriptDialog::onBrowseClicked);
    browseLayout->addWidget(m_browseButton);
    browseLayout->addStretch();
    mainLayout->addLayout(browseLayout);

    // 脚本路径
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(new QLabel(tr("脚本路径:"), this));
    m_pathEdit = new QLineEdit(this);
    connect(m_pathEdit, &QLineEdit::textChanged,
            this, &ScriptDialog::onPathChanged);
    pathLayout->addWidget(m_pathEdit);
    mainLayout->addLayout(pathLayout);

    // 按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_okButton = new QPushButton(tr("确定"), this);
    m_okButton->setEnabled(false);
    connect(m_okButton, &QPushButton::clicked,
            this, &ScriptDialog::onOkClicked);
    buttonLayout->addWidget(m_okButton);

    m_cancelButton = new QPushButton(tr("取消"), this);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(buttonLayout);
}

void ScriptDialog::loadBuiltinScripts()
{
    // 获取内置脚本目录
    QString appDir = QApplication::applicationDirPath();
    QString scriptsDir = appDir + "/scripts/builtin";

    // 如果 builtin 目录不存在，尝试 scripts 目录
    if (!QDir(scriptsDir).exists()) {
        scriptsDir = appDir + "/scripts";
    }

    qDebug() << "[ScriptDialog] Looking for builtin scripts in:" << scriptsDir;

    // 定义内置脚本
    struct BuiltinScript {
        QString fileName;
        QString displayName;
        QString description;
    };

    QList<BuiltinScript> builtinScripts = {
        {"simple_example.py", tr("简单示例脚本"),
         tr("演示基本的行处理和模式匹配功能")},
        {"auto_login.py", tr("自动登录脚本"),
         tr("自动检测登录提示并输入用户名和密码")},
        {"log_monitor.py", tr("日志监控脚本"),
         tr("监控终端输出中的错误和警告信息")},
        {"test_automation.py", tr("自动化测试脚本"),
         tr("运行一系列测试命令并验证输出")}
    };

    for (const auto &script : builtinScripts) {
        QString fullPath = scriptsDir + "/" + script.fileName;

        QListWidgetItem *item = new QListWidgetItem(script.displayName);
        item->setData(Qt::UserRole, fullPath);
        item->setData(Qt::UserRole + 1, script.description);

        // 检查文件是否存在
        if (QFileInfo::exists(fullPath)) {
            item->setToolTip(fullPath);
        } else {
            item->setForeground(Qt::gray);
            item->setToolTip(tr("脚本文件不存在: %1").arg(fullPath));
        }

        m_builtinList->addItem(item);
    }
}

void ScriptDialog::setRecentScripts(const QStringList &scripts)
{
    m_recentScripts = scripts;
    m_recentList->clear();

    for (const QString &scriptPath : scripts) {
        QFileInfo fileInfo(scriptPath);
        if (fileInfo.exists()) {
            QListWidgetItem *item = new QListWidgetItem(fileInfo.fileName());
            item->setData(Qt::UserRole, scriptPath);
            item->setToolTip(scriptPath);
            m_recentList->addItem(item);
        }
    }

    if (m_recentList->count() == 0) {
        QListWidgetItem *item = new QListWidgetItem(tr("(无最近使用的脚本)"));
        item->setForeground(Qt::gray);
        item->setFlags(Qt::NoItemFlags);
        m_recentList->addItem(item);
    }
}

void ScriptDialog::onBrowseClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("选择 Python 脚本"),
        QDir::homePath(),
        tr("Python 脚本 (*.py);;所有文件 (*)")
    );

    if (!fileName.isEmpty()) {
        m_pathEdit->setText(fileName);
        m_selectedPath = fileName;
    }
}

void ScriptDialog::onBuiltinScriptClicked(QListWidgetItem *item)
{
    if (!item) return;

    QString scriptPath = item->data(Qt::UserRole).toString();
    QString description = item->data(Qt::UserRole + 1).toString();

    if (!scriptPath.isEmpty()) {
        m_pathEdit->setText(scriptPath);
        m_selectedPath = scriptPath;
        m_descriptionLabel->setText(description);

        // 取消最近使用列表的选择
        m_recentList->clearSelection();
    }
}

void ScriptDialog::onRecentScriptClicked(QListWidgetItem *item)
{
    if (!item) return;

    QString scriptPath = item->data(Qt::UserRole).toString();

    if (!scriptPath.isEmpty()) {
        m_pathEdit->setText(scriptPath);
        m_selectedPath = scriptPath;
        m_descriptionLabel->clear();

        // 取消内置脚本列表的选择
        m_builtinList->clearSelection();
    }
}

void ScriptDialog::onPathChanged(const QString &path)
{
    m_selectedPath = path;

    // 检查文件是否存在
    bool exists = QFileInfo::exists(path);
    m_okButton->setEnabled(!path.isEmpty() && exists);

    if (!path.isEmpty() && !exists) {
        m_descriptionLabel->setText(tr("⚠ 文件不存在"));
        m_descriptionLabel->setStyleSheet("QLabel { color: red; }");
    } else if (exists) {
        m_descriptionLabel->setStyleSheet("QLabel { color: gray; font-style: italic; }");
    }
}

void ScriptDialog::onOkClicked()
{
    if (!m_selectedPath.isEmpty() && QFileInfo::exists(m_selectedPath)) {
        accept();
    }
}

QString ScriptDialog::getBuiltinScriptPath(const QString &scriptName) const
{
    QString appDir = QApplication::applicationDirPath();
    QString scriptsDir = appDir + "/scripts/builtin";

    if (!QDir(scriptsDir).exists()) {
        scriptsDir = appDir + "/scripts";
    }

    return scriptsDir + "/" + scriptName;
}
