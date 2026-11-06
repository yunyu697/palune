#include <QtTest>

#include <QTemporaryDir>

#include "settings/Settings.h"
#include "theme/ThemeRegistry.h"
#include "ui/UiStateStore.h"

using namespace palune;

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void validThemeLoads()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        settings.setTheme(QStringLiteral("B"));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        QCOMPARE(registry.currentTheme(), QStringLiteral("B"));
        QVERIFY(registry.fallbackReason().isEmpty());
    }

    void missingKeyFallsBackToA()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        QCOMPARE(registry.currentTheme(), QStringLiteral("A"));
        QVERIFY(registry.fallbackReason().contains(QStringLiteral("缺少")));
    }

    void invalidValueFallsBackToA()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        settings.setTheme(QStringLiteral("Z"));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        QCOMPARE(registry.currentTheme(), QStringLiteral("A"));
        QVERIFY(registry.fallbackReason().contains(QStringLiteral("非法")));
    }

    void missingResourceFallsBackToA()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        settings.setTheme(QStringLiteral("C"));
        ThemeRegistry registry(&settings, [](const QString &path) { return !path.contains(QStringLiteral("LayoutC")); });
        QCOMPARE(registry.currentTheme(), QStringLiteral("A"));
        QVERIFY(registry.fallbackReason().contains(QStringLiteral("资源缺失")));
    }

    void switchPersistsAndTimesUnder300ms()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        const ThemeRegistry::SwitchResult result = registry.switchTo(QStringLiteral("C"));
        QVERIFY(result.ok);
        QCOMPARE(result.theme, QStringLiteral("C"));
        QVERIFY(result.elapsedMs < 300.0);
        QVERIFY(registry.switchLog().size() >= 1);

        Settings reloaded(settings.path());
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.rawTheme(), QStringLiteral("C"));
    }

    void unknownThemeRejectedAndStateUnchanged()
    {
        Settings settings(QStringLiteral("unused.toml"));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        const ThemeRegistry::SwitchResult result = registry.switchTo(QStringLiteral("NOPE"));
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(registry.currentTheme(), QStringLiteral("A"));
    }

    void cycleThemeWalksAvailableThemes()
    {
        Settings settings(QStringLiteral("unused.toml"));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        QCOMPARE(registry.cycleTheme(), QStringLiteral("B"));
        QCOMPARE(registry.cycleTheme(), QStringLiteral("C"));
        QCOMPARE(registry.cycleTheme(), QStringLiteral("A"));
    }

    void uiStateSurvivesThemeContextSwitch()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        ThemeRegistry registry(&settings, [](const QString &) { return true; });
        UiStateStore state(&settings);
        state.setScrollIndex(7);
        state.setSelectedItemId(QStringLiteral("ep-12"));
        registry.switchTo(QStringLiteral("B"));
        registry.switchTo(QStringLiteral("C"));
        QCOMPARE(state.scrollIndex(), 7);
        QCOMPARE(state.selectedItemId(), QStringLiteral("ep-12"));
    }
};

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"
