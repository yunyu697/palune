#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QTemporaryDir>

#include "core/AppPaths.h"
#include "settings/Preferences.h"
#include "settings/Settings.h"
#include "ui/ActionRegistry.h"
#include "ui/CompactController.h"

using namespace palune;

// Preferences 是配置持久化的唯一入口：QML 改设置 → 立即原子落盘 → 重启后读回。
// 这里不构造 MpvClient / UpdateService（它们依赖真实 mpv 与网络），播放器分支由
// M1 冒烟覆盖；本用例聚焦「读默认值 / 读写持久化 / 取值范围 / 导出导入 / 键位覆盖」。
class TestPreferences : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QVariant settingsValue(const Settings &settings, const QString &key)
    {
        return settings.value(key);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        AppPaths::setConfigDirOverride(m_dir.path());
    }

    void cleanupTestCase()
    {
        AppPaths::setConfigDirOverride(QString());
    }

    void defaultsFromEmptySettings()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);

        QCOMPARE(prefs.compactBreakpoint(), 560);
        QVERIFY(!prefs.restoreLastSession());
        QVERIFY(prefs.autoPlay());
        QVERIFY(!prefs.startMaximized());
        QVERIFY(!prefs.closeToTray());
        QVERIFY(!prefs.minimizeToTray());
        QVERIFY(!prefs.confirmExit());
        QCOMPARE(prefs.logLevel(), QStringLiteral("info"));

        QCOMPARE(prefs.hwdecMode(), QStringLiteral("auto"));
        QCOMPARE(prefs.cacheSeconds(), 0);
        QVERIFY(prefs.toneMapping());
        QCOMPARE(prefs.targetPeakNits(), 1000);
        QCOMPARE(prefs.deinterlace(), QStringLiteral("auto"));
        QVERIFY(prefs.resumePlayback());
        QVERIFY(!prefs.autoNext());

        QCOMPARE(prefs.subtitleFontSize(), 15);
        QCOMPARE(prefs.subtitleColor(), QStringLiteral("#ffffff"));
        QCOMPARE(prefs.subtitleOutline(), 1.0);
        QCOMPARE(prefs.subtitleDelay(), 0.0);
        QCOMPARE(prefs.subtitlePosition(), 8);
        QVERIFY(prefs.autoLoadSubtitles());
        QCOMPARE(prefs.subtitleEncoding(), QStringLiteral("auto"));

        QCOMPARE(prefs.audioDevice(), QStringLiteral("auto"));
        QCOMPARE(prefs.audioChannels(), QStringLiteral("auto"));
        QCOMPARE(prefs.audioDelay(), 0.0);

        QCOMPARE(prefs.updateChannel(), QStringLiteral("stable"));
        QVERIFY(prefs.autoCheckUpdates());
        QCOMPARE(prefs.updateProxy(), QStringLiteral("system"));
        QVERIFY(prefs.downloadDir().isEmpty());

        QCOMPARE(prefs.thumbCacheLimitMb(), 2000);
        QCOMPARE(prefs.logRetentionDays(), 14);
        QVERIFY(prefs.storageNotice().isEmpty());
        QCOMPARE(prefs.language(), QStringLiteral("system"));

        // 无 MpvClient / ActionRegistry 时辅助接口必须安全返回空值。
        QVERIFY(prefs.audioDevices().isEmpty());
        QVERIFY(prefs.keyRows().isEmpty());
        QVERIFY(prefs.keySpec(QStringLiteral("play_pause")).isEmpty());
        QVERIFY(prefs.specForKey(Qt::Key_Space, Qt::NoModifier).isEmpty());
        QVERIFY(prefs.keyConflicts().isEmpty());
        prefs.applyAll();
        prefs.saveSessionState();
    }

    void reloadHonoursStoredValues()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        settings.setValue(QStringLiteral("ui.restoreLastSession"), true);
        settings.setValue(QStringLiteral("ui.autoPlay"), false);
        settings.setValue(QStringLiteral("ui.startMaximized"), true);
        settings.setValue(QStringLiteral("ui.closeToTray"), true);
        settings.setValue(QStringLiteral("ui.minimizeToTray"), true);
        settings.setValue(QStringLiteral("ui.confirmExit"), true);
        settings.setValue(QStringLiteral("diag.logLevel"), QStringLiteral("debug"));
        settings.setValue(QStringLiteral("playback.hwdec"), QStringLiteral("d3d11va"));
        settings.setValue(QStringLiteral("playback.cacheSeconds"), 30);
        settings.setValue(QStringLiteral("playback.toneMapping"), false);
        settings.setValue(QStringLiteral("playback.targetPeakNits"), 1600);
        settings.setValue(QStringLiteral("playback.deinterlace"), QStringLiteral("on"));
        settings.setValue(QStringLiteral("playback.resume"), false);
        settings.setValue(QStringLiteral("playback.autoNext"), true);
        settings.setValue(QStringLiteral("sub.fontSize"), 24);
        settings.setValue(QStringLiteral("sub.color"), QStringLiteral("#ffcc00"));
        settings.setValue(QStringLiteral("sub.outline"), 2.5);
        settings.setValue(QStringLiteral("sub.delay"), -1.5);
        settings.setValue(QStringLiteral("sub.position"), 20);
        settings.setValue(QStringLiteral("sub.autoLoad"), false);
        settings.setValue(QStringLiteral("sub.encoding"), QStringLiteral("gbk"));
        settings.setValue(QStringLiteral("audio.device"), QStringLiteral("wasapi"));
        settings.setValue(QStringLiteral("audio.channels"), QStringLiteral("stereo"));
        settings.setValue(QStringLiteral("audio.delay"), 0.4);
        settings.setValue(QStringLiteral("update.channel"), QStringLiteral("alpha"));
        settings.setValue(QStringLiteral("update.autoCheck"), false);
        settings.setValue(QStringLiteral("update.proxy"), QStringLiteral("none"));
        settings.setValue(QStringLiteral("update.downloadDir"), QStringLiteral("D:/dl"));
        settings.setValue(QStringLiteral("storage.thumbCacheLimitMb"), 500);
        settings.setValue(QStringLiteral("storage.logRetentionDays"), 30);
        settings.setValue(QStringLiteral("ui.language"), QStringLiteral("en_US"));

        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        QVERIFY(prefs.restoreLastSession());
        QVERIFY(!prefs.autoPlay());
        QVERIFY(prefs.startMaximized());
        QVERIFY(prefs.closeToTray());
        QVERIFY(prefs.minimizeToTray());
        QVERIFY(prefs.confirmExit());
        QCOMPARE(prefs.logLevel(), QStringLiteral("debug"));

        QCOMPARE(prefs.hwdecMode(), QStringLiteral("d3d11va"));
        QCOMPARE(prefs.cacheSeconds(), 30);
        QVERIFY(!prefs.toneMapping());
        QCOMPARE(prefs.targetPeakNits(), 1600);
        QCOMPARE(prefs.deinterlace(), QStringLiteral("on"));
        QVERIFY(!prefs.resumePlayback());
        QVERIFY(prefs.autoNext());

        QCOMPARE(prefs.subtitleFontSize(), 24);
        QCOMPARE(prefs.subtitleColor(), QStringLiteral("#ffcc00"));
        QCOMPARE(prefs.subtitleOutline(), 2.5);
        QCOMPARE(prefs.subtitleDelay(), -1.5);
        QCOMPARE(prefs.subtitlePosition(), 20);
        QVERIFY(!prefs.autoLoadSubtitles());
        QCOMPARE(prefs.subtitleEncoding(), QStringLiteral("gbk"));

        QCOMPARE(prefs.audioDevice(), QStringLiteral("wasapi"));
        QCOMPARE(prefs.audioChannels(), QStringLiteral("stereo"));
        QCOMPARE(prefs.audioDelay(), 0.4);

        QCOMPARE(prefs.updateChannel(), QStringLiteral("alpha"));
        QVERIFY(!prefs.autoCheckUpdates());
        QCOMPARE(prefs.updateProxy(), QStringLiteral("none"));
        QCOMPARE(prefs.downloadDir(), QStringLiteral("D:/dl"));

        QCOMPARE(prefs.thumbCacheLimitMb(), 500);
        QCOMPARE(prefs.logRetentionDays(), 30);
        QCOMPARE(prefs.language(), QStringLiteral("en_US"));
    }

    void invalidValuesFallBackToDefaults()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        // 类型不符（字符串写进整型键）与未知语言都必须回退，不能让脏配置把界面搞坏。
        settings.setValue(QStringLiteral("playback.targetPeakNits"), QStringLiteral("not-a-number"));
        settings.setValue(QStringLiteral("sub.delay"), QStringLiteral("abc"));
        settings.setValue(QStringLiteral("ui.language"), QStringLiteral("fr_FR"));
        settings.setValue(QStringLiteral("diag.logLevel"), QString());

        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        QCOMPARE(prefs.targetPeakNits(), 1000);
        QCOMPARE(prefs.subtitleDelay(), 0.0);
        QCOMPARE(prefs.language(), QStringLiteral("system"));
        QCOMPARE(prefs.logLevel(), QStringLiteral("info"));
    }

    void settersClampAndPersist()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("settings.toml"));
        Settings settings(path);
        Settings probe(path);
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);

        prefs.setCacheSeconds(999);
        prefs.setTargetPeakNits(100);
        prefs.setSubtitleFontSize(200);
        prefs.setSubtitleOutline(-3.0);
        prefs.setSubtitleDelay(99.0);
        prefs.setSubtitlePosition(99);
        prefs.setAudioDelay(-20.0);
        prefs.setThumbCacheLimitMb(1);
        prefs.setLogRetentionDays(9999);
        prefs.setLanguage(QStringLiteral("fr_FR"));

        QCOMPARE(prefs.cacheSeconds(), 120);
        QCOMPARE(prefs.targetPeakNits(), 200);
        QCOMPARE(prefs.subtitleFontSize(), 64);
        QCOMPARE(prefs.subtitleOutline(), 0.0);
        QCOMPARE(prefs.subtitleDelay(), 10.0);
        QCOMPARE(prefs.subtitlePosition(), 40);
        QCOMPARE(prefs.audioDelay(), -10.0);
        QCOMPARE(prefs.thumbCacheLimitMb(), 100);
        QCOMPARE(prefs.logRetentionDays(), 365);
        QCOMPARE(prefs.language(), QStringLiteral("system"));

        // 每次写入都立即保存：换一个 Settings 实例直接读文件即可复核。
        QVERIFY(probe.load());
        QCOMPARE(probe.value(QStringLiteral("playback.cacheSeconds")).toInt(), 120);
        QCOMPARE(probe.value(QStringLiteral("sub.fontSize")).toInt(), 64);
        QCOMPARE(probe.value(QStringLiteral("storage.thumbCacheLimitMb")).toInt(), 100);
        // 非法语言归一为 system；它与默认值相同，按「无变化不落盘」处理。
        QCOMPARE(prefs.language(), QStringLiteral("system"));
    }

    void settersNoOpWhenUnchanged()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        QSignalSpy spy(&prefs, &Preferences::preferencesChanged);
        const int before = spy.count();

        prefs.setAutoPlay(prefs.autoPlay());
        prefs.setStartMaximized(prefs.startMaximized());
        prefs.setCloseToTray(prefs.closeToTray());
        prefs.setMinimizeToTray(prefs.minimizeToTray());
        prefs.setConfirmExit(prefs.confirmExit());
        prefs.setRestoreLastSession(prefs.restoreLastSession());
        prefs.setHwdecMode(prefs.hwdecMode());
        prefs.setCacheSeconds(prefs.cacheSeconds());
        prefs.setToneMapping(prefs.toneMapping());
        prefs.setTargetPeakNits(prefs.targetPeakNits());
        prefs.setDeinterlace(prefs.deinterlace());
        prefs.setResumePlayback(prefs.resumePlayback());
        prefs.setAutoNext(prefs.autoNext());
        prefs.setSubtitleFontSize(prefs.subtitleFontSize());
        prefs.setSubtitleColor(prefs.subtitleColor());
        prefs.setSubtitleOutline(prefs.subtitleOutline());
        prefs.setSubtitleDelay(prefs.subtitleDelay());
        prefs.setSubtitlePosition(prefs.subtitlePosition());
        prefs.setAutoLoadSubtitles(prefs.autoLoadSubtitles());
        prefs.setSubtitleEncoding(prefs.subtitleEncoding());
        prefs.setAudioDevice(prefs.audioDevice());
        prefs.setAudioChannels(prefs.audioChannels());
        prefs.setAudioDelay(prefs.audioDelay());
        prefs.setUpdateChannel(prefs.updateChannel());
        prefs.setAutoCheckUpdates(prefs.autoCheckUpdates());
        prefs.setUpdateProxy(prefs.updateProxy());
        prefs.setDownloadDir(prefs.downloadDir());
        prefs.setThumbCacheLimitMb(prefs.thumbCacheLimitMb());
        prefs.setLogRetentionDays(prefs.logRetentionDays());
        prefs.setLanguage(prefs.language());

        QCOMPARE(spy.count(), before);
    }

    void everySetterEmitsAndRoundTrips()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("settings.toml"));
        Settings settings(path);
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        QSignalSpy spy(&prefs, &Preferences::preferencesChanged);
        prefs.setAutoPlay(false);
        prefs.setStartMaximized(true);
        prefs.setCloseToTray(true);
        prefs.setMinimizeToTray(true);
        prefs.setConfirmExit(true);
        prefs.setRestoreLastSession(true);
        prefs.setHwdecMode(QStringLiteral("vaapi"));
        prefs.setCacheSeconds(45);
        prefs.setToneMapping(false);
        prefs.setTargetPeakNits(1500);
        prefs.setDeinterlace(QStringLiteral("off"));
        prefs.setResumePlayback(false);
        prefs.setAutoNext(true);
        prefs.setSubtitleFontSize(18);
        prefs.setSubtitleColor(QStringLiteral("#00ff00"));
        prefs.setSubtitleOutline(1.5);
        prefs.setSubtitleDelay(1.25);
        prefs.setSubtitlePosition(12);
        prefs.setAutoLoadSubtitles(false);
        prefs.setSubtitleEncoding(QStringLiteral("utf-8"));
        prefs.setAudioDevice(QStringLiteral("coreaudio"));
        prefs.setAudioChannels(QStringLiteral("mono"));
        prefs.setAudioDelay(-0.5);
        prefs.setUpdateChannel(QStringLiteral("rc"));
        prefs.setAutoCheckUpdates(false);
        prefs.setDownloadDir(QStringLiteral("E:/media"));
        prefs.setThumbCacheLimitMb(800);
        prefs.setLogRetentionDays(7);
        prefs.setLanguage(QStringLiteral("zh_CN"));
        QCOMPARE(spy.count(), 29);

        Settings reloaded(path);
        QVERIFY(reloaded.load());
        Preferences again(&reloaded, nullptr, nullptr, nullptr, nullptr);
        QVERIFY(!again.autoPlay());
        QCOMPARE(again.hwdecMode(), QStringLiteral("vaapi"));
        QCOMPARE(again.cacheSeconds(), 45);
        QCOMPARE(again.subtitleFontSize(), 18);
        QCOMPARE(again.subtitleOutline(), 1.5);
        QCOMPARE(again.subtitleDelay(), 1.25);
        QCOMPARE(again.audioDelay(), -0.5);
        QCOMPARE(again.updateChannel(), QStringLiteral("rc"));
        QCOMPARE(again.downloadDir(), QStringLiteral("E:/media"));
        QCOMPARE(again.thumbCacheLimitMb(), 800);
        QCOMPARE(again.logRetentionDays(), 7);
        QCOMPARE(again.language(), QStringLiteral("zh_CN"));
    }

    void logLevelRejectsUnknownName()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        prefs.setLogLevel(QStringLiteral("warn"));
        QCOMPARE(prefs.logLevel(), QStringLiteral("warn"));
        // 非法级别不写入，保持上一个有效值。
        prefs.setLogLevel(QStringLiteral("verbose"));
        QCOMPARE(prefs.logLevel(), QStringLiteral("warn"));
        prefs.setLogLevel(QStringLiteral("info"));
        QCOMPARE(prefs.logLevel(), QStringLiteral("info"));
    }

    void updateProxyModes()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);

        // 非法片段（不是 host:port）不生效：只校验分支被执行且配置值原样保留。
        prefs.setUpdateProxy(QStringLiteral("manual:broken"));
        QCOMPARE(prefs.updateProxy(), QStringLiteral("manual:broken"));

        prefs.setUpdateProxy(QStringLiteral("none"));
        QCOMPARE(QNetworkProxy::applicationProxy().type(), QNetworkProxy::NoProxy);

        prefs.setUpdateProxy(QStringLiteral("manual:127.0.0.1:7890"));
        const QNetworkProxy proxy = QNetworkProxy::applicationProxy();
        QCOMPARE(proxy.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(proxy.hostName(), QStringLiteral("127.0.0.1"));
        QCOMPARE(proxy.port(), static_cast<quint16>(7890));

        // system：交给 Qt 读取系统配置，不在这里断言具体类型。
        prefs.setUpdateProxy(QStringLiteral("system"));
        QCOMPARE(prefs.updateProxy(), QStringLiteral("system"));
    }

    void downloadDirEffective()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        QVERIFY(prefs.downloadDirEffective().contains(QStringLiteral("Palune")));
        prefs.setDownloadDir(QStringLiteral("D:/downloads/palune"));
        QCOMPARE(prefs.downloadDirEffective(), QStringLiteral("D:/downloads/palune"));
    }

    void keyBindingsFlowThroughActionRegistry()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.toml")));
        ActionRegistry actions;
        Preferences prefs(&settings, nullptr, nullptr, nullptr, &actions);

        const QVector<ActionRegistry::Action> table = ActionRegistry::actionTable();
        QVERIFY(!table.isEmpty());
        const QString id = table.first().id;

        const QVariantList rows = prefs.keyRows();
        QCOMPARE(rows.size(), table.size());
        QCOMPARE(rows.first().toMap().value(QStringLiteral("id")).toString(), id);

        prefs.setKeyBinding(id, QStringLiteral("F7"));
        QCOMPARE(prefs.keySpec(id), QStringLiteral("F7"));
        settings.load();
        QVERIFY(settings.value(QStringLiteral("keys.") + id).isValid());

        QVariantList switched = prefs.keyRows();
        QVERIFY(switched.first().toMap().value(QStringLiteral("custom")).toBool());

        prefs.clearKeyBinding(id);
        QCOMPARE(prefs.keySpec(id), QString());

        prefs.resetKeyBinding(id);
        QVERIFY(!settings.value(QStringLiteral("keys.") + id).isValid());

        // 空 id 与空指针防护分支。
        prefs.setKeyBinding(QString(), QStringLiteral("F8"));
        prefs.clearKeyBinding(QString());
        prefs.resetKeyBinding(QString());

        QSignalSpy spy(&prefs, &Preferences::preferencesChanged);
        prefs.setKeyBinding(id, QStringLiteral("F9"));
        QCOMPARE(spy.count(), 1);
    }

    void exportAndImportConfig()
    {
        QTemporaryDir dir;
        Settings settings(AppPaths::settingsFile());
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        settings.setTheme(QStringLiteral("C"));
        QVERIFY(settings.save());

        const QString exportPath = dir.filePath(QStringLiteral("export/nested/palune-settings.toml"));
        QString error;
        QVERIFY2(prefs.exportConfigTo(exportPath, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(exportPath));
        QVERIFY(prefs.storageNotice().contains(QStringLiteral("已导出配置")));

        // 覆盖同一路径（QFile::copy 不覆盖，代码里先删后拷）。
        QVERIFY2(prefs.exportConfigTo(exportPath, &error), qPrintable(error));
        QVERIFY2(!prefs.exportConfigTo(QString(), &error), "空路径必须拒绝");
        QVERIFY(!error.isEmpty());

        Settings probe(exportPath);
        QVERIFY(probe.load());
        QCOMPARE(probe.rawTheme(), QStringLiteral("C"));

        QVERIFY2(prefs.importConfigFrom(exportPath, &error), qPrintable(error));
        QVERIFY(prefs.storageNotice().contains(QStringLiteral("已导入配置")));

        const QString broken = dir.filePath(QStringLiteral("broken.toml"));
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[ui]\ntheme B\n");
        f.close();
        error.clear();
        QVERIFY2(!prefs.importConfigFrom(broken, &error), "TOML 解析失败必须拒绝导入");
        QVERIFY(error.contains(QStringLiteral("配置格式无效")));
        QVERIFY(!prefs.importConfigFrom(dir.filePath(QStringLiteral("missing.toml")), &error));
    }

    void storageUsageAndLogRetention()
    {
        QTemporaryDir dir;
        const QString configDir = dir.path();
        AppPaths::setConfigDirOverride(configDir);
        QDir().mkpath(configDir + QStringLiteral("/library/thumbs"));
        QDir().mkpath(configDir + QStringLiteral("/logs"));

        QFile thumb(configDir + QStringLiteral("/library/thumbs/a.jpg"));
        QVERIFY(thumb.open(QIODevice::WriteOnly));
        thumb.write(QByteArray(2048, 'x'));
        thumb.close();
        QFile index(configDir + QStringLiteral("/library/index.json"));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write(QByteArray(1024 * 1024, 'x'));
        index.close();
        QFile log(configDir + QStringLiteral("/logs/old.log"));
        QVERIFY(log.open(QIODevice::WriteOnly));
        log.write(QByteArray(3 * 1024 * 1024, 'x'));
        log.close();

        Settings settings(AppPaths::settingsFile());
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);

        const QVariantMap usage = prefs.storageUsage();
        QCOMPARE(usage.value(QStringLiteral("thumbsBytes")).toLongLong(), qint64(2048));
        QCOMPARE(usage.value(QStringLiteral("indexBytes")).toLongLong(), qint64(1024 * 1024));
        QCOMPARE(usage.value(QStringLiteral("logsBytes")).toLongLong(), qint64(3 * 1024 * 1024));
        QVERIFY(usage.value(QStringLiteral("thumbsText")).toString().endsWith(QStringLiteral("KB")));
        QVERIFY(usage.value(QStringLiteral("logsText")).toString().endsWith(QStringLiteral("MB")));
        QVERIFY(usage.value(QStringLiteral("totalText")).toString().size() > 0);
        QCOMPARE(usage.value(QStringLiteral("configDir")).toString(), configDir);

        // 把旧日志改成 60 天前，然后把保留期降到 7 天：清理逻辑必须删掉它。
        QFile oldLog(configDir + QStringLiteral("/logs/old.log"));
        const QDateTime past = QDateTime::currentDateTime().addDays(-60);
        // setFileTime 要求文件处于打开状态。
        QVERIFY(oldLog.open(QIODevice::ReadWrite));
        QVERIFY(oldLog.setFileTime(past, QFileDevice::FileModificationTime));
        oldLog.close();
        prefs.setLogRetentionDays(7);
        QVERIFY(!QFileInfo::exists(configDir + QStringLiteral("/logs/old.log")));

        AppPaths::setConfigDirOverride(m_dir.path());
    }

    void purgeRunsAtStartup()
    {
        QTemporaryDir dir;
        AppPaths::setConfigDirOverride(dir.path());
        QDir().mkpath(dir.path() + QStringLiteral("/logs"));
        QFile stale(dir.path() + QStringLiteral("/logs/stale.log"));
        QVERIFY(stale.open(QIODevice::ReadWrite));
        QVERIFY(stale.setFileTime(QDateTime::currentDateTime().addDays(-90),
                                  QFileDevice::FileModificationTime));
        stale.close();

        Settings settings(AppPaths::settingsFile());
        settings.setValue(QStringLiteral("storage.logRetentionDays"), 3);
        Preferences prefs(&settings, nullptr, nullptr, nullptr, nullptr);
        prefs.applyAll();
        QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/logs/stale.log")));

        AppPaths::setConfigDirOverride(m_dir.path());
    }
};

QTEST_GUILESS_MAIN(TestPreferences)
#include "tst_preferences.moc"
