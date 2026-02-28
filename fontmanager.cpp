#include "fontmanager.h"
#include <QSettings>
#include <QApplication>
#include <QFontDatabase>
#include <QDir>
#include <QFile>
#include <QDebug>

FontManager *FontManager::m_instance = nullptr;

FontManager::FontManager(QObject *parent)
    : QObject(parent)
    , m_currentFontSize(10)
    , m_initialized(false)
{
}

FontManager::~FontManager()
{
}

FontManager* FontManager::instance()
{
    if (!m_instance) {
        m_instance = new FontManager();
    }
    return m_instance;
}

void FontManager::initialize()
{
    if (m_initialized) {
        return;
    }

    initFonts();
    loadSettings();
    m_initialized = true;
}

void FontManager::initFonts()
{
    // 注册嵌入式字体（从 Qt 资源系统加载）
    // 字体资源路径使用 Qt 资源系统前缀

    // Cascadia Code
    registerEmbeddedFont("cascadia-code", "Cascadia Code", "Cascadia Code",
                         ":/fonts/CascadiaCode.ttf");

    // Fira Code
    registerEmbeddedFont("fira-code", "Fira Code", "Fira Code",
                         ":/fonts/FiraCode-Regular.ttf");

    // JetBrains Mono
    registerEmbeddedFont("jetbrains-mono", "JetBrains Mono", "JetBrains Mono",
                         ":/fonts/JetBrainsMono-Regular.ttf");

    // Source Code Pro
    registerEmbeddedFont("source-code-pro", "Source Code Pro", "Source Code Pro",
                         ":/fonts/SourceCodePro-Regular.ttf");

    // 系统默认等宽字体
    FontInfo info;
    info.id = "system-mono";
    info.name = "System Mono";
    info.displayName = QStringLiteral("System Mono");
    info.isEmbedded = false;
    m_fonts[info.id] = info;

    // Consolas (Windows 常见)
    info.id = "consolas";
    info.name = "Consolas";
    info.displayName = QStringLiteral("Consolas");
    info.isEmbedded = false;
    m_fonts[info.id] = info;

    // Microsoft YaHei Mono
    info.id = "msyh-mono";
    info.name = "Microsoft YaHei Mono";
    info.displayName = QStringLiteral("Microsoft YaHei Mono");
    info.isEmbedded = false;
    m_fonts[info.id] = info;

    // Courier New
    info.id = "courier-new";
    info.name = "Courier New";
    info.displayName = QStringLiteral("Courier New");
    info.isEmbedded = false;
    m_fonts[info.id] = info;

    // Lucida Console
    info.id = "lucida-console";
    info.name = "Lucida Console";
    info.displayName = QStringLiteral("Lucida Console");
    info.isEmbedded = false;
    m_fonts[info.id] = info;

    // 设置为默认字体
    m_currentFontId = "system-mono";
}

void FontManager::registerEmbeddedFont(const QString &id, const QString &name,
                                        const QString &displayName, const QString &path)
{
    FontInfo info;
    info.id = id;
    info.name = name;
    info.displayName = displayName;
    info.isEmbedded = true;
    info.embeddedPath = path;

    // 检查字体文件是否存在，如果存在则加载
    if (QFile::exists(path)) {
        if (loadFontFile(path)) {
            qDebug() << "[FontManager] Loaded embedded font:" << id << "from" << path;
        } else {
            qDebug() << "[FontManager] Failed to load embedded font:" << id;
            info.isEmbedded = false;
        }
    } else {
        qDebug() << "[FontManager] Embedded font file not found:" << path;
        info.isEmbedded = false;
    }

    m_fonts[id] = info;
}

bool FontManager::loadFontFile(const QString &path)
{
    int fontId = QFontDatabase::addApplicationFont(path);
    if (fontId == -1) {
        return false;
    }

    // 成功加载后，字体将在全局范围内可用
    return true;
}

bool FontManager::loadEmbeddedFont(const QString &fontId)
{
    FontInfo info = m_fonts.value(fontId);
    if (info.isEmbedded && !info.embeddedPath.isEmpty()) {
        return loadFontFile(info.embeddedPath);
    }
    return false;
}

QString FontManager::getFontPath(const QString &fontId) const
{
    FontInfo info = m_fonts.value(fontId);
    return info.embeddedPath;
}

QVector<FontInfo> FontManager::availableFonts() const
{
    return m_fonts.values().toVector();
}

FontInfo FontManager::fontInfo(const QString &id) const
{
    return m_fonts.value(id);
}

QString FontManager::currentFontId() const
{
    return m_currentFontId;
}

int FontManager::currentFontSize() const
{
    return m_currentFontSize;
}

void FontManager::setCurrentFont(const QString &id)
{
    if (m_fonts.contains(id) && m_currentFontId != id) {
        m_currentFontId = id;

        // 如果是嵌入式字体，确保已加载
        FontInfo info = m_fonts[id];
        if (info.isEmbedded) {
            loadEmbeddedFont(id);
        }

        emit fontChanged(currentFont());
        saveSettings();
    }
}

void FontManager::setCurrentFontSize(int size)
{
    if (m_currentFontSize != size && size > 0 && size < 100) {
        m_currentFontSize = size;
        emit fontChanged(currentFont());
        saveSettings();
    }
}

QFont FontManager::currentFont() const
{
    return font(m_currentFontId, m_currentFontSize);
}

QFont FontManager::font(const QString &id, int size) const
{
    FontInfo info = m_fonts.value(id);
    if (info.name.isEmpty()) {
        // 返回默认等宽字体
        QFont font("Consolas", size);
        font.setStyleHint(QFont::Monospace);
        return font;
    }

    QFont font(info.name, size);
    font.setStyleHint(QFont::Monospace);
    return font;
}

void FontManager::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Font");
    settings.setValue("FontId", m_currentFontId);
    settings.setValue("FontSize", m_currentFontSize);
    settings.endGroup();
}

void FontManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Font");

    QString fontId = settings.value("FontId", "system-mono").toString();
    int fontSize = settings.value("FontSize", 10).toInt();

    if (m_fonts.contains(fontId)) {
        m_currentFontId = fontId;

        // 加载嵌入式字体
        FontInfo info = m_fonts[fontId];
        if (info.isEmbedded) {
            loadEmbeddedFont(fontId);
        }
    }

    m_currentFontSize = fontSize;

    settings.endGroup();
}
