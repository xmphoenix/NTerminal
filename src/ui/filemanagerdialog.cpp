#include "ui/filemanagerdialog.h"

#include "files/remotefilesession.h"
#include "ui/iconprovider.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace {

// QFileSystemModel 的表头来自 Qt 自带翻译；应用只内嵌自己的翻译，
// 这里显式提供与远程栏一致的列名。
class LocalFileModel : public QFileSystemModel
{
public:
    explicit LocalFileModel(QObject *parent = nullptr)
        : QFileSystemModel(parent)
    {
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
            case 0: return QCoreApplication::translate("FileManagerDialog", "名称");
            case 1: return QCoreApplication::translate("FileManagerDialog", "大小");
            case 2: return QCoreApplication::translate("FileManagerDialog", "类型");
            case 3: return QCoreApplication::translate("FileManagerDialog", "修改时间");
            default: break;
            }
        }
        return QFileSystemModel::headerData(section, orientation, role);
    }

    // QFileSystemModel 的"类型"列来自 Qt 自带翻译（未加载时显示
    // File Folder / File）。这里统一成界面语言，避免中英混排。
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DisplayRole && index.column() == 2) {
            return isDir(index)
                       ? QCoreApplication::translate("FileManagerDialog", "文件夹")
                       : QCoreApplication::translate("FileManagerDialog", "文件");
        }
        return QFileSystemModel::data(index, role);
    }

    // QFileSystemModel 默认不允许拖动；显式打开后由 LocalFileView 接管拖拽。
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QFileSystemModel::flags(index) | Qt::ItemIsDragEnabled;
    }
};

// 远端条目的内部拖拽格式（本地 -> 远端使用 text/uri-list）。
constexpr char kRemoteItemsMime[] = "application/x-nterminal-remote-items";

QByteArray encodeRemoteItems(const QVector<RemoteTransferItem> &items)
{
    QJsonArray array;
    for (const RemoteTransferItem &item : items) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), item.name);
        object.insert(QStringLiteral("path"), item.path);
        object.insert(QStringLiteral("isDir"), item.isDir);
        object.insert(QStringLiteral("size"), double(item.size));
        object.insert(QStringLiteral("mode"), item.mode);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("items"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QVector<RemoteTransferItem> decodeRemoteItems(const QByteArray &data)
{
    QVector<RemoteTransferItem> items;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return items;
    }
    const QJsonArray array =
        document.object().value(QStringLiteral("items")).toArray();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        RemoteTransferItem item;
        item.name = object.value(QStringLiteral("name")).toString();
        item.path = object.value(QStringLiteral("path")).toString();
        item.isDir = object.value(QStringLiteral("isDir")).toBool();
        item.size = object.value(QStringLiteral("size")).toVariant().toLongLong();
        item.mode = object.value(QStringLiteral("mode")).toInt();
        if (!item.path.isEmpty()) {
            items.append(item);
        }
    }
    return items;
}

QStringList localPathsFromMime(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls()) {
        return paths;
    }
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty() && !paths.contains(path)) {
                paths.append(path);
            }
        }
    }
    return paths;
}

// 本地文件视图：把远端条目拖到这里即下载；同时允许把本地文件拖出到
// 资源管理器/远端视图。
class LocalFileView : public QTreeView
{
public:
    explicit LocalFileView(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
        setAcceptDrops(true);
        setDragEnabled(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::CopyAction);
    }

    void setRemoteItemsDroppedHandler(
        std::function<void(const QVector<RemoteTransferItem> &, const QString &)>
            handler)
    {
        m_dropHandler = std::move(handler);
    }

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        Q_UNUSED(supportedActions);
        auto *fileModel = qobject_cast<QFileSystemModel *>(model());
        if (!fileModel || !selectionModel()) {
            return;
        }
        QList<QUrl> urls;
        const QModelIndexList rows = selectionModel()->selectedRows(0);
        for (const QModelIndex &index : rows) {
            urls.append(QUrl::fromLocalFile(fileModel->filePath(index)));
        }
        if (urls.isEmpty()) {
            return;
        }
        QMimeData *mime = new QMimeData;
        mime->setUrls(urls);
        QDrag *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (m_dropHandler
            && event->mimeData()->hasFormat(kRemoteItemsMime)) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (m_dropHandler
            && event->mimeData()->hasFormat(kRemoteItemsMime)) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!m_dropHandler
            || !event->mimeData()->hasFormat(kRemoteItemsMime)) {
            event->ignore();
            return;
        }
        const QVector<RemoteTransferItem> items =
            decodeRemoteItems(event->mimeData()->data(kRemoteItemsMime));
        const QString target = targetDirectory(event->position().toPoint());
        if (items.isEmpty() || target.isEmpty()) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        m_dropHandler(items, target);
    }

private:
    QString targetDirectory(const QPoint &position) const
    {
        auto *fileModel = qobject_cast<QFileSystemModel *>(model());
        if (!fileModel) {
            return QString();
        }
        const QModelIndex index = indexAt(position);
        if (index.isValid()) {
            const QString path = fileModel->filePath(index);
            return fileModel->isDir(index) ? path
                                           : QFileInfo(path).absolutePath();
        }
        const QModelIndex root = rootIndex();
        return root.isValid() ? fileModel->filePath(root) : QString();
    }

    std::function<void(const QVector<RemoteTransferItem> &, const QString &)>
        m_dropHandler;
};

// 远端文件视图：把本地文件拖到这里即上传；拖动选中行可拖到本地栏。
class RemoteFileView : public QTableWidget
{
public:
    using ItemsProvider = std::function<QVector<RemoteTransferItem>()>;
    using TargetProvider = std::function<QString(int)>;
    using DropHandler = std::function<void(const QStringList &, const QString &)>;

    RemoteFileView(int rows, int columns, QWidget *parent = nullptr)
        : QTableWidget(rows, columns, parent)
    {
        setAcceptDrops(true);
        setDragEnabled(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::CopyAction);
    }

    void setItemsProvider(ItemsProvider provider)
    {
        m_itemsProvider = std::move(provider);
    }

    void setTargetProvider(TargetProvider provider)
    {
        m_targetProvider = std::move(provider);
    }

    void setLocalPathsDroppedHandler(DropHandler handler)
    {
        m_dropHandler = std::move(handler);
    }

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        Q_UNUSED(supportedActions);
        if (!m_itemsProvider) {
            return;
        }
        const QVector<RemoteTransferItem> items = m_itemsProvider();
        if (items.isEmpty()) {
            return;
        }
        QMimeData *mime = new QMimeData;
        mime->setData(kRemoteItemsMime, encodeRemoteItems(items));
        QDrag *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (m_dropHandler && !localPathsFromMime(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (m_dropHandler && !localPathsFromMime(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        const QStringList paths = localPathsFromMime(event->mimeData());
        if (!m_dropHandler || paths.isEmpty()) {
            event->ignore();
            return;
        }
        const QModelIndex index = indexAt(event->position().toPoint());
        const QString target =
            m_targetProvider ? m_targetProvider(index.isValid() ? index.row() : -1)
                             : QString();
        if (target.isEmpty()) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        m_dropHandler(paths, target);
    }

private:
    ItemsProvider m_itemsProvider;
    TargetProvider m_targetProvider;
    DropHandler m_dropHandler;
};

bool askConfirm(QWidget *parent, const QString &title, const QString &text,
                const QString &acceptText, const QString &rejectText)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton,
                    parent);
    QAbstractButton *accept = box.addButton(acceptText, QMessageBox::AcceptRole);
    QAbstractButton *reject = box.addButton(rejectText, QMessageBox::RejectRole);
    // 覆盖/删除/关闭都属于可能丢数据的操作，默认焦点放在取消上
    box.setDefaultButton(qobject_cast<QPushButton *>(reject));
    box.exec();
    return box.clickedButton() == accept;
}

QPushButton *makeToolButton(const QString &icon, const QString &text,
                            const QString &styleClass, QWidget *parent)
{
    QPushButton *button = new QPushButton(IconProvider::themedIcon(icon), text, parent);
    if (!styleClass.isEmpty()) {
        button->setProperty("class", styleClass);
    }
    button->setIconSize(QSize(16, 16));
    return button;
}

QPushButton *makeSmallIconButton(const QString &icon, const QString &tooltip,
                                 QWidget *parent)
{
    QPushButton *button = new QPushButton(IconProvider::themedIcon(icon), QString(), parent);
    button->setProperty("class", "ghost");
    button->setToolTip(tooltip);
    // 图标按钮没有可见文字；提供可访问名称，屏幕阅读器/UI 自动化也能识别
    button->setAccessibleName(tooltip);
    button->setFixedSize(28, 28);
    button->setIconSize(QSize(16, 16));
    return button;
}

} // namespace

FileManagerDialog::FileManagerDialog(const Common::ConnectionSettings &settings,
                                     const QString &sessionName, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_sessionName(sessionName)
    , m_session(new RemoteFileSession(settings, this))
    , m_localPath(QDir::homePath())
{
    m_title = protocolLabel() + QStringLiteral(" ") + tr("文件管理器");
    if (!m_sessionName.isEmpty()) {
        m_title += QStringLiteral(" - ") + m_sessionName;
    }
    setWindowTitle(m_title);
    // 作为主窗口标签页使用：Qt::Widget 去掉 QDialog 的独立窗口语义，
    // 避免弹出新窗口；关闭动作统一由 closeRequested 交给主窗口处理。
    setWindowFlags(Qt::Widget);
    setModal(false);
    setMinimumSize(640, 420);

    setupUi();
    connectSession();

    setStatus(tr("正在连接 %1:%2 ...").arg(m_settings.host).arg(m_settings.port));
    m_session->start();
}

FileManagerDialog::~FileManagerDialog()
{
    if (m_session) {
        m_session->stop();
    }
}

QString FileManagerDialog::tabTitle() const
{
    return m_title;
}

bool FileManagerDialog::isConnected() const
{
    return m_connected;
}

void FileManagerDialog::stopSession()
{
    if (m_session) {
        m_session->stop();
    }
}

void FileManagerDialog::requestClose()
{
    if (m_busy) {
        if (!askConfirm(this, tr("关闭文件管理器"),
                        tr("正在传输文件，确定取消并关闭吗？"),
                        tr("关闭"), tr("取消"))) {
            return;
        }
        m_closePending = true;
        m_session->cancel();
        setStatus(tr("正在取消…"));
        return;
    }
    m_session->stop();
    emit closeRequested();
}

void FileManagerDialog::reject()
{
    // 嵌入式 QDialog 的 Esc/关闭按键不应把页面隐藏后留下空白标签页
    requestClose();
}

QString FileManagerDialog::protocolLabel() const
{
    switch (m_settings.type) {
    case Common::ConnectionType::SFTP:
        return QStringLiteral("SFTP");
    case Common::ConnectionType::FTP:
        return QStringLiteral("FTP");
    case Common::ConnectionType::SCP:
        return QStringLiteral("SCP");
    default:
        return tr("文件");
    }
}

void FileManagerDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(10);

    // ---- 标题与连接状态 ----
    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(12);
    QLabel *title = new QLabel(protocolLabel() + QStringLiteral(" ")
                                   + tr("文件管理器"),
                               this);
    title->setProperty("class", "sectionTitle");
    header->addWidget(title);

    const QString user = m_settings.username.isEmpty()
                             ? QStringLiteral("--")
                             : m_settings.username;
    m_connectionLabel = new QLabel(
        QStringLiteral("%1@%2:%3").arg(user, m_settings.host)
            .arg(m_settings.port), this);
    m_connectionLabel->setProperty("class", "hint");
    header->addWidget(m_connectionLabel);
    header->addStretch();
    root->addLayout(header);

    // ---- 工具栏 ----
    QHBoxLayout *toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);

    m_uploadButton = makeToolButton(
        QStringLiteral("transfer"), tr("上传 →"), QStringLiteral("tonal"), this);
    m_downloadButton = makeToolButton(
        QStringLiteral("download"), tr("← 下载"), QStringLiteral("tonal"), this);
    m_mkdirButton = makeToolButton(
        QStringLiteral("folder"), tr("新建文件夹"), QString(), this);
    m_deleteButton = makeToolButton(
        QStringLiteral("clear"), tr("删除"), QStringLiteral("danger"), this);
    m_renameButton = makeToolButton(
        QStringLiteral("save"), tr("重命名"), QString(), this);
    m_cancelButton = makeToolButton(
        QStringLiteral("stop"), tr("取消传输"), QStringLiteral("danger"), this);
    m_cancelButton->setVisible(false);
    m_closeButton = makeToolButton(
        QStringLiteral("close"), tr("关闭"), QStringLiteral("ghost"), this);

    toolbar->addWidget(m_uploadButton);
    toolbar->addWidget(m_downloadButton);
    toolbar->addSpacing(8);
    toolbar->addWidget(m_mkdirButton);
    toolbar->addWidget(m_deleteButton);
    toolbar->addWidget(m_renameButton);
    toolbar->addStretch();
    toolbar->addWidget(m_cancelButton);
    toolbar->addWidget(m_closeButton);
    root->addLayout(toolbar);

    // ---- 双栏 ----
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // 本地
    QWidget *localPane = new QWidget(splitter);
    QVBoxLayout *localLayout = new QVBoxLayout(localPane);
    localLayout->setContentsMargins(0, 0, 0, 0);
    localLayout->setSpacing(6);
    QHBoxLayout *localBar = new QHBoxLayout;
    localBar->setSpacing(4);
    QLabel *localTitle = new QLabel(tr("本地"), localPane);
    localTitle->setProperty("class", "subheading");
    localBar->addWidget(localTitle);
    m_localBackButton = makeSmallIconButton(
        QStringLiteral("chevron-left"), tr("后退"), localPane);
    m_localForwardButton = makeSmallIconButton(
        QStringLiteral("chevron-right"), tr("前进"), localPane);
    localBar->addWidget(m_localBackButton);
    localBar->addWidget(m_localForwardButton);
    m_localUpButton = makeSmallIconButton(
        QStringLiteral("chevron-up"), tr("上级目录"), localPane);
    localBar->addWidget(m_localUpButton);
    m_localPathEdit = new QLineEdit(m_localPath, localPane);
    m_localPathEdit->setPlaceholderText(tr("本地路径"));
    m_localPathEdit->setMinimumWidth(60);
    localBar->addWidget(m_localPathEdit, 1);
    // 根目录与主目录紧邻，使用不同图标，避免与“上级目录”混淆
    m_localRootButton = makeSmallIconButton(
        QStringLiteral("hard-drive"), tr("根目录"), localPane);
    m_localHomeButton = makeSmallIconButton(
        QStringLiteral("home"), tr("主目录"), localPane);
    m_localRefreshButton = makeSmallIconButton(
        QStringLiteral("reconnect"), tr("刷新"), localPane);
    localBar->addWidget(m_localRootButton);
    localBar->addWidget(m_localHomeButton);
    localBar->addWidget(m_localRefreshButton);
    localLayout->addLayout(localBar);

    m_localModel = new LocalFileModel(this);
    m_localModel->setRootPath(m_localPath);
    m_localModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    auto *localView = new LocalFileView(localPane);
    localView->setRemoteItemsDroppedHandler(
        [this](const QVector<RemoteTransferItem> &items,
               const QString &target) { downloadItems(items, target); });
    m_localView = localView;
    m_localView->setModel(m_localModel);
    m_localView->setRootIndex(m_localModel->index(m_localPath));
    m_localView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_localView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_localView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_localView->setRootIsDecorated(false);
    m_localView->setItemsExpandable(false);
    m_localView->setIndentation(0);
    m_localView->setUniformRowHeights(true);
    m_localView->setAlternatingRowColors(true);
    m_localView->setSortingEnabled(true);
    m_localView->sortByColumn(0, Qt::AscendingOrder);
    // 固定列的初始宽度用 Interactive，而不是 ResizeToContents：
    // 后者会把内容宽度计入 minimumSizeHint，双栏在窄窗口下会把右侧挤出屏幕。
    m_localView->header()->setMinimumSectionSize(40);
    m_localView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    const int localColumnWidths[] = {80, 90, 140};
    for (int column = 1; column < 4; ++column) {
        m_localView->header()->setSectionResizeMode(
            column, QHeaderView::Interactive);
        m_localView->header()->resizeSection(column,
                                             localColumnWidths[column - 1]);
    }
    localLayout->addWidget(m_localView, 1);
    m_localHistory = {m_localPath};
    m_localHistoryIndex = 0;
    splitter->addWidget(localPane);

    // 远端
    QWidget *remotePane = new QWidget(splitter);
    QVBoxLayout *remoteLayout = new QVBoxLayout(remotePane);
    remoteLayout->setContentsMargins(0, 0, 0, 0);
    remoteLayout->setSpacing(6);
    QHBoxLayout *remoteBar = new QHBoxLayout;
    remoteBar->setSpacing(4);
    QLabel *remoteTitle = new QLabel(tr("远程 · %1").arg(protocolLabel()),
                                     remotePane);
    remoteTitle->setProperty("class", "subheading");
    remoteBar->addWidget(remoteTitle);
    m_remoteBackButton = makeSmallIconButton(
        QStringLiteral("chevron-left"), tr("后退"), remotePane);
    m_remoteForwardButton = makeSmallIconButton(
        QStringLiteral("chevron-right"), tr("前进"), remotePane);
    remoteBar->addWidget(m_remoteBackButton);
    remoteBar->addWidget(m_remoteForwardButton);
    m_remoteUpButton = makeSmallIconButton(
        QStringLiteral("chevron-up"), tr("上级目录"), remotePane);
    remoteBar->addWidget(m_remoteUpButton);
    m_remotePathEdit = new QLineEdit(remotePane);
    m_remotePathEdit->setPlaceholderText(tr("远端路径"));
    m_remotePathEdit->setMinimumWidth(60);
    remoteBar->addWidget(m_remotePathEdit, 1);
    m_remoteRootButton = makeSmallIconButton(
        QStringLiteral("hard-drive"), tr("根目录"), remotePane);
    m_remoteHomeButton = makeSmallIconButton(
        QStringLiteral("home"), tr("主目录"), remotePane);
    m_remoteRefreshButton = makeSmallIconButton(
        QStringLiteral("reconnect"), tr("刷新"), remotePane);
    remoteBar->addWidget(m_remoteRootButton);
    remoteBar->addWidget(m_remoteHomeButton);
    remoteBar->addWidget(m_remoteRefreshButton);
    remoteLayout->addLayout(remoteBar);

    auto *remoteView = new RemoteFileView(0, 5, remotePane);
    remoteView->setItemsProvider([this] { return selectedRemoteItems(); });
    remoteView->setTargetProvider(
        [this](int row) { return remoteTargetDirectory(row); });
    remoteView->setLocalPathsDroppedHandler(
        [this](const QStringList &paths, const QString &target) {
            uploadPaths(paths, target);
        });
    m_remoteTable = remoteView;
    m_remoteTable->setHorizontalHeaderLabels(
        {tr("名称"), tr("大小"), tr("修改时间"), tr("权限"), tr("所有者")});
    m_remoteTable->horizontalHeader()->setMinimumSectionSize(40);
    m_remoteTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    const int remoteColumnWidths[] = {70, 130, 80, 80};
    for (int column = 1; column < 5; ++column) {
        m_remoteTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::Interactive);
        m_remoteTable->horizontalHeader()->resizeSection(
            column, remoteColumnWidths[column - 1]);
    }
    m_remoteTable->verticalHeader()->setVisible(false);
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->setShowGrid(false);
    m_remoteTable->setAlternatingRowColors(true);
    m_remoteTable->setSortingEnabled(false);
    m_remoteTable->setIconSize(QSize(16, 16));
    remoteLayout->addWidget(m_remoteTable, 1);
    splitter->addWidget(remotePane);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({560, 560});
    root->addWidget(splitter, 1);

    // ---- 底部进度与状态 ----
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    // 大文件传输时在进度条内显示百分比；未知总大小时显示忙碌动画
    m_progress->setTextVisible(true);
    m_progress->setFormat(QStringLiteral("%p%"));
    m_progress->setProperty("class", "transfer");
    m_progress->setRange(0, 0);
    root->addWidget(m_progress);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setProperty("class", "hint");
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    // ---- 信号 ----
    connect(m_localPathEdit, &QLineEdit::returnPressed, this,
            [this] { navigateLocal(m_localPathEdit->text().trimmed()); });
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this,
            [this] { navigateRemote(m_remotePathEdit->text().trimmed()); });
    connect(m_localBackButton, &QPushButton::clicked, this,
            &FileManagerDialog::goLocalBack);
    connect(m_localForwardButton, &QPushButton::clicked, this,
            &FileManagerDialog::goLocalForward);
    connect(m_localRootButton, &QPushButton::clicked, this,
            &FileManagerDialog::goLocalRoot);
    connect(m_localHomeButton, &QPushButton::clicked, this,
            &FileManagerDialog::goLocalHome);
    connect(m_localUpButton, &QPushButton::clicked, this, [this] {
        navigateLocal(QFileInfo(m_localPath).absolutePath());
    });
    connect(m_localRefreshButton, &QPushButton::clicked, this,
            &FileManagerDialog::refreshLocal);
    connect(m_remoteBackButton, &QPushButton::clicked, this,
            &FileManagerDialog::goRemoteBack);
    connect(m_remoteForwardButton, &QPushButton::clicked, this,
            &FileManagerDialog::goRemoteForward);
    connect(m_remoteRootButton, &QPushButton::clicked, this,
            &FileManagerDialog::goRemoteRoot);
    connect(m_remoteHomeButton, &QPushButton::clicked, this,
            &FileManagerDialog::goRemoteHome);
    connect(m_remoteUpButton, &QPushButton::clicked, this, [this] {
        const QString parent = remoteParent(m_remotePath);
        if (!parent.isEmpty()) {
            navigateRemote(parent);
        }
    });
    connect(m_remoteRefreshButton, &QPushButton::clicked, this,
            &FileManagerDialog::refreshRemote);
    connect(m_uploadButton, &QPushButton::clicked, this,
            &FileManagerDialog::uploadSelected);
    connect(m_downloadButton, &QPushButton::clicked, this,
            &FileManagerDialog::downloadSelected);
    connect(m_mkdirButton, &QPushButton::clicked, this,
            &FileManagerDialog::makeRemoteDirectory);
    connect(m_deleteButton, &QPushButton::clicked, this,
            &FileManagerDialog::removeSelected);
    connect(m_renameButton, &QPushButton::clicked, this,
            &FileManagerDialog::renameSelected);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        m_session->cancel();
        setStatus(tr("正在取消传输…"));
    });
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_localView, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (m_localModel->isDir(index)) {
                    navigateLocal(m_localModel->filePath(index));
                }
            });
    connect(m_localView->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this] { updateButtonStates(); });
    connect(m_remoteTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                QTableWidgetItem *item = m_remoteTable->item(row, 0);
                if (!item) {
                    return;
                }
                const int index = item->data(Qt::UserRole).toInt();
                if (index >= 0 && index < m_entries.size() && m_entries[index].isDir) {
                    navigateRemote(m_entries[index].path);
                }
            });
    connect(m_remoteTable, &QTableWidget::itemSelectionChanged, this,
            [this] { updateButtonStates(); });

    m_remoteTable->setEnabled(false);
    m_remotePathEdit->setEnabled(false);
    m_remoteUpButton->setEnabled(false);
    m_remoteRefreshButton->setEnabled(false);
    updateButtonStates();
}

void FileManagerDialog::connectSession()
{
    connect(m_session, &RemoteFileSession::connected, this,
            &FileManagerDialog::onConnected);
    connect(m_session, &RemoteFileSession::connectionFailed, this,
            &FileManagerDialog::onConnectionFailed);
    connect(m_session, &RemoteFileSession::directoryListed, this,
            &FileManagerDialog::onDirectoryListed);
    connect(m_session, &RemoteFileSession::listFailed, this,
            &FileManagerDialog::onListFailed);
    connect(m_session, &RemoteFileSession::transferStarted, this,
            &FileManagerDialog::onTransferStarted);
    connect(m_session, &RemoteFileSession::transferProgress, this,
            &FileManagerDialog::onTransferProgress);
    connect(m_session, &RemoteFileSession::transferFinished, this,
            &FileManagerDialog::onTransferFinished);
    connect(m_session, &RemoteFileSession::operationFinished, this,
            &FileManagerDialog::onOperationFinished);
}

void FileManagerDialog::onConnected(const QString &home,
                                    bool supportsDirectories)
{
    m_connected = true;
    m_directorySupport = supportsDirectories;
    m_remoteHome = home;
    m_connectionLabel->setText(
        tr("已连接 · %1@%2:%3")
            .arg(m_settings.username.isEmpty() ? QStringLiteral("--")
                                               : m_settings.username,
                 m_settings.host)
            .arg(m_settings.port));
    setStatus(tr("已连接。"));
    navigateRemote(home);
    updateButtonStates();
    emit connectionStateChanged(true);
}

void FileManagerDialog::onConnectionFailed(const QString &error)
{
    m_connected = false;
    m_connectionLabel->setText(tr("连接失败"));
    // 错误直接显示在页面状态栏，不再弹出额外对话框；带上目标地址，
    // 便于发现保存会话里主机/端口填错的情况。
    setStatus(tr("连接失败（%1:%2）：%3")
                  .arg(m_settings.host)
                  .arg(m_settings.port)
                  .arg(error),
              true);
    m_remoteTable->setEnabled(false);
    m_remotePathEdit->setEnabled(false);
    updateButtonStates();
    emit connectionStateChanged(false);
}

void FileManagerDialog::onDirectoryListed(
    const QString &path, const QVector<RemoteFileEntry> &entries)
{
    // 操作结束后自动刷新可能晚于用户的新导航；只接受最近一次请求的结果
    if (!m_requestedRemotePath.isEmpty() && path != m_requestedRemotePath) {
        return;
    }

    // 列表成功后才更新前进/后退历史，失败的路径不会留在历史里
    if (m_remoteNavPending) {
        if (m_remoteHistoryTarget >= 0) {
            if (m_remoteHistoryTarget < m_remoteHistory.size()) {
                m_remoteHistoryIndex = m_remoteHistoryTarget;
            }
        } else if (m_remoteHistoryIndex < 0) {
            m_remoteHistory = {path};
            m_remoteHistoryIndex = 0;
        } else if (m_remoteHistory.value(m_remoteHistoryIndex) != path) {
            while (m_remoteHistory.size() > m_remoteHistoryIndex + 1) {
                m_remoteHistory.removeLast();
            }
            m_remoteHistory.append(path);
            m_remoteHistoryIndex = m_remoteHistory.size() - 1;
        }
        m_remoteNavPending = false;
        m_remoteHistoryTarget = -1;
    }

    m_remotePath = path;
    m_remotePathEdit->setText(path);
    populateRemote(entries);
    setStatus(tr("目录 %1：%2 项").arg(path).arg(entries.size()));
    updateButtonStates();
}

void FileManagerDialog::onListFailed(const QString &path, const QString &error)
{
    if (!m_requestedRemotePath.isEmpty() && path != m_requestedRemotePath) {
        return;
    }
    const bool wasNavigating = m_remoteNavPending;
    m_remoteNavPending = false;
    m_remoteHistoryTarget = -1;
    if (wasNavigating) {
        // 后退/前进或直接输入失败时，路径栏回到当前已显示的目录
        m_remotePathEdit->setText(m_remotePath);
    }
    setStatus(error, true);
    updateButtonStates();
}

void FileManagerDialog::populateRemote(
    const QVector<RemoteFileEntry> &entries)
{
    m_entries = entries;
    m_remoteTable->setUpdatesEnabled(false);
    m_remoteTable->clearContents();
    m_remoteTable->setRowCount(entries.size());
    const Qt::ItemFlags rowFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled
                                   | Qt::ItemIsDragEnabled;
    for (int row = 0; row < entries.size(); ++row) {
        const RemoteFileEntry &entry = entries.at(row);

        auto *nameItem = new QTableWidgetItem(
            IconProvider::themedIcon(entry.isDir ? QStringLiteral("folder")
                                                 : QStringLiteral("log")),
            entry.name);
        nameItem->setData(Qt::UserRole, row);
        nameItem->setToolTip(entry.path);
        nameItem->setFlags(rowFlags);
        m_remoteTable->setItem(row, 0, nameItem);

        auto *sizeItem = new QTableWidgetItem(
            entry.isDir ? QString() : formatBytes(entry.size));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeItem->setFlags(rowFlags);
        m_remoteTable->setItem(row, 1, sizeItem);

        auto *dateItem = new QTableWidgetItem(
            entry.modified.isValid()
                ? entry.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QString());
        dateItem->setFlags(rowFlags);
        m_remoteTable->setItem(row, 2, dateItem);

        auto *permissionItem = new QTableWidgetItem(entry.permissions);
        permissionItem->setFlags(rowFlags);
        m_remoteTable->setItem(row, 3, permissionItem);

        auto *ownerItem = new QTableWidgetItem(entry.owner);
        ownerItem->setFlags(rowFlags);
        m_remoteTable->setItem(row, 4, ownerItem);
    }
    m_remoteTable->setUpdatesEnabled(true);
}

void FileManagerDialog::navigateLocal(const QString &path, bool recordHistory)
{
    if (path.isEmpty()) {
        return;
    }
    QDir dir(path);
    if (!dir.exists()) {
        setStatus(tr("本地目录不存在：%1").arg(path), true);
        return;
    }
    m_localPath = dir.absolutePath();
    m_localPathEdit->setText(m_localPath);
    // QFileSystemModel 只监视 setRootPath 指定的目录；切换目录时必须重设，
    // 否则 index(path) 为空，视图会显示空白。
    m_localModel->setRootPath(m_localPath);
    m_localView->setRootIndex(m_localModel->index(m_localPath));

    if (recordHistory) {
        if (m_localHistoryIndex < 0) {
            m_localHistory = {m_localPath};
            m_localHistoryIndex = 0;
        } else if (m_localHistory.value(m_localHistoryIndex) != m_localPath) {
            while (m_localHistory.size() > m_localHistoryIndex + 1) {
                m_localHistory.removeLast();
            }
            m_localHistory.append(m_localPath);
            m_localHistoryIndex = m_localHistory.size() - 1;
        }
    }
    refreshLocal();
}

void FileManagerDialog::goLocalBack()
{
    if (m_localHistoryIndex <= 0) {
        return;
    }
    const QString target = m_localHistory.at(m_localHistoryIndex - 1);
    if (!QDir(target).exists()) {
        setStatus(tr("本地目录不存在：%1").arg(target), true);
        return;
    }
    --m_localHistoryIndex;
    navigateLocal(target, false);
}

void FileManagerDialog::goLocalForward()
{
    if (m_localHistoryIndex < 0
        || m_localHistoryIndex + 1 >= m_localHistory.size()) {
        return;
    }
    const QString target = m_localHistory.at(m_localHistoryIndex + 1);
    if (!QDir(target).exists()) {
        setStatus(tr("本地目录不存在：%1").arg(target), true);
        return;
    }
    ++m_localHistoryIndex;
    navigateLocal(target, false);
}

void FileManagerDialog::goLocalRoot()
{
    navigateLocal(localRootPath());
}

QString FileManagerDialog::localRootPath() const
{
    // 当前路径所在盘的根目录（Windows 为 C:/、D:/ 等；Unix 为 /）。
    // 不能用 QFileInfo::absolutePath() 反复向上：在 Windows 上它可能把
    // "C:/" 变成 "C:"，而 "C:" 表示该盘的当前目录，反而跳回用户目录。
    const QString absolute = QDir(m_localPath).absolutePath();
#ifdef Q_OS_WIN
    if (absolute.size() >= 2 && absolute.at(1) == QLatin1Char(':')) {
        return absolute.left(2) + QLatin1Char('/');
    }
    if (absolute.startsWith(QLatin1String("//"))) {
        const QStringList parts = absolute.split(QLatin1Char('/'),
                                                 Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            return QStringLiteral("//%1/%2").arg(parts.at(0), parts.at(1));
        }
    }
#endif
    return QStringLiteral("/");
}

void FileManagerDialog::goLocalHome()
{
    navigateLocal(QDir::homePath());
}

void FileManagerDialog::navigateRemote(const QString &path, bool recordHistory)
{
    if (!m_connected || m_busy || path.isEmpty()) {
        return;
    }
    QString target = path;
    if (!target.startsWith(QLatin1Char('/'))
        && !target.startsWith(QLatin1String("./"))) {
        target = joinRemote(m_remotePath.isEmpty() ? QStringLiteral("/")
                                                   : m_remotePath,
                            target);
    }
    m_requestedRemotePath = target;
    m_remoteNavPending = recordHistory;
    m_remoteHistoryTarget = -1;
    setStatus(tr("正在读取目录 %1 ...").arg(target));
    m_session->listDirectory(target);
}

void FileManagerDialog::goRemoteHistory(int index)
{
    if (!m_connected || m_busy || index < 0
        || index >= m_remoteHistory.size()) {
        return;
    }
    const QString target = m_remoteHistory.at(index);
    m_requestedRemotePath = target;
    m_remoteNavPending = true;
    m_remoteHistoryTarget = index;
    setStatus(tr("正在读取目录 %1 ...").arg(target));
    m_session->listDirectory(target);
}

void FileManagerDialog::goRemoteBack()
{
    goRemoteHistory(m_remoteHistoryIndex - 1);
}

void FileManagerDialog::goRemoteForward()
{
    goRemoteHistory(m_remoteHistoryIndex + 1);
}

void FileManagerDialog::goRemoteRoot()
{
    navigateRemote(QStringLiteral("/"));
}

void FileManagerDialog::goRemoteHome()
{
    if (!m_remoteHome.isEmpty()) {
        navigateRemote(m_remoteHome);
    }
}

void FileManagerDialog::refreshLocal()
{
    m_localModel->setRootPath(m_localPath);
    m_localView->setRootIndex(m_localModel->index(m_localPath));
    updateButtonStates();
}

void FileManagerDialog::refreshRemote()
{
    // 导航请求尚未返回时不要用旧目录的刷新覆盖它
    if (m_connected && !m_busy && !m_remoteNavPending) {
        m_requestedRemotePath = m_remotePath;
        m_session->listDirectory(m_remotePath);
    }
}

QString FileManagerDialog::remoteParent(const QString &path) const
{
    if (path.isEmpty() || path == QLatin1String("/")) {
        return QString();
    }
    QString normalized = path;
    while (normalized.size() > 1 && normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    const int slash = normalized.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0) {
        return QStringLiteral("/");
    }
    return normalized.left(slash);
}

QString FileManagerDialog::joinRemote(const QString &directory,
                                      const QString &name) const
{
    if (directory.isEmpty() || directory == QLatin1String("/")) {
        return QLatin1Char('/') + name;
    }
    if (directory.endsWith(QLatin1Char('/'))) {
        return directory + name;
    }
    return directory + QLatin1Char('/') + name;
}

QStringList FileManagerDialog::selectedLocalPaths() const
{
    QStringList paths;
    if (!m_localView->selectionModel()) {
        return paths;
    }
    const QModelIndexList rows =
        m_localView->selectionModel()->selectedRows(0);
    for (const QModelIndex &index : rows) {
        paths.append(m_localModel->filePath(index));
    }
    return paths;
}

QString FileManagerDialog::remoteTargetDirectory(int row) const
{
    if (row >= 0) {
        QTableWidgetItem *item = m_remoteTable->item(row, 0);
        if (item) {
            const int entryIndex = item->data(Qt::UserRole).toInt();
            if (entryIndex >= 0 && entryIndex < m_entries.size()
                && m_entries.at(entryIndex).isDir) {
                return m_entries.at(entryIndex).path;
            }
        }
    }
    return m_remotePath;
}

QVector<RemoteTransferItem> FileManagerDialog::selectedRemoteItems() const
{
    QVector<RemoteTransferItem> items;
    const QModelIndexList rows = m_remoteTable->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        QTableWidgetItem *item = m_remoteTable->item(index.row(), 0);
        if (!item) {
            continue;
        }
        const int entryIndex = item->data(Qt::UserRole).toInt();
        if (entryIndex < 0 || entryIndex >= m_entries.size()) {
            continue;
        }
        const RemoteFileEntry &entry = m_entries.at(entryIndex);
        RemoteTransferItem target;
        target.name = entry.name;
        target.path = entry.path;
        target.isDir = entry.isDir;
        target.size = entry.size;
        target.mode = entry.mode;
        items.append(target);
    }
    return items;
}

void FileManagerDialog::uploadSelected()
{
    uploadPaths(selectedLocalPaths(), m_remotePath);
}

void FileManagerDialog::downloadSelected()
{
    downloadItems(selectedRemoteItems(), m_localPath);
}

void FileManagerDialog::uploadPaths(const QStringList &paths,
                                    const QString &remoteDirectory)
{
    if (paths.isEmpty() || remoteDirectory.isEmpty() || !m_connected
        || m_busy) {
        return;
    }

    if (!m_directorySupport) {
        for (const QString &path : paths) {
            if (QFileInfo(path).isDir()) {
                setStatus(
                    tr("SCP 模式当前只支持传输文件，暂不支持目录。"), true);
                return;
            }
        }
    }

    // 只有目标就是当前列出目录时才能用内存中的条目判断覆盖；
    // 拖到子目录时由服务器行为决定，不再额外确认。
    QStringList conflicts;
    if (remoteDirectory == m_remotePath) {
        for (const QString &path : paths) {
            const QString name = QFileInfo(path).fileName();
            for (const RemoteFileEntry &entry : m_entries) {
                if (entry.name == name) {
                    conflicts.append(name);
                    break;
                }
            }
        }
    }
    if (!conflicts.isEmpty()
        && !askConfirm(
            this, tr("上传"),
            tr("远端已存在同名项，继续会覆盖：\n%1\n\n是否继续？")
                .arg(conflicts.join(QStringLiteral(", "))),
            tr("继续"), tr("取消"))) {
        return;
    }

    setBusy(true);
    setStatus(tr("正在上传 %1 项…").arg(paths.size()));
    m_session->upload(paths, remoteDirectory);
}

void FileManagerDialog::downloadItems(
    const QVector<RemoteTransferItem> &items, const QString &localDirectory)
{
    if (items.isEmpty() || localDirectory.isEmpty() || !m_connected
        || m_busy) {
        return;
    }

    if (!m_directorySupport) {
        for (const RemoteTransferItem &item : items) {
            if (item.isDir) {
                setStatus(
                    tr("SCP 模式当前只支持传输文件，暂不支持目录。"), true);
                return;
            }
        }
    }

    QStringList conflicts;
    for (const RemoteTransferItem &item : items) {
        if (QFileInfo(QDir(localDirectory).filePath(item.name)).exists()) {
            conflicts.append(item.name);
        }
    }
    if (!conflicts.isEmpty()
        && !askConfirm(
            this, tr("下载"),
            tr("本地已存在同名项，继续会覆盖：\n%1\n\n是否继续？")
                .arg(conflicts.join(QStringLiteral(", "))),
            tr("继续"), tr("取消"))) {
        return;
    }

    setBusy(true);
    setStatus(tr("正在下载 %1 项…").arg(items.size()));
    m_session->download(items, localDirectory);
}

void FileManagerDialog::makeRemoteDirectory()
{
    if (!m_connected || m_busy) {
        return;
    }
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("新建文件夹"));
    dialog.setLabelText(tr("文件夹名称："));
    dialog.setOkButtonText(tr("确定"));
    dialog.setCancelButtonText(tr("取消"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dialog.textValue().trimmed();
    if (name.isEmpty()) {
        return;
    }
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("新建文件夹"),
                             tr("文件夹名称不能包含 /"));
        return;
    }
    setBusy(true);
    setStatus(tr("正在创建文件夹…"));
    m_session->makeDirectory(joinRemote(m_remotePath, name));
}

void FileManagerDialog::removeSelected()
{
    const QVector<RemoteTransferItem> items = selectedRemoteItems();
    if (items.isEmpty() || m_busy) {
        return;
    }
    QStringList names;
    for (const RemoteTransferItem &item : items) {
        names.append(item.name);
    }
    if (!askConfirm(this, tr("删除"),
                    tr("确定删除以下远端项吗？\n%1")
                        .arg(names.join(QStringLiteral(", "))),
                    tr("删除"), tr("取消"))) {
        return;
    }
    setBusy(true);
    setStatus(tr("正在删除…"));
    m_session->remove(items);
}

void FileManagerDialog::renameSelected()
{
    const QVector<RemoteTransferItem> items = selectedRemoteItems();
    if (items.size() != 1 || m_busy) {
        return;
    }
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("重命名"));
    dialog.setLabelText(tr("新名称："));
    dialog.setOkButtonText(tr("确定"));
    dialog.setCancelButtonText(tr("取消"));
    dialog.setTextValue(items.first().name);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dialog.textValue().trimmed();
    if (name.isEmpty() || name == items.first().name) {
        return;
    }
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("重命名"), tr("名称不能包含 /"));
        return;
    }
    setBusy(true);
    setStatus(tr("正在重命名…"));
    m_session->rename(items.first().path,
                      joinRemote(m_remotePath, name));
}

void FileManagerDialog::updateButtonStates()
{
    const bool canUse = m_connected && !m_busy;
    const bool hasLocal = !selectedLocalPaths().isEmpty();
    const bool hasRemote = !selectedRemoteItems().isEmpty();
    m_uploadButton->setEnabled(canUse && hasLocal);
    m_downloadButton->setEnabled(canUse && hasRemote);
    m_mkdirButton->setEnabled(canUse);
    m_deleteButton->setEnabled(canUse && hasRemote);
    m_renameButton->setEnabled(canUse && selectedRemoteItems().size() == 1);
    m_remoteRefreshButton->setEnabled(canUse);
    m_remoteUpButton->setEnabled(canUse
                                 && !m_remotePath.isEmpty()
                                 && m_remotePath != QLatin1String("/"));
    m_remotePathEdit->setEnabled(canUse);
    m_remoteTable->setEnabled(canUse);
    m_cancelButton->setVisible(m_busy);
    updateNavigationButtons();
}

void FileManagerDialog::updateNavigationButtons()
{
    m_localBackButton->setEnabled(m_localHistoryIndex > 0);
    m_localForwardButton->setEnabled(m_localHistoryIndex >= 0
                                     && m_localHistoryIndex + 1
                                            < m_localHistory.size());
    m_localRootButton->setEnabled(true);
    m_localHomeButton->setEnabled(true);

    const bool canUse = m_connected && !m_busy;
    m_remoteBackButton->setEnabled(canUse && m_remoteHistoryIndex > 0);
    m_remoteForwardButton->setEnabled(canUse && m_remoteHistoryIndex >= 0
                                      && m_remoteHistoryIndex + 1
                                             < m_remoteHistory.size());
    m_remoteRootButton->setEnabled(canUse);
    m_remoteHomeButton->setEnabled(canUse && !m_remoteHome.isEmpty());
}

void FileManagerDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    if (busy) {
        m_progress->setRange(0, 0);
        m_progress->setValue(0);
    }
    updateButtonStates();
}

void FileManagerDialog::setStatus(const QString &text, bool error)
{
    // 传输进度约每 100 ms 刷新一次，样式没有变化时不要反复 unpolish/polish
    const QString state = error ? QStringLiteral("error") : QString();
    if (m_statusLabel->property("state").toString() != state) {
        m_statusLabel->setProperty("state", state);
        m_statusLabel->style()->unpolish(m_statusLabel);
        m_statusLabel->style()->polish(m_statusLabel);
    }
    m_statusLabel->setText(text);
}

void FileManagerDialog::onTransferStarted(const QString &name, qint64 total)
{
    m_currentTransferName = name;
    m_transferTimer.start();
    setBusy(true);
    m_progress->setTextVisible(total > 0);
    if (total > 0) {
        // 固定 0..1000 的百分比刻度，避免超过 2 GB 的文件让 int 溢出
        m_progress->setRange(0, 1000);
        m_progress->setValue(0);
    } else {
        m_progress->setRange(0, 0);
    }
    setStatus(tr("正在传输 %1 …").arg(name));
}

void FileManagerDialog::onTransferProgress(qint64 done, qint64 total)
{
    const qint64 elapsedMs = m_transferTimer.elapsed();
    const qint64 speed =
        elapsedMs > 200 ? qint64(double(done) * 1000.0 / double(elapsedMs)) : 0;

    m_progress->setTextVisible(total > 0);
    if (total > 0) {
        const double ratio = double(done) / double(total);
        m_progress->setRange(0, 1000);
        m_progress->setValue(
            qBound(0, int(ratio * 1000.0), 1000));

        const int percent = qBound(0, int(ratio * 100.0), 100);
        QString text = tr("%1  %2 / %3")
                           .arg(m_currentTransferName, formatBytes(done),
                                formatBytes(total))
                       + QStringLiteral("  (%1%)").arg(percent);
        if (speed > 0) {
            text += QStringLiteral("  %1/s").arg(formatBytes(speed));
        }
        setStatus(text);
    } else {
        m_progress->setRange(0, 0);
        QString text = tr("%1  %2")
                           .arg(m_currentTransferName, formatBytes(done));
        if (speed > 0) {
            text += QStringLiteral("  %1/s").arg(formatBytes(speed));
        }
        setStatus(text);
    }
}

void FileManagerDialog::onTransferFinished(const QString &name, bool success,
                                           const QString &message)
{
    setStatus(success ? tr("%1：%2").arg(name, message)
                      : tr("%1 失败：%2").arg(name, message),
              !success);
}

void FileManagerDialog::onOperationFinished(const QString &operation,
                                            bool success,
                                            const QString &message)
{
    setBusy(false);
    setStatus(message, !success);
    if (success) {
        if (operation == QLatin1String("download")) {
            refreshLocal();
        } else {
            refreshRemote();
        }
    }
    if (m_closePending && !m_busy) {
        m_closePending = false;
        m_session->stop();
        emit closeRequested();
    }
}

void FileManagerDialog::closeEvent(QCloseEvent *event)
{
    if (m_busy) {
        requestClose();
        event->ignore();
        return;
    }
    requestClose();
    event->accept();
}

QString FileManagerDialog::formatBytes(qint64 bytes)
{
    constexpr qint64 kb = 1024LL;
    constexpr qint64 mb = kb * 1024;
    constexpr qint64 gb = mb * 1024;
    constexpr qint64 tb = gb * 1024;
    if (bytes >= tb) {
        return QStringLiteral("%1 TB").arg(double(bytes) / double(tb), 0, 'f', 2);
    }
    if (bytes >= gb) {
        return QStringLiteral("%1 GB").arg(double(bytes) / double(gb), 0, 'f', 2);
    }
    if (bytes >= mb) {
        return QStringLiteral("%1 MB").arg(double(bytes) / double(mb), 0, 'f', 2);
    }
    if (bytes >= kb) {
        return QStringLiteral("%1 KB").arg(double(bytes) / double(kb), 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}
