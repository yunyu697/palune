#include "app/AppController.h"

#include "core/ProcessMetrics.h"
#include "core/StartupClock.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "m0/SelfTest.h"
#include "media/MediaLibraryService.h"
#include "mpv/MpvClient.h"
#include "settings/Settings.h"
#include "theme/LayoutHost.h"
#include "theme/ThemeRegistry.h"
#include "ui/UiStateStore.h"
#include "update/UpdateService.h"
#include "window/WindowController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QThread>
#include <QTimer>

namespace palune {

namespace {

const QStringList kMediaSuffixes = {
    QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("webm"),
    QStringLiteral("avi"), QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("flv"),
    QStringLiteral("wmv"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ogv"),
    QStringLiteral("m4a"), QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
    QStringLiteral("aac"), QStringLiteral("opus")
};

bool isMediaFile(const QFileInfo &info)
{
    return info.isFile() && kMediaSuffixes.contains(info.suffix().toLower());
}

QString defaultReportPath(const QString &suffix)
{
    return QDir::currentPath() + QStringLiteral("/m1-%1-report.json").arg(suffix);
}

// start() 在 app.exec() 之前调用，自动化模式通过 finishWithCode() 把退出码送进事件循环。

} // namespace

AppController::AppController(const Options &options, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_settings(settings)
{
    m_library = new MediaLibraryService(this);
}

void AppController::start()
{
    if (m_options.restarted) {
        writeRestartMarker();
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    if (!m_options.shotDir.isEmpty()) {
        runShots();
        return;
    }
    if (m_options.updateCheckOnly) {
        runUpdateCheckOnly();
        return;
    }
    if (!m_options.e2eMode.isEmpty()) {
        runUpdateE2E();
        return;
    }
    if (m_options.m1Smoke) {
        runM1Smoke();
        return;
    }
    if (m_options.m1Perf) {
        runM1Perf();
        return;
    }
    if (m_options.m1ThemeSwitch) {
        runM1ThemeSwitch();
        return;
    }
    startNormal();
}

void AppController::startNormal()
{
    m_player = new MpvClient(this);
    if (!m_player->initialize())
        Log::error(QStringLiteral("libmpv 初始化失败：%1").arg(m_player->loadError()));
    connect(m_player, &MpvClient::errorOccurred, this, [](const QString &message) {
        Log::error(QStringLiteral("播放器：%1").arg(message));
    });
    connect(m_player, &MpvClient::fileLoaded, this, []() {
        Log::info(QStringLiteral("文件已加载"));
    });

    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);

    m_windows = new WindowController(m_player, m_updater, m_settings, m_library, this);
    if (!m_windows->start(1180, 700)) {
        Log::error(QStringLiteral("窗口初始化失败"));
        finishWithCode(4);
        return;
    }
    m_windowReadyMs = static_cast<double>(StartupClock::elapsedMs());
    CrashBreadcrumb::record(QStringLiteral("主窗口就绪（%1ms）").arg(m_windowReadyMs));

    QString path = m_options.openPath;
    if (!path.isEmpty())
        m_windows->openFile(QUrl::fromLocalFile(path));

    if (m_options.selftest) {
        QTimer::singleShot(700, this, [this]() {
            if (!m_options.videoPath.isEmpty())
                m_windows->openFile(QUrl::fromLocalFile(m_options.videoPath));
            else
                m_player->openTestPattern();
            sleepMs(900);
            QJsonObject report = SelfTest::run(m_windows, m_player, m_options.reportPath);
            const QString overall = report.value(QStringLiteral("verdicts")).toObject()
                                        .value(QStringLiteral("overall")).toString();
            finishWithCode(overall == QLatin1String("PASS") ? 0 : 2);
        });
    }
}

void AppController::runShots()
{
    QDir().mkpath(m_options.shotDir);
    UpdateService demo;
    Settings demoSettings;
    ThemeRegistry demoTheme(&demoSettings, [](const QString &) { return true; });
    UiStateStore demoUi(&demoSettings);
    MediaLibraryService demoLibrary;
    QQuickView view;
    // 不用 offscreen 平台截图：offscreen + software 后端的字体引擎拿不到系统字体，中文会变方块。
    // 这里用不抢焦点的工具窗 + 全透明不透明度：窗口在屏上被正常合成（字体与渲染路径真实），
    // 但用户看不到、不进任务栏；grabWindow() 走 FBO 离屏渲染，取到完整一帧。
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
                  | Qt::WindowDoesNotAcceptFocus);
    view.setColor(QColor(QStringLiteral("#0b0c10")));
    view.setOpacity(0.0);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.engine()->rootContext()->setContextProperty(QStringLiteral("updater"), &demo);
    view.engine()->rootContext()->setContextProperty(QStringLiteral("themeRegistry"), &demoTheme);
    view.engine()->rootContext()->setContextProperty(QStringLiteral("uiState"), &demoUi);
    view.engine()->rootContext()->setContextProperty(QStringLiteral("library"), &demoLibrary);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/SettingsPanel.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        finishWithCode(5);
        return;
    }
    view.resize(1440, 900);
    view.setPosition(0, 0);
    view.show();
    sleepMs(200);
    if (view.rootObject())
        view.rootObject()->setProperty("shotMode", true);

    const QStringList states = {QStringLiteral("unchecked"), QStringLiteral("uptodate"),
                                QStringLiteral("available"), QStringLiteral("downloading"),
                                QStringLiteral("downloaded"), QStringLiteral("error")};
    int saved = 0;
    for (const QString &state : states) {
        demo.setDemoState(state);
        sleepMs(220);
        const QImage image = view.grabWindow();
        const QString path = m_options.shotDir + QStringLiteral("/update-settings-%1.png").arg(state);
        if (!image.isNull() && image.save(path)) {
            ++saved;
            Log::info(QStringLiteral("已保存截图 %1").arg(path));
        } else {
            Log::error(QStringLiteral("截图失败：%1").arg(path));
        }
    }
    finishWithCode(saved == states.size() ? 0 : 6);
}

void AppController::finishWithCode(int code)
{
    // start() 在 app.exec() 之前被调用，此时直接 qApp->exit() 不会生效，
    // 事件循环起来后会一直挂着；用排队回调把退出码带到事件循环里再退出。
    QTimer::singleShot(0, qApp, [code]() { qApp->exit(code); });
}

void AppController::runUpdateCheckOnly()
{
    Log::info(QStringLiteral("update-check-only: 开始"));
    UpdateService *service = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    m_updater = service; // writeReport 依赖 m_updater
    connect(service, &UpdateService::finished, this, [this, service](const QString &state) {
        if (state == QLatin1String("checking"))
            return;
        QJsonObject report;
        report.insert(QStringLiteral("mode"), QStringLiteral("update-check-only"));
        report.insert(QStringLiteral("feed"), m_options.updateUrl);
        report.insert(QStringLiteral("state"), state);
        report.insert(QStringLiteral("errorCode"), service->errorCode());
        report.insert(QStringLiteral("errorMessage"), service->errorMessage());
        report.insert(QStringLiteral("currentVersion"), service->currentVersion());
        report.insert(QStringLiteral("availableVersion"), service->availableVersion());
        report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
        writeReport(report);
        finishWithCode(0);
    });
    QTimer::singleShot(30000, this, [this]() { finishWithCode(7); });
    service->checkForUpdates();
}

void AppController::runUpdateE2E()
{
    if (!m_options.restartMarker.isEmpty())
        qputenv("PALUNE_E2E_MARKER", m_options.restartMarker.toUtf8());
    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    connect(m_updater, &UpdateService::finished, this, [this](const QString &state) {
        QJsonObject transition;
        transition.insert(QStringLiteral("state"), state);
        transition.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        transition.insert(QStringLiteral("mirror"), m_updater->mirrorName());
        transition.insert(QStringLiteral("progress"), m_updater->progress());
        m_transitions.append(transition);

        if (state == QLatin1String("available")) {
            m_updater->selectMirror(1); // 使用第一个加速镜像，验证通道切换
            QTimer::singleShot(60, this, [this]() {
                if (m_options.e2eMode == QLatin1String("full"))
                    m_updater->startUpdate();
                else
                    m_updater->startDownloadOnly();
            });
        } else if (state == QLatin1String("downloaded")) {
            writeReport(QJsonObject());
            m_updater->installDownloaded();
        } else if (state == QLatin1String("installing")) {
            writeReport(QJsonObject());
        } else if (state == QLatin1String("error")) {
            writeReport(QJsonObject());
            finishWithCode(2);
        }
    });
    QTimer::singleShot(120000, this, [this]() { finishWithCode(3); });
    m_updater->checkForUpdates();
}

void AppController::runM1Smoke()
{
    const QStringList media = collectMedia();
    QJsonArray results;
    int passed = 0;
    m_lastPlaybackError.clear();
    qputenv("PALUNE_MPV_VO", "null");
    qputenv("PALUNE_MPV_AO", "null");

    MpvClient player;
    connect(&player, &MpvClient::errorOccurred, this, [this](const QString &message) {
        m_lastPlaybackError = message;
    });
    if (!player.initialize() || !player.setVideoWindow(0)) {
        QJsonObject report;
        report.insert(QStringLiteral("mode"), QStringLiteral("m1-smoke"));
        report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
        report.insert(QStringLiteral("error"), QStringLiteral("libmpv 初始化失败：%1").arg(player.loadError()));
        report.insert(QStringLiteral("samples"), media.size());
        report.insert(QStringLiteral("passed"), 0);
        writeJsonReport(report, m_options.reportPath.isEmpty() ? defaultReportPath(QStringLiteral("smoke"))
                                                              : m_options.reportPath);
        finishWithCode(2);
        return;
    }

    for (int i = 0; i < media.size(); ++i) {
        const QString file = media.at(i);
        m_lastPlaybackError.clear();
        const int baselineEnds = player.endFileCount();
        const int baselineStarts = player.startFileCount();
        QElapsedTimer timer;
        timer.start();
        bool ok = player.openFile(file);
        double reached = 0.0;
        QString error;
        if (!ok) {
            error = QStringLiteral("openFile 返回 false");
        } else {
            waitFor([&player]() { return player.position() >= 5.0 || player.eofReached(); }, 20000);
            reached = player.position();
            if (reached < 5.0) {
                error = player.eofReached()
                    ? QStringLiteral("播放提前结束，仅到 %1s").arg(reached, 0, 'f', 2)
                    : QStringLiteral("20s 内未到达 5s 播放位置（当前 %1s）").arg(reached, 0, 'f', 2);
            }
            if (!m_lastPlaybackError.isEmpty())
                error = m_lastPlaybackError;
            if (player.endFileCount() > baselineEnds && reached < 5.0 && error.isEmpty())
                error = QStringLiteral("播放结束事件早于 5s");
        }
        QJsonObject item;
        item.insert(QStringLiteral("file"), QFileInfo(file).fileName());
        item.insert(QStringLiteral("path"), file);
        item.insert(QStringLiteral("reachedSeconds"), reached);
        item.insert(QStringLiteral("elapsedMs"), double(timer.elapsed()));
        item.insert(QStringLiteral("startFileDelta"), player.startFileCount() - baselineStarts);
        item.insert(QStringLiteral("passed"), error.isEmpty());
        if (!error.isEmpty())
            item.insert(QStringLiteral("error"), error);
        results.append(item);
        if (error.isEmpty())
            ++passed;
        player.stop();
        sleepMs(120);
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m1-smoke"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("mpv"), player.backendDescription());
    report.insert(QStringLiteral("samples"), media.size());
    report.insert(QStringLiteral("passed"), passed);
    report.insert(QStringLiteral("threshold"), 19);
    report.insert(QStringLiteral("verdict"), passed >= 19 && media.size() >= 20
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("items"), results);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty() ? defaultReportPath(QStringLiteral("smoke"))
                                                          : m_options.reportPath);
    finishWithCode(passed >= 19 ? 0 : 2);
}

void AppController::runM1Perf()
{
    m_player = new MpvClient(this);
    connect(m_player, &MpvClient::errorOccurred, this, [this](const QString &message) {
        m_lastPlaybackError = message;
    });
    if (!m_player->initialize()) {
        Log::error(QStringLiteral("libmpv 初始化失败：%1").arg(m_player->loadError()));
        finishWithCode(2);
        return;
    }
    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    m_windows = new WindowController(m_player, m_updater, m_settings, m_library, this);
    if (!m_windows->start(1180, 700)) {
        finishWithCode(4);
        return;
    }
    m_windowReadyMs = static_cast<double>(StartupClock::elapsedMs());
    sleepMs(2000);
    const double idleRss = currentRssMb();

    const QString video = m_options.videoPath.isEmpty() ? collectMedia().value(0) : m_options.videoPath;
    double firstFrameMs = -1.0;
    double playingRss = -1.0;
    QString verdict = QStringLiteral("NOT_VERIFIED");
    double reached = 0.0;
    if (!video.isEmpty()) {
        const qint64 openStarted = StartupClock::elapsedMs();
        m_windows->openFile(QUrl::fromLocalFile(video));
        if (waitFor([this]() { return m_player->position() >= 1.0; }, 15000)) {
            firstFrameMs = static_cast<double>(StartupClock::elapsedMs() - openStarted);
            sleepMs(15000);
            playingRss = currentRssMb();
            reached = m_player->position();
            // 冷启动按“进程启动→主窗口可交互”计；首帧耗时单独记录，避免混为一谈。
            const bool pass = m_windowReadyMs >= 0 && m_windowReadyMs <= 800.0
                && idleRss >= 0 && idleRss <= 300.0
                && playingRss >= 0 && playingRss <= 500.0;
            verdict = pass ? QStringLiteral("PASS") : QStringLiteral("FAIL");
        }
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m1-perf"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("video"), video);
    report.insert(QStringLiteral("windowReadyMs"), m_windowReadyMs);
    report.insert(QStringLiteral("coldStartToWindowMs"), m_windowReadyMs);
    report.insert(QStringLiteral("firstFrameAfterOpenMs"), firstFrameMs);
    report.insert(QStringLiteral("idleRssMb"), idleRss);
    report.insert(QStringLiteral("playingRssMb"), playingRss);
    report.insert(QStringLiteral("reachedSeconds"), reached);
    report.insert(QStringLiteral("frameDrops"), double(m_player->frameDropCount()));
    report.insert(QStringLiteral("mistimedFrames"), double(m_player->mistimedFrameCount()));
    report.insert(QStringLiteral("verdict"), verdict);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty() ? defaultReportPath(QStringLiteral("perf"))
                                                          : m_options.reportPath);
    finishWithCode(verdict == QLatin1String("PASS") ? 0 : 2);
}

void AppController::runM1ThemeSwitch()
{
    const QString video = m_options.videoPath.isEmpty() ? collectMedia().value(0) : m_options.videoPath;
    if (video.isEmpty()) {
        Log::error(QStringLiteral("m1-theme-switch 需要 --video 或 --media"));
        finishWithCode(2);
        return;
    }
    m_player = new MpvClient(this);
    connect(m_player, &MpvClient::errorOccurred, this, [this](const QString &message) {
        m_lastPlaybackError = message;
    });
    if (!m_player->initialize()) {
        finishWithCode(2);
        return;
    }
    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    m_windows = new WindowController(m_player, m_updater, m_settings, m_library, this);
    if (!m_windows->start(1180, 700)) {
        finishWithCode(4);
        return;
    }
    m_windowReadyMs = static_cast<double>(StartupClock::elapsedMs());
    m_windows->openFile(QUrl::fromLocalFile(video));
    if (!waitFor([this]() { return m_player->position() >= 5.0; }, 20000)) {
        Log::error(QStringLiteral("视频 5s 内未进入播放状态"));
        finishWithCode(2);
        return;
    }

    ThemeRegistry *registry = m_windows->themeRegistry();
    LayoutHost *layoutHost = m_windows->layoutHost();
    const QStringList themes = ThemeRegistry::availableThemes();
    const int switchCount = qBound(1, m_options.switchCount, 30);
    QJsonArray switches;
    bool allSwitchFast = true;
    bool allLumaOk = true;
    bool allContinuous = true;
    double maxSwitchMs = 0.0;
    double maxAudioGapMs = 0.0;

    for (int i = 0; i < switchCount; ++i) {
        const QString theme = themes.at(i % themes.size());
        const double posBefore = m_player->position();
        const double audioBefore = m_player->audioPts();
        const int startBefore = m_player->startFileCount();
        const int endBefore = m_player->endFileCount();
        const double rssBefore = currentRssMb();

        QElapsedTimer timer;
        timer.start();
        m_windows->action(QStringLiteral("setTheme"), theme);
        const double totalSwitchMs = timer.nsecsElapsed() / 1.0e6;
        const double registryMs = registry->lastSwitchMs();
        const double layoutMs = layoutHost->lastBuildMs();

        double lumaImmediate = -1.0;
        double lumaStable = -1.0;
        QElapsedTimer gapTimer;
        gapTimer.start();
        m_player->sampleVideoLuminance(&lumaImmediate, nullptr, nullptr, nullptr);
        sleepMs(120);
        m_player->sampleVideoLuminance(&lumaStable, nullptr, nullptr, nullptr);
        sleepMs(300);
        const double posAfter = m_player->position();
        const double audioAfter = m_player->audioPts();
        const double wallMs = gapTimer.nsecsElapsed() / 1.0e6;
        const double audioGapMs = (audioAfter > audioBefore)
            ? qMax(0.0, wallMs - (audioAfter - audioBefore) * 1000.0)
            : wallMs;
        maxAudioGapMs = qMax(maxAudioGapMs, audioGapMs);

        const bool fast = totalSwitchMs <= 300.0 && layoutMs <= 300.0;
        const bool lumaOk = lumaImmediate >= 3.0 && lumaStable >= 3.0;
        const bool continuous = (m_player->startFileCount() == startBefore)
            && (m_player->endFileCount() == endBefore)
            && (posAfter + 0.05 >= posBefore);
        allSwitchFast = allSwitchFast && fast;
        allLumaOk = allLumaOk && lumaOk;
        allContinuous = allContinuous && continuous;
        maxSwitchMs = qMax(maxSwitchMs, qMax(totalSwitchMs, layoutMs));

        QJsonObject item;
        item.insert(QStringLiteral("index"), i + 1);
        item.insert(QStringLiteral("theme"), theme);
        item.insert(QStringLiteral("switchMs"), totalSwitchMs);
        item.insert(QStringLiteral("registryMs"), registryMs);
        item.insert(QStringLiteral("layoutBuildMs"), layoutMs);
        item.insert(QStringLiteral("lumaImmediate"), lumaImmediate);
        item.insert(QStringLiteral("lumaAfter120ms"), lumaStable);
        item.insert(QStringLiteral("positionBefore"), posBefore);
        item.insert(QStringLiteral("positionAfter"), posAfter);
        item.insert(QStringLiteral("audioGapMs"), audioGapMs);
        item.insert(QStringLiteral("startFileDelta"), m_player->startFileCount() - startBefore);
        item.insert(QStringLiteral("endFileDelta"), m_player->endFileCount() - endBefore);
        item.insert(QStringLiteral("rssBeforeMb"), rssBefore);
        item.insert(QStringLiteral("rssAfterMb"), currentRssMb());
        switches.append(item);
        CrashBreadcrumb::record(QStringLiteral("主题切换 #%1 %2（%3ms）")
                                    .arg(i + 1).arg(theme).arg(totalSwitchMs, 0, 'f', 1));
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m1-theme-switch"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("video"), video);
    report.insert(QStringLiteral("switches"), switchCount);
    report.insert(QStringLiteral("maxSwitchMs"), maxSwitchMs);
    report.insert(QStringLiteral("maxAudioGapMs"), maxAudioGapMs);
    report.insert(QStringLiteral("allSwitchWithin300ms"), allSwitchFast);
    report.insert(QStringLiteral("allFramesNonBlack"), allLumaOk);
    report.insert(QStringLiteral("playbackContinuous"), allContinuous);
    report.insert(QStringLiteral("layoutContainsVideoHost"), layoutHost->layoutContainsVideoHost());
    report.insert(QStringLiteral("layoutBuildCount"), layoutHost->buildCount());
    report.insert(QStringLiteral("windowReadyMs"), m_windowReadyMs);
    const bool pass = allSwitchFast && allLumaOk && allContinuous
        && !layoutHost->layoutContainsVideoHost();
    report.insert(QStringLiteral("verdict"),
                  pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("items"), switches);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty() ? defaultReportPath(QStringLiteral("theme-switch"))
                                                          : m_options.reportPath);
    finishWithCode(pass ? 0 : 2);
}

QStringList AppController::collectMedia() const
{
    QStringList files;
    if (m_options.mediaPath.isEmpty())
        return files;
    const QFileInfo info(m_options.mediaPath);
    if (info.isDir()) {
        const QFileInfoList entries = QDir(info.absoluteFilePath()).entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (isMediaFile(entry))
                files << entry.absoluteFilePath();
        }
    } else if (info.isFile()) {
        files << info.absoluteFilePath();
    }
    return files;
}

bool AppController::waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

void AppController::sleepMs(int ms)
{
    waitFor([]() { return false; }, ms);
}

void AppController::writeJsonReport(const QJsonObject &report, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Log::error(QStringLiteral("无法写入报告：%1").arg(path));
        return;
    }
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    Log::info(QStringLiteral("报告已写入：%1").arg(path));
}

void AppController::writeReport(const QJsonObject &extra)
{
    QJsonObject report = extra;
    if (!report.contains(QStringLiteral("mode")))
        report.insert(QStringLiteral("mode"), QStringLiteral("update-e2e:%1").arg(m_options.e2eMode));
    report.insert(QStringLiteral("feed"), m_options.updateUrl);
    report.insert(QStringLiteral("downloadDir"), m_options.downloadDir);
    report.insert(QStringLiteral("currentVersion"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("availableVersion"), m_updater->availableVersion());
    report.insert(QStringLiteral("state"), m_updater->state());
    report.insert(QStringLiteral("mirror"), m_updater->mirrorName());
    report.insert(QStringLiteral("mirrorIndex"), m_updater->mirrorIndex());
    report.insert(QStringLiteral("downloadedPath"), m_updater->downloadedPath());
    report.insert(QStringLiteral("expectedSha256"), m_updater->expectedSha256());
    report.insert(QStringLiteral("actualSha256"), hashFile(m_updater->downloadedPath()));
    report.insert(QStringLiteral("errorCode"), m_updater->errorCode());
    report.insert(QStringLiteral("errorMessage"), m_updater->errorMessage());
    report.insert(QStringLiteral("transitions"), m_transitions);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    report.insert(QStringLiteral("logPath"), Log::filePath());
    report.insert(QStringLiteral("restartMarker"), m_options.restartMarker);
    QDir().mkpath(QFileInfo(m_options.reportPath).absolutePath());
    QFile f(m_options.reportPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    else
        Log::error(QStringLiteral("无法写入 e2e 报告：%1").arg(m_options.reportPath));
}

QString AppController::hashFile(const QString &path)
{
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return QString::fromLatin1(hash.result().toHex());
}

void AppController::writeRestartMarker()
{
    if (m_options.restartMarker.isEmpty())
        return;
    QJsonObject marker;
    marker.insert(QStringLiteral("restartedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    marker.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    marker.insert(QStringLiteral("pid"), double(QCoreApplication::applicationPid()));
    marker.insert(QStringLiteral("applicationPath"), QCoreApplication::applicationFilePath());
    marker.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(QCoreApplication::arguments().mid(1)));
    QDir().mkpath(QFileInfo(m_options.restartMarker).absolutePath());
    QFile f(m_options.restartMarker);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(marker).toJson(QJsonDocument::Indented));
    Log::info(QStringLiteral("已写入重启标记：%1").arg(m_options.restartMarker));
}

} // namespace palune
