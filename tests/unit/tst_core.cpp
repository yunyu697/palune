#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/AppPaths.h"
#include "core/ProcessMetrics.h"
#include "core/StartupClock.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "settings/Settings.h"
#include "ui/CompactController.h"

using namespace palune;

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void appPathsFollowNamingRules()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppPaths::setConfigDirOverride(dir.path());

        QCOMPARE(QDir::fromNativeSeparators(AppPaths::configDir()),
                 QDir::fromNativeSeparators(dir.path()));
        QVERIFY(AppPaths::settingsFile().endsWith(QStringLiteral("/settings.toml")));
        QVERIFY(AppPaths::logFile().endsWith(QStringLiteral("/logs/palune.log")));
        QVERIFY(AppPaths::crashDir().endsWith(QStringLiteral("/crashes")));
        QVERIFY(AppPaths::stateFile().endsWith(QStringLiteral("/state.db")));
        QVERIFY(AppPaths::updateStagingDir(QStringLiteral("1.0.0-alpha2"))
                    .contains(QStringLiteral("1.0.0-alpha2")));

        AppPaths::setConfigDirOverride(QString());
        QVERIFY(!AppPaths::configDir().isEmpty());
    }

    void startupClockMeasuresElapsed()
    {
        StartupClock::mark();
        QTest::qSleep(20);
        const qint64 elapsed = StartupClock::elapsedMs();
        QVERIFY2(elapsed >= 10, qPrintable(QString::number(elapsed)));
        QVERIFY(elapsed < 5000);
    }

    void processMetricsReturnsRss()
    {
        const double rss = currentRssMb();
        QVERIFY2(rss > 0.0, "无法读取进程 RSS");
        QVERIFY(rss < 2048.0);
    }

    void logLevelsFilterAndWrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/logs/test.log");
        Log::init(path);

        QVERIFY(!Log::setMinimumLevelName(QStringLiteral("verbose")));
        QCOMPARE(Log::minimumLevelName(), QStringLiteral("info"));

        Log::debug(QStringLiteral("不应写入"));
        QVERIFY(Log::setMinimumLevelName(QStringLiteral("DEBUG")));
        QCOMPARE(Log::minimumLevel(), Log::Debug);
        Log::debug(QStringLiteral("调试行"));
        Log::info(QStringLiteral("信息行"));
        Log::warn(QStringLiteral("警告行"));
        Log::error(QStringLiteral("错误行"));

        QVERIFY(Log::setMinimumLevelName(QStringLiteral("warning")));
        QCOMPARE(Log::minimumLevelName(), QStringLiteral("warn"));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY(content.contains(QStringLiteral("[DEBUG] 调试行")));
        QVERIFY(content.contains(QStringLiteral("[INFO] 信息行")));
        QVERIFY(content.contains(QStringLiteral("[WARN] 警告行")));
        QVERIFY(content.contains(QStringLiteral("[ERROR] 错误行")));

        QVERIFY(Log::setMinimumLevelName(QStringLiteral("error")));
        Log::warn(QStringLiteral("不应写入"));
        file.seek(0);
        QVERIFY(!QString::fromUtf8(file.readAll()).contains(QStringLiteral("不应写入")));
    }

    void crashBreadcrumbKeepsLastFifty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppPaths::setConfigDirOverride(dir.path());
        CrashBreadcrumb::install(QStringLiteral("1.0.0-alpha2"));

        for (int i = 0; i < 60; ++i)
            CrashBreadcrumb::record(QStringLiteral("动作 %1").arg(i));
        CrashBreadcrumb::record(QStringLiteral("含引号\"与反斜杠\\的动作"));

        QCOMPARE(CrashBreadcrumb::recent().size(), 50);
        QVERIFY(CrashBreadcrumb::recent().last().contains(QStringLiteral("含引号")));

        QFile file(CrashBreadcrumb::breadcrumbFile());
        QVERIFY(file.exists());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(QStringLiteral("\"version\":\"1.0.0-alpha2\"")));
        QVERIFY(text.contains(QStringLiteral("含引号\\\"与反斜杠\\\\的动作")));
        QVERIFY(!text.contains(QStringLiteral("动作 0")));

        AppPaths::setConfigDirOverride(QString());
    }

    void compactBreakpointValidationAndHysteresis()
    {
        int bp = 0;
        QString notice;
        QVERIFY(CompactController::parseBreakpoint(QVariant(640), &bp, &notice));
        QCOMPARE(bp, 640);
        QVERIFY(notice.isEmpty());

        QVERIFY(!CompactController::parseBreakpoint(QVariant(0), &bp, &notice));
        QCOMPARE(bp, CompactController::kDefaultBreakpoint);
        QVERIFY(!notice.isEmpty());

        QVERIFY(!CompactController::parseBreakpoint(QVariant(QStringLiteral("abc")), &bp, &notice));
        QCOMPARE(bp, CompactController::kDefaultBreakpoint);
        QVERIFY(!notice.isEmpty());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppPaths::setConfigDirOverride(dir.path());
        Settings settings;
        settings.setValue(QStringLiteral("ui.layout.A.compactBelow"), 640);

        CompactController controller(&settings);
        QCOMPARE(controller.breakpoint(), 640);
        controller.feedWidth(700);
        controller.flush();
        QVERIFY(!controller.compact());
        QCOMPARE(controller.switchCount(), 0);

        controller.feedWidth(610); // 低于 640-24，进入紧凑
        controller.flush();
        QVERIFY(controller.compact());
        QCOMPARE(controller.switchCount(), 1);

        controller.feedWidth(650); // 滞回带内，保持紧凑
        controller.flush();
        QVERIFY(controller.compact());
        QCOMPARE(controller.switchCount(), 1);

        controller.feedWidth(680); // 高于 640+24，退出紧凑
        controller.flush();
        QVERIFY(!controller.compact());
        QCOMPARE(controller.switchCount(), 2);

        settings.setValue(QStringLiteral("ui.layout.A.compactBelow"), 0);
        controller.reloadBreakpoint();
        QCOMPARE(controller.breakpoint(), CompactController::kDefaultBreakpoint);
        QVERIFY(!controller.configNotice().isEmpty());
        AppPaths::setConfigDirOverride(QString());
    }
};

QTEST_GUILESS_MAIN(TestCore)

#include "tst_core.moc"
