#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "settings/Settings.h"
#include "settings/Toml.h"
#include "ui/UiStateStore.h"

using namespace palune;

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void tomlRoundTripPreservesUnknownKeys()
    {
        QString error;
        const QString text = QStringLiteral(
            "# 用户注释\n"
            "[ui]\n"
            "theme = \"B\"\n"
            "futureKey = \"保留\"\n"
            "\n"
            "[window]\n"
            "width = 1200\n"
            "height = 720\n"
            "maximized = false\n"
            "\n"
            "[files]\n"
            "recent = [\"C:\\\\media\\\\a.mkv\", \"C:\\\\media\\\\b.mp4\"]\n");
        const QVariantMap values = Toml::parse(text, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(values.value(QStringLiteral("ui.theme")).toString(), QStringLiteral("B"));
        QCOMPARE(values.value(QStringLiteral("ui.futureKey")).toString(), QStringLiteral("保留"));
        QCOMPARE(values.value(QStringLiteral("window.width")).toInt(), 1200);
        QCOMPARE(values.value(QStringLiteral("files.recent")).toStringList().size(), 2);

        const QString serialized = Toml::serialize(values);
        const QVariantMap again = Toml::parse(serialized, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(again.value(QStringLiteral("ui.futureKey")).toString(), QStringLiteral("保留"));
        QCOMPARE(again.value(QStringLiteral("files.recent")).toStringList(),
                 values.value(QStringLiteral("files.recent")).toStringList());
    }

    void rejectsBrokenToml()
    {
        QString error;
        const QVariantMap values = Toml::parse(QStringLiteral("[ui]\ntheme B\n"), &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(values.isEmpty());
    }

    void missingFileStartsEmptyAndSaves()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        QString error;
        QVERIFY(settings.load(&error));
        QVERIFY(error.isEmpty());
        QVERIFY(settings.isFirstRun());
        QVERIFY(!settings.hasWindowGeometry());

        settings.setTheme(QStringLiteral("C"));
        settings.setWindowGeometry(QRect(10, 20, 800, 500));
        settings.addRecentFile(QStringLiteral("D:/media/x.mkv"));
        QVERIFY(settings.save(&error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(QFile::exists(settings.path()));

        Settings reloaded(settings.path());
        QVERIFY(reloaded.load(&error));
        QCOMPARE(reloaded.rawTheme(), QStringLiteral("C"));
        QCOMPARE(reloaded.windowGeometry(), QRect(10, 20, 800, 500));
        QCOMPARE(reloaded.recentFiles().size(), 1);
        QVERIFY(!reloaded.isFirstRun());
    }

    void recentFilesDedupeAndLimit()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        for (int i = 0; i < 15; ++i)
            settings.addRecentFile(QStringLiteral("D:/m/%1.mkv").arg(i));
        QCOMPARE(settings.recentFiles().size(), 10);
        settings.addRecentFile(QStringLiteral("D:/m/7.mkv"));
        QCOMPARE(settings.recentFiles().first(), QStringLiteral("D:/m/7.mkv"));
        QCOMPARE(settings.recentFiles().size(), 10);
        QCOMPARE(settings.recentFiles().count(QStringLiteral("D:/m/7.mkv")), 1);
    }

    void windowGeometryRoundTrip()
    {
        Settings settings(QStringLiteral("unused.toml"));
        QVERIFY(!settings.hasWindowGeometry());
        settings.setWindowGeometry(QRect(4, 8, 1280, 720));
        QVERIFY(settings.hasWindowGeometry());
        QCOMPARE(settings.windowGeometry(), QRect(4, 8, 1280, 720));
        settings.setWindowMaximized(true);
        QVERIFY(settings.windowMaximized());
    }

    void resumePositionNormalisesPathSeparators()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString native = QDir(dir.path()).filePath(QStringLiteral("clip.mkv"));
        {
            QFile file(native);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        const QString slashed = QDir::fromNativeSeparators(native);
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        QString error;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.resumePosition(native), 0.0);

        settings.setResumePosition(native, 42.5);
        QCOMPARE(settings.resumePosition(native), 42.5);
        QCOMPARE(settings.resumePosition(slashed), 42.5);

        // 非正数不写入，避免把"从头播放"当成续播点。
        settings.setResumePosition(native, 0.0);
        QCOMPARE(settings.resumePosition(native), 42.5);
        QCOMPARE(settings.resumePosition(QString()), 0.0);

        QVERIFY(settings.save(&error));
        Settings reloaded(settings.path());
        QVERIFY(reloaded.load(&error));
        QCOMPARE(reloaded.resumePosition(native), 42.5);
        QCOMPARE(reloaded.resumePosition(slashed), 42.5);
    }

    void uiStatePersistsPanelWidths()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        UiStateStore state(&settings);
        QCOMPARE(state.panelWidth(QStringLiteral("B")), 320);
        state.setPanelWidth(QStringLiteral("B"), 420);
        QCOMPARE(state.panelWidth(QStringLiteral("B")), 420);
        QCOMPARE(state.panelWidth(QStringLiteral("C")), 320);
        state.setScrollIndex(12);
        state.setSelectedItemId(QStringLiteral("item-42"));
        QCOMPARE(state.scrollIndex(), 12);
        QCOMPARE(state.selectedItemId(), QStringLiteral("item-42"));
        QVERIFY(state.chromeVisible());
        state.setChromeVisible(false);
        QVERIFY(!state.chromeVisible());
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
