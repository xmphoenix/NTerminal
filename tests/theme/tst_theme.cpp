// ============================================================================
// tst_theme —— 主题系统测试
//
// 覆盖：
//   1. TerminalTheme::ansi256Color（0-15 / 216 色立方 / 灰阶 / 越界）
//   2. 主题目录完整性（id 唯一、颜色 token 有效、明暗标记一致）
//   3. 外壳主题与终端配色的自动配对
//   4. 主题切换信号、调色板与 QSS 占位符替换、设置持久化
//   5. 配置中的失效主题 id 回落，不得出现空主题
// ============================================================================
#include <QtTest>
#include <QApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>
#include "theme/thememanager.h"
#include "terminal/terminaltheme.h"

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void ansi256Indexed0to15();
    void ansi256Cube_data();
    void ansi256Cube();
    void ansi256OutOfRange();

    void terminalThemeCatalogIntegrity();
    void appThemeCatalogIntegrity();
    void pairedTerminalThemesMatchShell();

    void stylesheetTokensResolved();
    void switchAppThemeAndAutoPairing();
    void explicitTerminalSelectionStopsAutoPairing();
    void loadSettingsFallbackForStaleIds();
    void staleTerminalIdFallsBackToPairedTheme();
    void iconColorAndWindowBorderSmoke();

private:
    void resetSettings();
};

namespace {

void verifyColor(const QColor &color, const char *name, const char *context)
{
    QVERIFY2(color.isValid(), qPrintable(QStringLiteral("%1.%2 is invalid")
                                             .arg(QLatin1String(context),
                                                  QLatin1String(name))));
}

} // namespace

void TestTheme::initTestCase()
{
    // 在 ThemeManager 创建 QSettings 之前隔离测试配置，
    // 绝不能读写用户真实主题设置。
    QCoreApplication::setOrganizationName(QStringLiteral("NTerminalTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_theme"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings().clear();

    ThemeManager::instance()->initialize();
}

void TestTheme::cleanupTestCase()
{
    QSettings().clear();
}

void TestTheme::resetSettings()
{
    QSettings().clear();
    ThemeManager::instance()->loadSettings();
}

void TestTheme::ansi256Indexed0to15()
{
    // 0-15 必须逐项来自主题的 ansiColors，不能落入立方色计算
    const QVector<TerminalTheme> themes = ThemeManager::instance()->terminalThemes();
    QVERIFY(!themes.isEmpty());
    for (const TerminalTheme &theme : themes) {
        for (int i = 0; i < 16; ++i) {
            QCOMPARE(theme.ansi256Color(i), theme.ansiColors[i]);
        }
    }
}

void TestTheme::ansi256Cube_data()
{
    QTest::addColumn<int>("index");
    QTest::addColumn<int>("red");
    QTest::addColumn<int>("green");
    QTest::addColumn<int>("blue");

    // xterm 216 色立方：每通道 0/95/135/175/215/255
    QTest::newRow("16 black") << 16 << 0 << 0 << 0;
    QTest::newRow("17 blue level 1") << 17 << 0 << 0 << 95;
    QTest::newRow("21 blue max") << 21 << 0 << 0 << 255;
    QTest::newRow("22 green level 1") << 22 << 0 << 95 << 0;
    QTest::newRow("51 cyan max") << 51 << 0 << 255 << 255;
    QTest::newRow("52 red level 1") << 52 << 95 << 0 << 0;
    QTest::newRow("196 pure red") << 196 << 255 << 0 << 0;
    QTest::newRow("231 white") << 231 << 255 << 255 << 255;

    // 232-255 灰阶：8 + 10*n
    QTest::newRow("232 first gray") << 232 << 8 << 8 << 8;
    QTest::newRow("244 middle gray") << 244 << 128 << 128 << 128;
    QTest::newRow("255 last gray") << 255 << 238 << 238 << 238;
}

void TestTheme::ansi256Cube()
{
    QFETCH(int, index);
    QFETCH(int, red);
    QFETCH(int, green);
    QFETCH(int, blue);

    const TerminalTheme theme =
        ThemeManager::instance()->terminalTheme(QStringLiteral("default"));
    const QColor color = theme.ansi256Color(index);
    QVERIFY(color.isValid());
    QCOMPARE(color.red(), red);
    QCOMPARE(color.green(), green);
    QCOMPARE(color.blue(), blue);
}

void TestTheme::ansi256OutOfRange()
{
    const TerminalTheme theme =
        ThemeManager::instance()->terminalTheme(QStringLiteral("default"));
    // 越界索引必须返回无效颜色而不是读取数组之外的内存
    QVERIFY(!theme.ansi256Color(-1).isValid());
    QVERIFY(!theme.ansi256Color(256).isValid());
    QVERIFY(!theme.ansi256Color(100000).isValid());
}

void TestTheme::terminalThemeCatalogIntegrity()
{
    ThemeManager *tm = ThemeManager::instance();
    const QVector<TerminalTheme> themes = tm->terminalThemes();
    QVERIFY(themes.size() >= 16);

    QSet<QString> ids;
    for (const TerminalTheme &theme : themes) {
        QVERIFY(!theme.id.isEmpty());
        QVERIFY(!theme.name.isEmpty());
        QVERIFY2(!ids.contains(theme.id), qPrintable(theme.id));
        ids.insert(theme.id);

        QVERIFY(theme.defaultForeground.isValid());
        QVERIFY(theme.defaultBackground.isValid());
        QVERIFY(theme.cursorColor.isValid());
        QVERIFY(theme.selectionBackground.isValid());
        QVERIFY(theme.selectionForeground.isValid());
        for (int i = 0; i < 16; ++i) {
            QVERIFY2(theme.ansiColors[i].isValid(),
                     qPrintable(QStringLiteral("%1 ansiColors[%2]")
                                    .arg(theme.id).arg(i)));
        }
    }

    // 按 id 查询与缺失 id 的行为
    QCOMPARE(tm->terminalTheme(QStringLiteral("default")).id, QString("default"));
    QVERIFY(tm->terminalTheme(QStringLiteral("no-such-theme")).id.isEmpty());
    QVERIFY(!tm->currentTerminalThemeId().isEmpty());
    QVERIFY(!tm->currentTerminalTheme().id.isEmpty());
}

void TestTheme::appThemeCatalogIntegrity()
{
    ThemeManager *tm = ThemeManager::instance();
    const QVector<AppTheme> themes = tm->appThemes();
    QVERIFY(themes.size() >= 7);

    QSet<QString> ids;
    for (const AppTheme &theme : themes) {
        const QByteArray id = theme.id.toUtf8();
        QVERIFY(!theme.id.isEmpty());
        QVERIFY(!theme.name.isEmpty());
        QVERIFY2(!ids.contains(theme.id), id.constData());
        ids.insert(theme.id);

        verifyColor(theme.windowBackground, "windowBackground", id.constData());
        verifyColor(theme.windowText, "windowText", id.constData());
        verifyColor(theme.buttonBackground, "buttonBackground", id.constData());
        verifyColor(theme.buttonText, "buttonText", id.constData());
        verifyColor(theme.highlight, "highlight", id.constData());
        verifyColor(theme.highlightText, "highlightText", id.constData());
        verifyColor(theme.link, "link", id.constData());
        verifyColor(theme.menuBackground, "menuBackground", id.constData());
        verifyColor(theme.menuText, "menuText", id.constData());
        verifyColor(theme.toolTipBase, "toolTipBase", id.constData());
        verifyColor(theme.toolTipText, "toolTipText", id.constData());

        verifyColor(theme.panel, "panel", id.constData());
        verifyColor(theme.panelHover, "panelHover", id.constData());
        verifyColor(theme.panelRaised, "panelRaised", id.constData());
        verifyColor(theme.surfaceAlt, "surfaceAlt", id.constData());
        verifyColor(theme.border, "border", id.constData());
        verifyColor(theme.borderStrong, "borderStrong", id.constData());
        verifyColor(theme.hairline, "hairline", id.constData());
        verifyColor(theme.scrollbar, "scrollbar", id.constData());
        verifyColor(theme.textDim, "textDim", id.constData());
        verifyColor(theme.textFaint, "textFaint", id.constData());
        verifyColor(theme.accent, "accent", id.constData());
        verifyColor(theme.accentHover, "accentHover", id.constData());
        verifyColor(theme.accentText, "accentText", id.constData());
        verifyColor(theme.success, "success", id.constData());
        verifyColor(theme.warning, "warning", id.constData());
        verifyColor(theme.danger, "danger", id.constData());

        QCOMPARE(theme.radiusSm, 6);
        QCOMPARE(theme.radius, 8);
        QCOMPARE(theme.radiusLg, 12);
        QCOMPARE(theme.dark, theme.windowBackground.lightness() < 128);
    }

    QCOMPARE(tm->appTheme(QStringLiteral("fresh-light")).id, QString("fresh-light"));
    QVERIFY(tm->appTheme(QStringLiteral("no-such-theme")).id.isEmpty());
    QVERIFY(!tm->currentAppThemeId().isEmpty());
    QVERIFY(!tm->currentAppTheme().id.isEmpty());
}

void TestTheme::pairedTerminalThemesMatchShell()
{
    ThemeManager *tm = ThemeManager::instance();
    for (const AppTheme &app : tm->appThemes()) {
        const QString pairedId = tm->pairedTerminalThemeId(app);
        const TerminalTheme terminal = tm->terminalTheme(pairedId);
        QVERIFY2(!terminal.id.isEmpty(),
                 qPrintable(QStringLiteral("%1 -> %2").arg(app.id, pairedId)));

        // 同名主题优先；否则按明暗回落到 vscode-dark / vscode-light
        if (tm->terminalTheme(app.id).id.isEmpty()) {
            QCOMPARE(pairedId, app.dark ? QStringLiteral("vscode-dark")
                                        : QStringLiteral("vscode-light"));
        } else {
            QCOMPARE(pairedId, app.id);
        }

        const bool terminalDark = terminal.defaultBackground.lightness() < 128;
        QVERIFY2(terminalDark == app.dark,
                 qPrintable(QStringLiteral("%1 (%2) vs %3 (%4)")
                                .arg(app.id)
                                .arg(app.dark ? "dark" : "light")
                                .arg(pairedId)
                                .arg(terminalDark ? "dark" : "light")));
    }
}

void TestTheme::stylesheetTokensResolved()
{
    resetSettings();
    QVERIFY(!qApp->styleSheet().isEmpty());

    const QString qss = qApp->styleSheet();
    // 所有 @token@ 都必须被替换，否则控件会拿到非法样式值
    const QRegularExpression unresolved(QStringLiteral("@[A-Za-z][A-Za-z0-9]*@"));
    QVERIFY2(!unresolved.match(qss).hasMatch(),
             qPrintable(unresolved.match(qss).captured(0)));

    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    QVERIFY(qss.contains(theme.windowBackground.name()));
    QVERIFY(qss.contains(theme.accent.name()));

    // 调色板同步当前主题
    QCOMPARE(qApp->palette().color(QPalette::Window), theme.windowBackground);
    QCOMPARE(qApp->palette().color(QPalette::WindowText), theme.windowText);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), theme.highlight);
}

void TestTheme::switchAppThemeAndAutoPairing()
{
    resetSettings();
    ThemeManager *tm = ThemeManager::instance();
    QVERIFY(tm->terminalThemeFollowsApp());

    int changes = 0;
    QString lastChangedId;
    connect(tm, &ThemeManager::appThemeChanged, this,
            [&](const AppTheme &theme) {
                ++changes;
                lastChangedId = theme.id;
            });

    tm->setAppTheme(QStringLiteral("default-dark"));
    QCOMPARE(tm->currentAppThemeId(), QString("default-dark"));
    QCOMPARE(changes, 1);
    QCOMPARE(lastChangedId, QString("default-dark"));
    QCOMPARE(qApp->palette().color(QPalette::Window),
             tm->appTheme(QStringLiteral("default-dark")).windowBackground);
    QVERIFY(qApp->styleSheet().contains(
        tm->appTheme(QStringLiteral("default-dark")).windowBackground.name()));

    // 自动配对：外壳转深色，终端配色跟着转到对应的深色主题
    QVERIFY(tm->terminalThemeFollowsApp());
    QCOMPARE(tm->currentTerminalThemeId(),
             tm->pairedTerminalThemeId(tm->appTheme(QStringLiteral("default-dark"))));

    // 持久化
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    QCOMPARE(settings.value(QStringLiteral("AppTheme")).toString(),
             QString("default-dark"));
    QCOMPARE(settings.value(QStringLiteral("TerminalThemeAuto")).toBool(), true);
    settings.endGroup();

    // 重复设置同一主题：不发信号、不重复写配置
    tm->setAppTheme(QStringLiteral("default-dark"));
    QCOMPARE(changes, 1);

    // 未知主题：完全忽略，保持当前主题
    tm->setAppTheme(QStringLiteral("no-such-theme"));
    QCOMPARE(tm->currentAppThemeId(), QString("default-dark"));
    QCOMPARE(changes, 1);

    disconnect(tm, &ThemeManager::appThemeChanged, this, nullptr);
}

void TestTheme::explicitTerminalSelectionStopsAutoPairing()
{
    resetSettings();
    ThemeManager *tm = ThemeManager::instance();
    QVERIFY(tm->terminalThemeFollowsApp());

    int terminalChanges = 0;
    connect(tm, &ThemeManager::terminalThemeChanged, this,
            [&](const TerminalTheme &) { ++terminalChanges; });

    tm->setTerminalTheme(QStringLiteral("default"), true);
    QCOMPARE(tm->currentTerminalThemeId(), QString("default"));
    QVERIFY(!tm->terminalThemeFollowsApp());
    QCOMPARE(terminalChanges, 1);

    // 用户显式选择后，切换外壳主题不应再改终端配色
    tm->setAppTheme(QStringLiteral("nord"));
    QCOMPARE(tm->currentAppThemeId(), QString("nord"));
    QCOMPARE(tm->currentTerminalThemeId(), QString("default"));
    QCOMPARE(terminalChanges, 1);

    // 重复选择同一配色不发信号；未知 id 不影响当前状态
    tm->setTerminalTheme(QStringLiteral("default"), true);
    tm->setTerminalTheme(QStringLiteral("no-such-theme"), true);
    QCOMPARE(tm->currentTerminalThemeId(), QString("default"));
    QCOMPARE(terminalChanges, 1);

    // 非用户触发的切换（自动配对）不改变"是否跟随"标记
    tm->setTerminalTheme(QStringLiteral("vscode-dark"), false);
    QCOMPARE(tm->currentTerminalThemeId(), QString("vscode-dark"));
    QVERIFY(!tm->terminalThemeFollowsApp());

    disconnect(tm, &ThemeManager::terminalThemeChanged, this, nullptr);

    // 重启（重新读取设置）应保留用户选择。
    // 非用户触发的切换不写配置，所以终端配色回到最后一次用户选择 "default"。
    tm->loadSettings();
    QCOMPARE(tm->currentAppThemeId(), QString("nord"));
    QCOMPARE(tm->currentTerminalThemeId(), QString("default"));
    QVERIFY(!tm->terminalThemeFollowsApp());
}

void TestTheme::loadSettingsFallbackForStaleIds()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    settings.setValue(QStringLiteral("AppTheme"), QStringLiteral("removed-app-theme"));
    settings.setValue(QStringLiteral("TerminalTheme"), QStringLiteral("removed-term-theme"));
    settings.setValue(QStringLiteral("TerminalThemeAuto"), false);
    settings.endGroup();
    settings.sync();

    ThemeManager *tm = ThemeManager::instance();
    tm->loadSettings();

    // 失效 id 必须回落到默认外壳主题和配套终端配色，而不是留下空主题
    QCOMPARE(tm->currentAppThemeId(), QString("fresh-light"));
    QVERIFY(!tm->currentAppTheme().id.isEmpty());
    QVERIFY(!tm->currentTerminalThemeId().isEmpty());
    QCOMPARE(tm->currentTerminalThemeId(),
             tm->pairedTerminalThemeId(tm->currentAppTheme()));
    QVERIFY(!qApp->styleSheet().isEmpty());
    QVERIFY(!tm->terminalThemeFollowsApp());

    resetSettings();
}

void TestTheme::staleTerminalIdFallsBackToPairedTheme()
{
    // 仅终端配色失效（外壳主题有效）时也要回落
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    settings.setValue(QStringLiteral("AppTheme"), QStringLiteral("dracula"));
    settings.setValue(QStringLiteral("TerminalTheme"), QStringLiteral("removed-term-theme"));
    settings.setValue(QStringLiteral("TerminalThemeAuto"), false);
    settings.endGroup();
    settings.sync();

    ThemeManager *tm = ThemeManager::instance();
    tm->loadSettings();
    QCOMPARE(tm->currentAppThemeId(), QString("dracula"));
    QCOMPARE(tm->currentTerminalThemeId(),
             tm->pairedTerminalThemeId(tm->appTheme(QStringLiteral("dracula"))));

    resetSettings();
}

void TestTheme::iconColorAndWindowBorderSmoke()
{
    resetSettings();
    ThemeManager *tm = ThemeManager::instance();
    QCOMPARE(tm->iconColor(), tm->currentAppTheme().textDim);
    QVERIFY(tm->iconColor().isValid());

    // 传入空窗口/无效颜色不得崩溃
    ThemeManager::setWindowBorderColor(nullptr, QColor(Qt::blue));
    QWidget widget;
    ThemeManager::setWindowBorderColor(&widget, QColor());
    ThemeManager::setWindowBorderColor(&widget, QColor(Qt::blue));
}

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
