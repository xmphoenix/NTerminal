#ifndef FONTMANAGER_H
#define FONTMANAGER_H

#include <QObject>
#include <QFont>
#include <QVector>
#include <QMap>
#include <QString>

struct FontInfo {
    QString id;
    QString name;
    QString displayName;
    bool isEmbedded;
    QString embeddedPath;
};

class FontManager : public QObject
{
    Q_OBJECT

public:
    static FontManager* instance();

    void initialize();

    QVector<FontInfo> availableFonts() const;
    FontInfo fontInfo(const QString &id) const;

    QString currentFontId() const;
    int currentFontSize() const;

    void setCurrentFont(const QString &id);
    void setCurrentFontSize(int size);

    QFont currentFont() const;
    QFont font(const QString &id, int size) const;

    void saveSettings();
    void loadSettings();

    bool loadEmbeddedFont(const QString &fontId);
    QString getFontPath(const QString &fontId) const;

signals:
    void fontChanged(const QFont &font);

private:
    FontManager(QObject *parent = nullptr);
    ~FontManager();

    void initFonts();
    void registerEmbeddedFont(const QString &id, const QString &name, const QString &displayName, const QString &path);
    bool loadFontFile(const QString &path);

    static FontManager *m_instance;

    QMap<QString, FontInfo> m_fonts;
    QString m_currentFontId;
    int m_currentFontSize;

    bool m_initialized;
};

#endif // FONTMANAGER_H
