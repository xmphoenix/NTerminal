#include "common/languagemanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace {
const char *kSettingsGroup = "UI";
const char *kLanguageKey = "Language";
const char *kChineseCode = "zh_CN";
const char *kEnglishCode = "en_US";

// 首次启动（用户尚未在“设置 -> 语言”中保存过选择）时的缺省语言：
// 系统首选语言是中文则用中文，其余（英文、日文等）一律用英文。
LanguageManager::Language systemDefaultLanguage()
{
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    if (!uiLanguages.isEmpty()
            && uiLanguages.first().startsWith(QLatin1String("zh"), Qt::CaseInsensitive)) {
        return LanguageManager::Language::Chinese;
    }
    return LanguageManager::Language::English;
}
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
    const QString code = settings.value(QLatin1String(kLanguageKey)).toString();
    settings.endGroup();

    // 用户显式保存过语言则优先使用；否则按系统首选语言决定缺省值。
    apply(code.isEmpty() ? systemDefaultLanguage() : languageFromCode(code));
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
    // 使界面语言一致。优先加载 qtbase_zh_CN.qm：qt_zh_CN.qm 只是聚合目录，
    // 还会加载 qtmultimedia 等未必随程序分发的模块翻译，缺一项就整体失败。
    const QString translationsDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    auto *qtTranslator = new QTranslator(this);
    bool loaded = qtTranslator->load(translationsDir + QStringLiteral("/qtbase_zh_CN.qm"));
    if (!loaded) {
        loaded = qtTranslator->load(translationsDir + QStringLiteral("/qt_zh_CN.qm"));
    }
    if (loaded) {
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
