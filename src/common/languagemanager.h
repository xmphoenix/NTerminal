#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QTranslator>

// 界面语言管理：源码字符串为中文（缺省语言，无需翻译文件），
// 选择英文时加载内嵌的 :/i18n/NTerminal_en_US.qm。
// 切换语言会安装/移除 QTranslator，Qt 自动向所有窗口派发
// QEvent::LanguageChange，各窗口在 changeEvent() 中重设文案。
class LanguageManager : public QObject
{
    Q_OBJECT

public:
    enum class Language {
        Chinese = 0,
        English = 1,
    };

    static LanguageManager *instance();

    // 读取保存的语言并应用。未保存过时跟随系统首选语言
    // （中文系统→中文，其他系统→英文）。需在创建主窗口之前调用。
    void initialize();

    Language current() const { return m_language; }

    // 切换语言：立即生效、写入配置并发出 languageChanged()
    void setLanguage(Language language);

    static Language languageFromCode(const QString &code);
    static QString languageCode(Language language);
    // 语言自身的显示名（不随界面语言变化）
    static QString languageDisplayName(Language language);

signals:
    void languageChanged(Language language);

private:
    explicit LanguageManager(QObject *parent = nullptr);
    void apply(Language language);

    QTranslator *m_appTranslator = nullptr;   // 英文：内嵌的应用翻译
    QTranslator *m_qtTranslator = nullptr;    // 中文：Qt 自带控件文案（OK/取消、文本框右键菜单）
    Language m_language = Language::Chinese;
};

#endif // LANGUAGEMANAGER_H
