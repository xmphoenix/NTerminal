#include "common/languagemanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QSettings>

namespace {
const char *kSettingsGroup = "UI";
const char *kLanguageKey = "Language";
const char *kChineseCode = "zh_CN";
const char *kEnglishCode = "en_US";
} // namespace

LanguageManager *LanguageManager::instance()
{
    static LanguageManager manager;
    return &manager;
}

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
}

void LanguageManager::initialize()
{
    // 显式指定组织/应用名，与 LogConfigDialog 的配置位置保持一致
    QSettings settings(QStringLiteral("NTerminal"), QStringLiteral("NTerminal"));
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString code = settings.value(QLatin1String(kLanguageKey),
                                        QLatin1String(kChineseCode)).toString();
    settings.endGroup();

    apply(languageFromCode(code));
}

void LanguageManager::setLanguage(Language language)
{
    if (language == m_language) {
        return;
    }

    apply(language);

    QSettings settings(QStringLiteral("NTerminal"), QStringLiteral("NTerminal"));
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kLanguageKey), languageCode(language));
    settings.endGroup();

    emit languageChanged(m_language);
}

void LanguageManager::apply(Language language)
{
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        QCoreApplication::removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    m_language = language;

    if (language == Language::English) {
        // 英文：加载内嵌的应用翻译（中文是源语言，不需要翻译文件）
        auto *translator = new QTranslator(this);
        if (translator->load(QStringLiteral(":/i18n/NTerminal_en_US.qm"))) {
            QCoreApplication::installTranslator(translator);
            m_appTranslator = translator;
        } else {
            qWarning() << "LanguageManager: failed to load :/i18n/NTerminal_en_US.qm";
            delete translator;
        }
        return;
    }

    // 中文：额外加载 Qt 自带的控件翻译（标准按钮、行编辑右键菜单等），
    // 使界面语言一致。翻译文件随 Qt 安装/部署提供，缺失时静默回退英文。
    const QString qtTranslations =
        QLibraryInfo::path(QLibraryInfo::TranslationsPath) + QStringLiteral("/qt_zh_CN.qm");
    auto *qtTranslator = new QTranslator(this);
    if (qtTranslator->load(qtTranslations)) {
        QCoreApplication::installTranslator(qtTranslator);
        m_qtTranslator = qtTranslator;
    } else {
        delete qtTranslator;
    }
}

LanguageManager::Language LanguageManager::languageFromCode(const QString &code)
{
    return code.compare(QLatin1String(kEnglishCode), Qt::CaseInsensitive) == 0
        ? Language::English
        : Language::Chinese;
}

QString LanguageManager::languageCode(Language language)
{
    return language == Language::English ? QLatin1String(kEnglishCode)
                                         : QLatin1String(kChineseCode);
}

QString LanguageManager::languageDisplayName(Language language)
{
    // 用各自语言书写语言名，切换时用户能一眼找到
    return language == Language::English ? QStringLiteral("English")
                                         : QStringLiteral("中文");
}
