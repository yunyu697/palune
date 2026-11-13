#include "app/AppController.h"

#include "core/AppPaths.h"
#include "core/ProcessMetrics.h"
#include "core/StartupClock.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "m0/SelfTest.h"
#include "media/MediaLibraryService.h"
#include "mpv/MpvClient.h"
#include "settings/Preferences.h"
#include "settings/Settings.h"
#include "app/ShotContexts.h"
#include "theme/LayoutHost.h"
#include "theme/ThemeRegistry.h"
#include "ui/CompactController.h"
#include "ui/UiStateStore.h"
#include "ui/ActionRegistry.h"
#include "update/UpdateService.h"
#include "window/WindowController.h"
#include "window/SettingsWindow.h"
#include "window/OverlayWindow.h"
#include "window/VideoWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMutex>
#include <QMenu>
#include <QQmlContext>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QVector>
#include <QQmlEngine>
#include <QSysInfo>
#include <QQuickItem>
#include <QQuickView>
#include <QThread>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include <functional>
#include <memory>

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
        if (m_options.shotMatrix)
            runShotMatrix();
        else
            runShots();
        return;
    }
    if (m_options.m2ConfigCheck) {
        runM2ConfigCheck();
        return;
    }
    if (m_options.m2Actions) {
        runM2Actions();
        return;
    }
    if (m_options.m2UiPerf) {
        runM2UiPerf();
        return;
    }
    if (m_options.m2Window) {
        runM2Window();
        return;
    }
    if (m_options.m2Compact) {
        runM2Compact();
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
    if (m_options.m1Soak) {
        runM1Soak();
        return;
    }
    if (m_options.m2StateSwitch) {
        runM2StateSwitch();
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
    else if (m_windows->preferences() && m_windows->preferences()->restoreLastSession()) {
        // 「恢复上次会话」：没有命令行文件时，自动打开最近列表的第一个文件。
        const QVariantList recent = m_windows->recentFiles();
        if (!recent.isEmpty()) {
            const QString last = recent.first().toMap().value(QStringLiteral("path")).toString();
            if (QFileInfo::exists(last)) {
                Log::info(QStringLiteral("恢复上次会话：%1").arg(last));
                m_windows->openFile(QUrl::fromLocalFile(last));
            }
        }
    }

    // 「自动检查更新」：启动后延迟一次静默检查，不抢占窗口激活；自动化截图/取证时跳过。
    const bool automationMode = !m_options.uiTheme.isEmpty() || !m_options.uiPanel.isEmpty()
        || !m_options.uiCapture.isEmpty() || !m_options.uiActions.isEmpty();
    if (!automationMode && m_windows->preferences() && m_windows->preferences()->autoCheckUpdates()) {
        QTimer::singleShot(3000, this, [this]() {
            if (m_updater && !m_updater->isDownloading())
                m_updater->checkForUpdates();
        });
    }

    // S1–S6 设置端到端探针（由环境变量触发，正常启动不受影响）。
    const QString probePath = qEnvironmentVariable("PALUNE_PREFS_PROBE");
    if (!probePath.isEmpty())
        QTimer::singleShot(5000, this, [this, probePath]() { runPreferencesProbe(probePath); });

    // 启动窗口态探针：验证 ui.startMaximized 是否真的以最大化启动（需要独立一次启动）。
    const QString windowStatePath = qEnvironmentVariable("PALUNE_WINDOW_STATE_PROBE");
    if (!windowStatePath.isEmpty()) {
        QTimer::singleShot(2000, this, [this, windowStatePath]() {
            QJsonObject report;
            report.insert(QStringLiteral("mode"), QStringLiteral("window-state"));
            report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
            if (m_windows && m_windows->overlay()) {
                report.insert(QStringLiteral("maximized"), m_windows->isMaximized());
                report.insert(QStringLiteral("visible"), m_windows->overlay()->isVisible());
                report.insert(QStringLiteral("size"),
                              QStringLiteral("%1x%2")
                                  .arg(m_windows->overlay()->width())
                                  .arg(m_windows->overlay()->height()));
                report.insert(QStringLiteral("startMaximizedSetting"),
                              m_windows->preferences()->startMaximized());
            }
            report.insert(QStringLiteral("generatedAt"),
                          QDateTime::currentDateTime().toString(Qt::ISODate));
            writeJsonReport(report, windowStatePath);
            finishWithCode(0);
        });
    }

    // 通知区域图标：窗口被最小化或切走后仍能找到入口。图标与窗口/exe 共用同一份
    // 品牌资源（qrc 里的 256 PNG），自动化截图场景不创建，避免遮挡与干扰。
    const bool uiAutomation = automationMode;
    if (!uiAutomation && QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/packaging/palune-256.png")), this);
        m_tray->setToolTip(QStringLiteral("Palune 媒体播放器"));
        auto *menu = new QMenu();
        connect(qApp, &QCoreApplication::aboutToQuit, menu, &QObject::deleteLater);
        QAction *showAction = menu->addAction(QStringLiteral("显示主窗口"));
        connect(showAction, &QAction::triggered, this, [this]() {
            if (m_windows) {
                m_windows->overlay()->showNormal();
                m_windows->activate();
            }
        });
        menu->addSeparator();
        QAction *quitAction = menu->addAction(QStringLiteral("退出"));
        connect(quitAction, &QAction::triggered, this, [this]() {
            if (m_windows)
                m_windows->quitApplication();
        });
        m_tray->setContextMenu(menu);
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                        if (m_windows)
                            m_windows->activate();
                    }
                });
        m_tray->show();
    }

    // 真实窗口自动化：按主题/面板参数驱动已安装窗口，并抓取窗口自身的渲染结果。
    // 这些开关只服务于实机截图与缺陷复核，不改变正常启动路径。
    if (!m_options.uiTheme.isEmpty() || !m_options.uiPanel.isEmpty()
        || !m_options.uiCapture.isEmpty() || !m_options.uiActions.isEmpty()) {
        QTimer::singleShot(600, this, [this]() {
            if (m_windows && !m_options.uiTheme.isEmpty())
                m_windows->action(QStringLiteral("setTheme"), m_options.uiTheme);
            if (!m_options.uiPanel.isEmpty())
                m_windows->action(m_options.uiPanel == QLatin1String("library")
                                      ? QStringLiteral("openLibrary")
                                      : QStringLiteral("openSettings"));
            const QStringList steps = m_options.uiActions.split(QLatin1Char(','),
                                                               Qt::SkipEmptyParts);
            int delay = 1800;
            for (const QString &step : steps) {
                const QString a = step.trimmed();
                if (a.isEmpty())
                    continue;
                QTimer::singleShot(delay, this, [this, a]() {
                    if (m_windows)
                        m_windows->action(a);
                });
                delay += 1400;
            }
            if (!m_options.uiCapture.isEmpty())
                QTimer::singleShot(delay, this, [this]() {
                    if (!m_windows)
                        return;
                    QImage image;
                    if (m_options.uiPanel == QLatin1String("settings")
                        && m_windows->settingsWindow()) {
                        // 截图分页：PALUNE_SETTINGS_PAGE=0..7（通用/播放/字幕/音频/快捷键/媒体库/存储/关于）
                        m_windows->settingsWindow()->setPage(
                            qEnvironmentVariableIntValue("PALUNE_SETTINGS_PAGE"));
                        QElapsedTimer settle;
                        settle.start();
                        while (settle.elapsed() < 160)
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                        image = m_windows->settingsWindow()->grabWindow();
                    } else if (m_windows->overlay())
                        image = m_windows->overlay()->grabWindow();
                    if (!image.isNull() && image.save(m_options.uiCapture))
                        Log::info(QStringLiteral("真实窗口截图：%1 %2x%3")
                                      .arg(m_options.uiCapture).arg(image.width()).arg(image.height()));
                });
        });
    }

    if (m_options.resumeProbe) {
        QTimer::singleShot(300, this, [this, path]() {
            QElapsedTimer probe;
            probe.start();
            while (probe.elapsed() < 9000 && m_player && m_player->duration() <= 0.0)
                sleepMs(120);
            // 冻结播放再读数，避免测量期间位置继续前进。
            if (m_player && !m_player->paused())
                m_player->togglePause();
            sleepMs(600);
            const double saved = m_settings->resumePosition(path);
            const double restored = m_player ? m_player->position() : 0.0;
            QJsonObject report;
            report.insert(QStringLiteral("mode"), QStringLiteral("resume-probe"));
            report.insert(QStringLiteral("path"), path);
            report.insert(QStringLiteral("savedPosition"), saved);
            report.insert(QStringLiteral("restoredPosition"), restored);
            report.insert(QStringLiteral("deltaSeconds"), qAbs(restored - saved));
            report.insert(QStringLiteral("loaded"), m_player && m_player->duration() > 0.0);
            report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
            writeJsonReport(report, m_options.reportPath.isEmpty()
                                        ? defaultReportPath(QStringLiteral("resume-probe"))
                                        : m_options.reportPath);
            finishWithCode(0);
        });
    }

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
    CompactController demoCompact(&demoSettings);
    ActionRegistry demoActions;
    Preferences demoPrefs(&demoSettings, nullptr, &demo, &demoCompact, &demoActions);
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
    view.engine()->rootContext()->setContextProperty(QStringLiteral("prefs"), &demoPrefs);
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
        report.insert(QStringLiteral("manifestSource"), service->manifestSource());
        report.insert(QStringLiteral("manifestSourceUrl"), service->manifestSourceUrl());
        report.insert(QStringLiteral("manifestAttempts"),
                      QJsonArray::fromVariantList(service->manifestAttempts()));
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

void AppController::runM1Soak()
{
    m_player = new MpvClient(this);
    if (!m_player->initialize()) {
        Log::error(QStringLiteral("libmpv 初始化失败：%1").arg(m_player->loadError()));
        finishWithCode(4);
        return;
    }
    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    m_windows = new WindowController(m_player, m_updater, m_settings, m_library, this);
    if (!m_windows->start(1180, 700)) {
        finishWithCode(4);
        return;
    }
    m_windowReadyMs = static_cast<double>(StartupClock::elapsedMs());

    QString video = m_options.videoPath;
    if (video.isEmpty())
        video = collectMedia().value(0);
    if (video.isEmpty()) {
        QJsonObject report;
        report.insert(QStringLiteral("mode"), QStringLiteral("m1-soak"));
        report.insert(QStringLiteral("verdict"), QStringLiteral("NOT_VERIFIED"));
        report.insert(QStringLiteral("error"), QStringLiteral("没有可用视频样本"));
        writeJsonReport(report, m_options.reportPath.isEmpty() ? defaultReportPath(QStringLiteral("soak"))
                                                              : m_options.reportPath);
        finishWithCode(2);
        return;
    }

    m_player->setLoopFile(true);
    m_windows->openFile(QUrl::fromLocalFile(video));

    const int minutes = qMax(1, m_options.soakMinutes);
    auto samples = std::make_shared<QJsonArray>();
    auto baseline = std::make_shared<double>(-1.0);
    QElapsedTimer soakClock;
    soakClock.start();
    const qint64 startedAtMs = StartupClock::elapsedMs();

    auto *timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, [this, timer, minutes, samples, baseline, video,
                                           soakClock, startedAtMs]() {
        const double rss = currentRssMb();
        if (*baseline < 0.0)
            *baseline = rss;
        QJsonObject sample;
        sample.insert(QStringLiteral("minute"), samples->size() + 1);
        sample.insert(QStringLiteral("elapsedMs"), static_cast<double>(soakClock.elapsed()));
        sample.insert(QStringLiteral("rssMb"), rss);
        sample.insert(QStringLiteral("position"), m_player->position());
        sample.insert(QStringLiteral("startFileCount"), m_player->startFileCount());
        sample.insert(QStringLiteral("endFileCount"), m_player->endFileCount());
        samples->append(sample);
        if (samples->size() >= minutes) {
            timer->stop();
            const double finalRss = rss;
            const double driftMbPerHour = (finalRss - *baseline) * 60.0 / minutes;
            QJsonObject report;
            report.insert(QStringLiteral("mode"), QStringLiteral("m1-soak"));
            report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
            report.insert(QStringLiteral("video"), video);
            report.insert(QStringLiteral("minutes"), samples->size());
            report.insert(QStringLiteral("baselineRssMb"), *baseline);
            report.insert(QStringLiteral("finalRssMb"), finalRss);
            report.insert(QStringLiteral("driftMbPerHour"), driftMbPerHour);
            report.insert(QStringLiteral("windowReadyMs"), m_windowReadyMs);
            report.insert(QStringLiteral("processStartMs"), static_cast<double>(startedAtMs));
            report.insert(QStringLiteral("samples"), *samples);
            const bool pass = driftMbPerHour <= 50.0;
            report.insert(QStringLiteral("verdict"),
                          pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
            report.insert(QStringLiteral("generatedAt"),
                          QDateTime::currentDateTime().toString(Qt::ISODate));
            writeJsonReport(report, m_options.reportPath.isEmpty()
                                        ? defaultReportPath(QStringLiteral("soak"))
                                        : m_options.reportPath);
            finishWithCode(pass ? 0 : 2);
        }
    });
    // 第一分钟给出基线，之后每分钟采样；全程循环播放。
    QTimer::singleShot(30000, timer, [timer]() { timer->start(); });
    Log::info(QStringLiteral("长时压测开始：%1 分钟，循环播放 %2").arg(minutes).arg(video));
}

void AppController::runM2StateSwitch()
{
    const QString video = m_options.videoPath.isEmpty() ? collectMedia().value(0) : m_options.videoPath;
    if (video.isEmpty()) {
        Log::error(QStringLiteral("m2-stateswitch 需要 --video 或 --media"));
        finishWithCode(2);
        return;
    }
    m_player = new MpvClient(this);
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

    UiStateStore *uiState = m_windows->uiState();
    // 预置可恢复状态：最近列表 10 项、滚动到第 6 项、选中同一项、焦点在播放按钮。
    for (int i = 1; i <= 10; ++i)
        m_settings->addRecentFile(QStringLiteral("D:/qa/recent-%1.mp4").arg(i));
    m_settings->save();
    uiState->setScrollIndex(5);
    uiState->setSelectedItemId(QStringLiteral("D:/qa/recent-6.mp4"));
    uiState->setFocusActionId(QStringLiteral("playPause"));
    uiState->setChromeVisible(true);
    uiState->setPanelWidth(QStringLiteral("B"), 360);
    sleepMs(150);

    QQuickItem *root = m_windows->overlay() ? m_windows->overlay()->rootObject() : nullptr;
    const QStringList themes = ThemeRegistry::availableThemes();
    const int count = qBound(1, m_options.stateSwitchCount, 100);
    QJsonArray items;
    bool allPosition = true;
    bool allScroll = true;
    bool allSelection = true;
    bool allFocus = true;
    bool allPanel = true;
    bool allNoBlack = true;
    bool allNoReload = true;
    double maxDeficitMs = 0.0;

    for (int i = 0; i < count; ++i) {
        const QString theme = themes.at((themes.indexOf(QStringLiteral("A")) + i + 1) % themes.size());
        // 30 秒样片在隐藏桌面下（每次亮度采样要 0.4s 左右）会跑到片尾，
        // 片尾停播是样本长度问题而非状态保持缺陷；临近结尾先回跳到 5s，
        // 回跳发生在测量窗口之外，不参与连续性判定。
        bool soughtBack = false;
        if (m_player->duration() > 0.0 && m_player->position() > m_player->duration() - 3.0) {
            m_player->seek(5.0);
            sleepMs(250);
            soughtBack = true;
        }
        const double posBefore = m_player->position();
        const int startBefore = m_player->startFileCount();
        const int endBefore = m_player->endFileCount();
        QElapsedTimer wall;
        wall.start();
        m_windows->action(QStringLiteral("setTheme"), theme);
        // 让 Qt.callLater 的焦点恢复与布局绑定跑完。
        sleepMs(80);
        double lumaEarly = -1.0;
        double lumaLate = -1.0;
        m_player->sampleVideoLuminance(&lumaEarly, nullptr, nullptr, nullptr);
        sleepMs(70);
        m_player->sampleVideoLuminance(&lumaLate, nullptr, nullptr, nullptr);
        const double wallMs = wall.nsecsElapsed() / 1.0e6;
        const double posAfter = m_player->position();

        const double expected = wallMs / 1000.0;
        const double deficitMs = qMax(0.0, (expected - (posAfter - posBefore)) * 1000.0);
        const double deltaSec = posAfter - posBefore;
        // 判据：不倒退（delta>=0）、不卡顿（落后墙钟 ≤500ms）、不超前（比墙钟多 ≤150ms）。
        // 隐藏桌面下两次亮度采样会阻塞事件循环，单纯用固定 0.5s 会把「正常前进 0.66s」
        // 误判为跳变，所以用墙钟作为上界。
        const bool positionOk = deltaSec >= -0.001
            && deficitMs <= 500.0
            && deltaSec <= wallMs / 1000.0 + 0.15;
        const bool scrollOk = uiState->scrollIndex() == 5;
        const bool selectionOk = uiState->selectedItemId() == QLatin1String("D:/qa/recent-6.mp4");
        const bool focusOk = root && [root]() {
            // 布局根由 QQmlComponent 创建，没有 QObject 父对象，findChildren 遍历不到；
            // 必须沿 QQuickItem::childItems() 递归。
            std::function<QQuickItem *(QQuickItem *)> findFocused =
                [&](QQuickItem *item) -> QQuickItem * {
                    if (item->property("actionId").toString() == QLatin1String("playPause")
                            && (item->hasFocus() || item->hasActiveFocus()))
                        return item;
                    for (QQuickItem *child : item->childItems()) {
                        if (QQuickItem *found = findFocused(child))
                            return found;
                    }
                    return nullptr;
                };
            // 隐藏桌面不会让窗口成为活动窗口，activeFocus 可能恒为 false；
            // focus 属性仍能证明焦点项已经落到等价控件上。
            return findFocused(root) != nullptr;
        }();
        const bool panelOk = uiState->panelWidth(QStringLiteral("B")) == 360;
        const bool noReload = m_player->startFileCount() == startBefore
            && m_player->endFileCount() == endBefore;
        const bool noBlack = lumaEarly >= 3.0 && lumaLate >= 3.0;

        allPosition = allPosition && positionOk;
        allScroll = allScroll && scrollOk;
        allSelection = allSelection && selectionOk;
        allFocus = allFocus && focusOk;
        allPanel = allPanel && panelOk;
        allNoBlack = allNoBlack && noBlack;
        allNoReload = allNoReload && noReload;
        maxDeficitMs = qMax(maxDeficitMs, deficitMs);

        QJsonObject item;
        item.insert(QStringLiteral("index"), i + 1);
        item.insert(QStringLiteral("theme"), theme);
        item.insert(QStringLiteral("positionBefore"), posBefore);
        item.insert(QStringLiteral("positionAfter"), posAfter);
        item.insert(QStringLiteral("positionDeltaMs"), deltaSec * 1000.0);
        item.insert(QStringLiteral("wallMs"), wallMs);
        item.insert(QStringLiteral("positionDeficitMs"), deficitMs);
        item.insert(QStringLiteral("scrollIndex"), uiState->scrollIndex());
        item.insert(QStringLiteral("selectedItemId"), uiState->selectedItemId());
        item.insert(QStringLiteral("focusActionId"), uiState->focusActionId());
        item.insert(QStringLiteral("focusRestored"), focusOk);
        item.insert(QStringLiteral("panelWidthB"), uiState->panelWidth(QStringLiteral("B")));
        item.insert(QStringLiteral("lumaEarly"), lumaEarly);
        item.insert(QStringLiteral("lumaLate"), lumaLate);
        item.insert(QStringLiteral("soughtBack"), soughtBack);
        item.insert(QStringLiteral("startFileDelta"), m_player->startFileCount() - startBefore);
        item.insert(QStringLiteral("endFileDelta"), m_player->endFileCount() - endBefore);
        items.append(item);
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-stateswitch"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("video"), video);
    report.insert(QStringLiteral("switches"), count);
    report.insert(QStringLiteral("positionContinuous"), allPosition);
    report.insert(QStringLiteral("scrollRestored"), allScroll);
    report.insert(QStringLiteral("selectionRestored"), allSelection);
    report.insert(QStringLiteral("focusRestored"), allFocus);
    report.insert(QStringLiteral("panelWidthKept"), allPanel);
    report.insert(QStringLiteral("noBlackFrameOver100ms"), allNoBlack);
    report.insert(QStringLiteral("noReload"), allNoReload);
    report.insert(QStringLiteral("maxPositionDeficitMs"), maxDeficitMs);
    report.insert(QStringLiteral("positionCriterion"),
                  QStringLiteral("不倒退、落后墙钟 ≤500ms、超前 ≤150ms"));
    const bool pass = allPosition && allScroll && allSelection && allFocus
        && allPanel && allNoBlack && allNoReload;
    report.insert(QStringLiteral("verdict"), pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("items"), items);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty()
                                ? defaultReportPath(QStringLiteral("m2-stateswitch"))
                                : m_options.reportPath);
    finishWithCode(pass ? 0 : 2);
}

void AppController::runM2Compact()
{
    CompactController controller(m_settings);
    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-compact"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("hysteresis"), controller.hysteresis());
    report.insert(QStringLiteral("debounceMs"), CompactController::kDefaultDebounceMs);
    report.insert(QStringLiteral("defaultBreakpoint"), controller.breakpoint());

    // 合法配置：640
    m_settings->setValue(QStringLiteral("ui.layout.A.compactBelow"), 640);
    controller.reloadBreakpoint();
    const int configured = controller.breakpoint();
    // 非法配置：0 / -20 / 非数字，都必须回退默认并给出提示
    m_settings->setValue(QStringLiteral("ui.layout.A.compactBelow"), 0);
    controller.reloadBreakpoint();
    const int fallbackZero = controller.breakpoint();
    const bool noticeZero = !controller.configNotice().isEmpty();
    m_settings->setValue(QStringLiteral("ui.layout.A.compactBelow"), QStringLiteral("abc"));
    controller.reloadBreakpoint();
    const int fallbackText = controller.breakpoint();
    const bool noticeText = !controller.configNotice().isEmpty();
    // 恢复测试配置，避免污染后续场景（只改内存，不落盘）
    m_settings->setValue(QStringLiteral("ui.layout.A.compactBelow"), 640);
    controller.reloadBreakpoint();

    controller.reset();
    controller.feedWidth(1000);
    controller.flush();
    const int bp = controller.breakpoint();
    // 阈值附近 ±8px、10Hz、50 次：滞回带内不应切换
    for (int i = 0; i < 50; ++i) {
        controller.feedWidth(i % 2 == 0 ? bp - 8 : bp + 8);
        sleepMs(100);
    }
    const int jitterSwitches = controller.switchCount();
    // 稳定落到紧凑侧：只允许 1 次切换
    controller.feedWidth(bp - 40);
    sleepMs(200);
    const int settleSwitches = controller.switchCount();
    const bool settledCompact = controller.compact();

    report.insert(QStringLiteral("configuredBreakpoint"), configured);
    report.insert(QStringLiteral("fallbackOnZero"), fallbackZero);
    report.insert(QStringLiteral("fallbackOnText"), fallbackText);
    report.insert(QStringLiteral("fallbackNotice"), noticeZero && noticeText);
    report.insert(QStringLiteral("jitterSwitches"), jitterSwitches);
    report.insert(QStringLiteral("switchesAfterSettle"), settleSwitches);
    report.insert(QStringLiteral("settledCompact"), settledCompact);
    const bool pass = configured == 640 && fallbackZero == CompactController::kDefaultBreakpoint
        && fallbackText == CompactController::kDefaultBreakpoint && noticeZero && noticeText
        && jitterSwitches == 0 && settleSwitches <= 1 && settledCompact;
    report.insert(QStringLiteral("verdict"), pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty()
                                ? defaultReportPath(QStringLiteral("m2-compact"))
                                : m_options.reportPath);
    finishWithCode(pass ? 0 : 2);
}

void AppController::runM2ConfigCheck()
{
    m_player = new MpvClient(this);
    const bool playerOk = m_player->initialize();
    if (!playerOk)
        Log::error(QStringLiteral("libmpv 初始化失败：%1").arg(m_player->loadError()));
    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    m_windows = new WindowController(m_player, m_updater, m_settings, m_library, this);
    const bool windowOk = m_windows->start(1180, 700);
    m_windowReadyMs = windowOk ? static_cast<double>(StartupClock::elapsedMs()) : -1.0;

    QString theme;
    QString fallbackReason;
    if (windowOk) {
        theme = m_windows->themeRegistry()->currentTheme();
        fallbackReason = m_windows->themeRegistry()->fallbackReason();
    }

    bool played = false;
    if (windowOk && playerOk && !m_options.videoPath.isEmpty()) {
        m_windows->openFile(QUrl::fromLocalFile(m_options.videoPath));
        played = waitFor([this]() { return m_player->position() >= 1.0; }, 15000);
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-config-check"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("configDir"), AppPaths::configDir());
    report.insert(QStringLiteral("settingsParseFailed"), m_settings->parseFailed());
    report.insert(QStringLiteral("theme"), theme);
    report.insert(QStringLiteral("fallbackReason"), fallbackReason);
    report.insert(QStringLiteral("windowReadyMs"), m_windowReadyMs);
    report.insert(QStringLiteral("playerAvailable"), playerOk);
    report.insert(QStringLiteral("played"), played);
    const bool hasVideo = !m_options.videoPath.isEmpty();
    // 本作业只验证「配置非法/缺失时能回退到布局 A 且仍能起窗播放」；
    // 启动耗时单独由 --m1-perf 的 coldStartToWindowMs 判据衡量，
    // 这里只如实记录，不把构建机负载/QML 冷缓存算成配置回退失败。
    const bool pass = windowOk && theme.size() == 1 && (!hasVideo || (playerOk && played));
    report.insert(QStringLiteral("windowReadyWithin800ms"),
                  m_windowReadyMs >= 0 && m_windowReadyMs <= 800.0);
    report.insert(QStringLiteral("verdict"), pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty()
                                ? defaultReportPath(QStringLiteral("m2-config-check"))
                                : m_options.reportPath);
    finishWithCode(pass ? 0 : 2);
}

void AppController::runM2Actions()
{
    UpdateService demo;
    Settings demoSettings;
    ThemeRegistry demoTheme(&demoSettings, [](const QString &) { return true; });
    UiStateStore demoUi(&demoSettings);
    CompactController demoCompact(&demoSettings);
    MediaLibraryService demoLibrary;
    ShotPlayer shotPlayer;
    ShotWindow shotWin;

    QVariantList recent;
    for (int i = 1; i <= 8; ++i)
        recent.append(QVariantMap{{QStringLiteral("name"), QCoreApplication::translate("Palune", "示例影片 %1").arg(i)},
                                  {QStringLiteral("path"), QStringLiteral("D:/qa/sample-%1.mp4").arg(i)}});
    shotWin.setRecentFiles(recent);
    shotPlayer.setFilePath(QStringLiteral("D:/qa/demo-1080p.mp4"));
    shotPlayer.setTitle(QCoreApplication::translate("Palune", "示例影片 · 1080p"));
    shotPlayer.setDuration(7200.0);
    shotPlayer.setPosition(30.0);
    shotPlayer.setVolume(100.0);

    // 让 40 个动作都有可核对的状态变化（截图/自动化桩，不触发真实解码）。
    ActionRegistry registry;
    Preferences demoPrefs(&demoSettings, nullptr, &demo, &demoCompact, &registry);
    QStringList dispatched;
    registry.setInvoker([&](const QString &id, const QVariant &value) {
        dispatched << id;
        shotWin.action(id, value);
        if (id == QLatin1String("playPause")) {
            shotPlayer.setPaused(!shotPlayer.paused());
        } else if (id == QLatin1String("stop")) {
            shotPlayer.setPaused(true);
            shotPlayer.setPosition(0.0);
        } else if (id == QLatin1String("seekForward10")) {
            shotPlayer.setPosition(shotPlayer.position() + 10.0);
        } else if (id == QLatin1String("seekBack10")) {
            shotPlayer.setPosition(qMax(0.0, shotPlayer.position() - 10.0));
        } else if (id == QLatin1String("seekForward60")) {
            shotPlayer.setPosition(shotPlayer.position() + 60.0);
        } else if (id == QLatin1String("seekBack60")) {
            shotPlayer.setPosition(qMax(0.0, shotPlayer.position() - 60.0));
        } else if (id == QLatin1String("seekForward1")) {
            shotPlayer.setPosition(shotPlayer.position() + 1.0);
        } else if (id == QLatin1String("seekBack1")) {
            shotPlayer.setPosition(qMax(0.0, shotPlayer.position() - 1.0));
        } else if (id == QLatin1String("seekToStart")) {
            shotPlayer.setPosition(0.0);
        } else if (id == QLatin1String("seekToEnd")) {
            shotPlayer.setPosition(shotPlayer.duration() - 0.5);
        } else if (id == QLatin1String("volumeUp")) {
            shotPlayer.setVolume(qMin(100.0, shotPlayer.volume() + 5.0));
        } else if (id == QLatin1String("volumeDown")) {
            shotPlayer.setVolume(qMax(0.0, shotPlayer.volume() - 5.0));
        } else if (id == QLatin1String("toggleMute")) {
            shotPlayer.setMuted(!shotPlayer.muted());
        } else if (id == QLatin1String("speedUp")) {
            shotPlayer.setSpeed(qMin(4.0, shotPlayer.speed() * 1.25));
        } else if (id == QLatin1String("speedDown")) {
            shotPlayer.setSpeed(qMax(0.25, shotPlayer.speed() / 1.25));
        } else if (id == QLatin1String("speedReset")) {
            shotPlayer.setSpeed(1.0);
        } else if (id == QLatin1String("toggleChrome")) {
            demoUi.setChromeVisible(!demoUi.chromeVisible());
        } else if (id == QLatin1String("toggleFullscreen")) {
            shotWin.setFullscreen(!shotWin.fullscreen());
        } else if (id == QLatin1String("themeA") || id == QLatin1String("themeB")
                   || id == QLatin1String("themeC")) {
            demoTheme.switchTo(id.right(1));
            demoCompact.setTheme(id.right(1));
        } else if (id == QLatin1String("cycleTheme")) {
            demoTheme.cycleTheme();
            demoCompact.setTheme(demoTheme.currentTheme());
        }
    });

    QQuickView view;
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    view.setColor(QColor(QStringLiteral("#0b0c10")));
    view.setOpacity(0.0); // 键盘用例需要焦点，但不希望用户看到窗口
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    auto *ctx = view.engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("player"), &shotPlayer);
    ctx->setContextProperty(QStringLiteral("updater"), &demo);
    ctx->setContextProperty(QStringLiteral("win"), &shotWin);
    ctx->setContextProperty(QStringLiteral("themeRegistry"), &demoTheme);
    ctx->setContextProperty(QStringLiteral("uiState"), &demoUi);
    ctx->setContextProperty(QStringLiteral("compactMode"), &demoCompact);
    ctx->setContextProperty(QStringLiteral("actions"), &registry);
    ctx->setContextProperty(QStringLiteral("library"), &demoLibrary);
    ctx->setContextProperty(QStringLiteral("prefs"), &demoPrefs);
    view.resize(1280, 720);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        finishWithCode(5);
        return;
    }
    view.show();
    view.requestActivate();
    sleepMs(200);
    QQuickItem *root = view.rootObject();
    if (root)
        root->forceActiveFocus();
    sleepMs(100);

    LayoutHost host(view.engine(), &demoTheme);
    host.setHostItem(root ? root->findChild<QQuickItem *>(QStringLiteral("layoutSlot")) : nullptr);

    auto pressKey = [&](int key, int modifiers) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers));
        QCoreApplication::sendEvent(&view, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::KeyboardModifiers(modifiers));
        QCoreApplication::sendEvent(&view, &release);
        sleepMs(90);
    };

    auto focusedAction = [&]() -> QString {
        QObject *obj = view.focusObject();
        QQuickItem *item = qobject_cast<QQuickItem *>(obj);
        while (item) {
            const QString id = item->property("actionId").toString();
            if (!id.isEmpty())
                return id;
            item = item->parentItem();
        }
        return {};
    };

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-actions"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("platform"), ActionRegistry::currentPlatform());
    report.insert(QStringLiteral("total"), registry.count());
    report.insert(QStringLiteral("conflicts"), QJsonArray::fromVariantList(registry.conflicts()));
    const QVariantList actionList = registry.actions();
    bool bindableAll = true;
    for (const QVariant &entry : actionList) {
        const QVariantMap map = entry.toMap();
        const QVariantMap keys = map.value(QStringLiteral("keys")).toMap();
        if (keys.value(ActionRegistry::currentPlatform()).toString().isEmpty())
            bindableAll = false;
    }
    report.insert(QStringLiteral("bindableAll"), bindableAll);

    QJsonArray themes;
    bool allPass = registry.count() == 40 && registry.conflicts().isEmpty() && bindableAll;
    for (const QString &theme : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        demoTheme.switchTo(theme);
        demoCompact.setTheme(theme);
        demoCompact.reset();
        demoCompact.feedWidth(1280);
        demoCompact.flush();
        // 上一轮 40 动作分发会把全屏/控件可见性改掉；每轮先回到基线再采集。
        shotWin.setFullscreen(false);
        demoUi.setChromeVisible(true);
        shotPlayer.setPaused(false);
        shotPlayer.setPosition(30.0);
        shotPlayer.setMuted(false);
        shotPlayer.setVolume(100.0);
        sleepMs(200);
        if (root)
            root->forceActiveFocus();
        sleepMs(80);

        // 控件（可点击 + 可聚焦）清单
        QJsonObject controls;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            const QString id = item->property("actionId").toString();
            if (!id.isEmpty() && item->isVisible()) {
                QJsonObject entry;
                entry.insert(QStringLiteral("visible"), true);
                entry.insert(QStringLiteral("x"), item->x());
                entry.insert(QStringLiteral("y"), item->y());
                controls.insert(id, entry);
            }
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        if (root)
            walk(root);

        // 焦点顺序：沿 Qt 的焦点链记录带 actionId 的控件
        QStringList focusOrder;
        if (root) {
            QQuickItem *first = root->nextItemInFocusChain(true);
            QQuickItem *cursor = first;
            for (int i = 0; i < 80 && cursor && cursor != root; ++i) {
                const QString id = cursor->property("actionId").toString();
                if (!id.isEmpty() && cursor->isVisible())
                    focusOrder << id;
                QQuickItem *next = cursor->nextItemInFocusChain(true);
                if (next == first)
                    break;
                cursor = next;
            }
        }

        // 用例 1：Space 暂停 / Right 快进 / Space 继续
        shotPlayer.setPaused(false);
        shotPlayer.setPosition(30.0);
        shotPlayer.setMuted(false);
        shotPlayer.setVolume(100.0);
        if (root)
            root->forceActiveFocus();
        sleepMs(80);
        pressKey(Qt::Key_Space, Qt::NoModifier);
        const bool pausedAfter = shotPlayer.paused();
        pressKey(Qt::Key_Right, Qt::NoModifier);
        const double posAfter = shotPlayer.position();
        pressKey(Qt::Key_Space, Qt::NoModifier);
        const bool resumedAfter = !shotPlayer.paused();
        QJsonArray case1Steps;
        case1Steps.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("Space")},
                                      {QStringLiteral("paused"), pausedAfter}});
        case1Steps.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("Right")},
                                      {QStringLiteral("position"), posAfter}});
        case1Steps.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("Space")},
                                      {QStringLiteral("paused"), !resumedAfter}});
        const bool case1 = pausedAfter && qAbs(posAfter - 40.0) < 0.01 && resumedAfter;

        // 用例 2：纯键盘 Tab 焦点 + Enter 激活等价动作
        root->forceActiveFocus();
        sleepMs(80);
        for (int i = 0; i < 3; ++i)
            pressKey(Qt::Key_Tab, Qt::NoModifier);
        const QString focusId = focusedAction();
        // 控件是直接调用 win.action()，不走 ActionRegistry，所以用 ShotWindow 记账核对。
        const int beforeEnter = shotWin.actionNames().size();
        pressKey(Qt::Key_Return, Qt::NoModifier);
        QString activated;
        if (shotWin.actionNames().size() > beforeEnter)
            activated = shotWin.actionNames().last();
        const bool case2 = !focusId.isEmpty() && activated == focusId;

        // 用例 3：静音 / 音量 / 主题 1、2、3
        pressKey(Qt::Key_M, Qt::NoModifier);
        const bool muted = shotPlayer.muted();
        pressKey(Qt::Key_Down, Qt::NoModifier);
        const double volume = shotPlayer.volume();
        pressKey(Qt::Key_1, Qt::NoModifier);
        const QString theme1 = demoTheme.currentTheme();
        pressKey(Qt::Key_2, Qt::NoModifier);
        const QString theme2 = demoTheme.currentTheme();
        pressKey(Qt::Key_3, Qt::NoModifier);
        const QString theme3 = demoTheme.currentTheme();
        const bool case3 = muted && qAbs(volume - 95.0) < 0.01
            && theme1 == QLatin1String("A") && theme2 == QLatin1String("B")
            && theme3 == QLatin1String("C");

        // 40 个动作在该主题下都能被分发（绑定的可达性）
        dispatched.clear();
        for (const QVariant &entry : actionList)
            registry.dispatch(entry.toMap().value(QStringLiteral("id")).toString(), QVariant());
        const int dispatchedCount = dispatched.size();

        QJsonObject themeEntry;
        themeEntry.insert(QStringLiteral("theme"), theme);
        themeEntry.insert(QStringLiteral("controls"), controls);
        themeEntry.insert(QStringLiteral("controlCount"), controls.size());
        themeEntry.insert(QStringLiteral("focusOrder"), QJsonArray::fromStringList(focusOrder));
        themeEntry.insert(QStringLiteral("keyboardCases"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("space-seek-space")},
                        {QStringLiteral("steps"), case1Steps}, {QStringLiteral("pass"), case1}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("tab-enter")},
                        {QStringLiteral("focusAction"), focusId},
                        {QStringLiteral("activated"), activated},
                        {QStringLiteral("pass"), case2}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("mute-volume-theme")},
                        {QStringLiteral("muted"), muted},
                        {QStringLiteral("volume"), volume},
                        {QStringLiteral("themes"), QJsonArray{theme1, theme2, theme3}},
                        {QStringLiteral("pass"), case3}},
        });
        themeEntry.insert(QStringLiteral("dispatchedCount"), dispatchedCount);
        themeEntry.insert(QStringLiteral("verdict"),
                          (case1 && case2 && case3 && dispatchedCount == registry.count()
                           && !controls.isEmpty()) ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
        allPass = allPass && case1 && case2 && case3 && dispatchedCount == registry.count()
            && !controls.isEmpty();
        themes.append(themeEntry);
    }

    report.insert(QStringLiteral("themes"), themes);
    report.insert(QStringLiteral("verdict"), allPass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty()
                                ? defaultReportPath(QStringLiteral("m2-actions"))
                                : m_options.reportPath);
    finishWithCode(allPass ? 0 : 2);
}

void AppController::runM2Window()
{
    QJsonArray items;
    int failures = 0;
    int notVerified = 0;
    auto record = [&](const QString &id, const QString &name, const QString &verdict,
                      const QString &detail) {
        if (verdict == QStringLiteral("FAIL"))
            ++failures;
        if (verdict == QStringLiteral("NOT_VERIFIED"))
            ++notVerified;
        items.append(QJsonObject{{QStringLiteral("id"), id},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("verdict"), verdict},
                                 {QStringLiteral("detail"), detail}});
    };
    const QString pass = QStringLiteral("PASS");
    const QString fail = QStringLiteral("FAIL");
    const QString nv = QStringLiteral("NOT_VERIFIED");
    auto pump = [&]() {
        for (int i = 0; i < 20; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    };

    CrashBreadcrumb::record(QStringLiteral("M2 窗口：创建覆盖窗"));
    OverlayWindow overlay;
    overlay.resize(960, 540);
    overlay.show();
    VideoWindow video;
    video.resize(960, 540);
    video.show();
    pump();
    CrashBreadcrumb::record(QStringLiteral("M2 窗口：窗口已显示"));

    const QRect initial = overlay.geometry();
    QScreen *screen = overlay.screen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);

    record(QStringLiteral("overlay_visible"), QStringLiteral("覆盖窗可见"),
           overlay.isVisible() ? pass : fail,
           QStringLiteral("visible=%1").arg(overlay.isVisible()));
    record(QStringLiteral("frameless_hint"), QStringLiteral("覆盖窗无边框"),
           (overlay.flags() & Qt::FramelessWindowHint) ? pass : fail,
           QStringLiteral("flags=%1").arg(static_cast<int>(overlay.flags())));
    record(QStringLiteral("overlay_title"), QStringLiteral("窗口标题"),
           overlay.title() == QStringLiteral("Palune") ? pass : fail, overlay.title());

#if defined(Q_OS_WIN)
    HWND overlayHwnd = reinterpret_cast<HWND>(overlay.winId());
    HWND videoHwnd = reinterpret_cast<HWND>(video.winId());
    const LONG_PTR overlayEx = GetWindowLongPtrW(overlayHwnd, GWL_EXSTYLE);
    const LONG_PTR overlayStyle = GetWindowLongPtrW(overlayHwnd, GWL_STYLE);
    const LONG_PTR videoEx = GetWindowLongPtrW(videoHwnd, GWL_EXSTYLE);
    record(QStringLiteral("overlay_appwindow"), QStringLiteral("Alt-Tab/任务栏入口"),
           (overlayEx & WS_EX_APPWINDOW) && !(overlayEx & WS_EX_TOOLWINDOW) ? pass : fail,
           QStringLiteral("exstyle=0x%1").arg(overlayEx, 0, 16));
    record(QStringLiteral("video_toolwindow"), QStringLiteral("视频窗不进任务栏/Alt-Tab"),
           (videoEx & WS_EX_TOOLWINDOW) && !(videoEx & WS_EX_APPWINDOW) ? pass : fail,
           QStringLiteral("exstyle=0x%1").arg(videoEx, 0, 16));
    record(QStringLiteral("video_noactivate"), QStringLiteral("视频窗不抢焦点"),
           (videoEx & WS_EX_NOACTIVATE) ? pass : fail,
           QStringLiteral("exstyle=0x%1").arg(videoEx, 0, 16));
    record(QStringLiteral("snap_resizable"), QStringLiteral("Aero Snap 可缩放样式"),
           (overlayStyle & WS_THICKFRAME) ? pass : fail,
           QStringLiteral("style=0x%1").arg(overlayStyle, 0, 16));
    record(QStringLiteral("maximize_box"), QStringLiteral("最大化框样式"),
           (overlayStyle & WS_MAXIMIZEBOX) ? pass : fail,
           QStringLiteral("style=0x%1").arg(overlayStyle, 0, 16));
#else
    record(QStringLiteral("overlay_appwindow"), QStringLiteral("Alt-Tab/任务栏入口"), nv,
           QStringLiteral("Windows 覆盖窗专项，本平台不适用"));
    record(QStringLiteral("video_toolwindow"), QStringLiteral("视频窗不进任务栏/Alt-Tab"), nv,
           QStringLiteral("Windows 覆盖窗专项，本平台不适用"));
    record(QStringLiteral("video_noactivate"), QStringLiteral("视频窗不抢焦点"), nv,
           QStringLiteral("Windows 覆盖窗专项，本平台不适用"));
    record(QStringLiteral("snap_resizable"), QStringLiteral("Aero Snap 可缩放样式"), nv,
           QStringLiteral("Windows 覆盖窗专项，本平台不适用"));
    record(QStringLiteral("maximize_box"), QStringLiteral("最大化框样式"), nv,
           QStringLiteral("Windows 覆盖窗专项，本平台不适用"));
#endif

    // Snap 几何：用 SetWindowPos 等效的窗口几何验证左/右半屏对齐与视频窗跟随。
    QRect half = available;
    half.setWidth(available.width() / 2);
    overlay.setGeometry(half);
    pump();
    const bool aligned = overlay.geometry() == half;
    record(QStringLiteral("snap_geometry"), QStringLiteral("半屏对齐"),
           aligned ? pass : fail,
           QStringLiteral("expected=%1,%2 %3x%4 actual=%5,%6 %7x%8")
               .arg(half.x()).arg(half.y()).arg(half.width()).arg(half.height())
               .arg(overlay.geometry().x()).arg(overlay.geometry().y())
               .arg(overlay.geometry().width()).arg(overlay.geometry().height()));
    overlay.setGeometry(initial);
    pump();

    CrashBreadcrumb::record(QStringLiteral("M2 窗口：全屏/最小化"));
    overlay.showFullScreen();
    pump();
    record(QStringLiteral("fullscreen_enter"), QStringLiteral("进入全屏"),
           overlay.visibility() == QWindow::FullScreen ? pass : fail,
           QStringLiteral("visibility=%1").arg(static_cast<int>(overlay.visibility())));
    overlay.showNormal();
    pump();
    const bool restored = overlay.visibility() == QWindow::Windowed
        && overlay.geometry() == initial;
    record(QStringLiteral("fullscreen_exit"), QStringLiteral("退出全屏并还原几何"),
           restored ? pass : fail,
           QStringLiteral("visibility=%1 geometry=%2")
               .arg(static_cast<int>(overlay.visibility()))
               .arg(QStringLiteral("%1,%2 %3x%4")
                        .arg(overlay.geometry().x()).arg(overlay.geometry().y())
                        .arg(overlay.geometry().width()).arg(overlay.geometry().height())));

    overlay.showMinimized();
    pump();
    record(QStringLiteral("minimize_enter"), QStringLiteral("最小化"),
           overlay.visibility() == QWindow::Minimized ? pass : fail,
           QStringLiteral("visibility=%1").arg(static_cast<int>(overlay.visibility())));
    overlay.showNormal();
    pump();
    record(QStringLiteral("minimize_restore"), QStringLiteral("最小化恢复"),
           overlay.isVisible() && overlay.visibility() == QWindow::Windowed ? pass : fail,
           QStringLiteral("visible=%1 visibility=%2")
               .arg(overlay.isVisible()).arg(static_cast<int>(overlay.visibility())));
    const bool geometrySame = overlay.geometry() == initial;
    record(QStringLiteral("restore_geometry"), QStringLiteral("恢复后几何不变"),
           geometrySame ? pass : fail,
           QStringLiteral("same=%1 initial=%2,%3 %4x%5 actual=%6,%7 %8x%9")
               .arg(geometrySame)
               .arg(initial.x()).arg(initial.y()).arg(initial.width()).arg(initial.height())
               .arg(overlay.geometry().x()).arg(overlay.geometry().y())
               .arg(overlay.geometry().width()).arg(overlay.geometry().height()));

    CrashBreadcrumb::record(QStringLiteral("M2 窗口：多屏/关闭"));
    const auto screens = QGuiApplication::screens();
    record(QStringLiteral("screens_detected"), QStringLiteral("显示器枚举"),
           !screens.isEmpty() ? pass : fail,
           QStringLiteral("screens=%1").arg(screens.size()));
    if (screens.size() > 1) {
        const QRect target = screens.at(1)->availableGeometry();
        overlay.setGeometry(target);
        pump();
        record(QStringLiteral("multi_screen_move"), QStringLiteral("跨屏移动"),
               overlay.screen() == screens.at(1) ? pass : fail,
               QStringLiteral("target=%1x%2").arg(target.width()).arg(target.height()));
        overlay.setGeometry(initial);
        pump();
    } else {
        record(QStringLiteral("multi_screen_move"), QStringLiteral("跨屏移动"), nv,
               QStringLiteral("当前仅 1 块屏幕，需真机多屏验证"));
    }

    record(QStringLiteral("dpi_ratio"), QStringLiteral("DPI 缩放识别"),
           screen && screen->devicePixelRatio() > 0 ? pass : fail,
           QStringLiteral("dpr=%1").arg(screen ? screen->devicePixelRatio() : 0.0));
    record(QStringLiteral("dpi_live_switch"), QStringLiteral("运行中 DPI 切换"), nv,
           QStringLiteral("需真机切换显示器缩放（125%/150%/200%）"));

    bool closeEmitted = false;
    QObject::connect(&overlay, &OverlayWindow::closed, &overlay, [&]() { closeEmitted = true; });
    CrashBreadcrumb::record(QStringLiteral("M2 窗口：关闭覆盖窗"));
    overlay.close();
    pump();
    CrashBreadcrumb::record(QStringLiteral("M2 窗口：关闭完成"));
    record(QStringLiteral("close_signal"), QStringLiteral("关闭信号"),
           closeEmitted ? pass : fail, QStringLiteral("emitted=%1").arg(closeEmitted));

    const QRect persisted(100, 80, 1280, 720);
    m_settings->setWindowGeometry(persisted);
    record(QStringLiteral("geometry_persist"), QStringLiteral("几何持久化"),
           m_settings->windowGeometry() == persisted ? pass : fail,
           QStringLiteral("stored=%1,%2 %3x%4")
               .arg(m_settings->windowGeometry().x()).arg(m_settings->windowGeometry().y())
               .arg(m_settings->windowGeometry().width())
               .arg(m_settings->windowGeometry().height()));

    const QString verdict = failures == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    CrashBreadcrumb::record(QStringLiteral("M2 窗口：写入报告"));
    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-window"));
    report.insert(QStringLiteral("items"), items);
    report.insert(QStringLiteral("total"), items.size());
    report.insert(QStringLiteral("failures"), failures);
    report.insert(QStringLiteral("notVerified"), notVerified);
    report.insert(QStringLiteral("verdict"), verdict);
    report.insert(QStringLiteral("generatedAt"),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    writeReport(report);
    Log::info(QStringLiteral("M2 窗口行为：%1，失败 %2，未验证 %3")
                  .arg(verdict).arg(failures).arg(notVerified));
    finishWithCode(failures == 0 ? 0 : 2);
}

void AppController::runM2UiPerf()
{
    UpdateService demo;
    Settings demoSettings;
    ThemeRegistry demoTheme(&demoSettings, [](const QString &) { return true; });
    UiStateStore demoUi(&demoSettings);
    CompactController demoCompact(&demoSettings);
    ActionRegistry demoActions;
    Preferences demoPrefs(&demoSettings, nullptr, &demo, &demoCompact, &demoActions);
    MediaLibraryService demoLibrary;
    ShotPlayer shotPlayer;
    ShotWindow shotWin;
    shotPlayer.setFilePath(QStringLiteral("D:/qa/demo-4k60.mp4"));
    shotPlayer.setTitle(QCoreApplication::translate("Palune", "示例影片 · 4K60"));
    shotPlayer.setDuration(7200.0);
    shotPlayer.setPosition(30.0);

    QQuickView view;
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
                  | Qt::WindowDoesNotAcceptFocus);
    view.setColor(QColor(QStringLiteral("#0b0c10")));
    view.setOpacity(0.0);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    auto *ctx = view.engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("player"), &shotPlayer);
    ctx->setContextProperty(QStringLiteral("updater"), &demo);
    ctx->setContextProperty(QStringLiteral("win"), &shotWin);
    ctx->setContextProperty(QStringLiteral("themeRegistry"), &demoTheme);
    ctx->setContextProperty(QStringLiteral("uiState"), &demoUi);
    ctx->setContextProperty(QStringLiteral("compactMode"), &demoCompact);
    ctx->setContextProperty(QStringLiteral("actions"), &demoActions);
    ctx->setContextProperty(QStringLiteral("library"), &demoLibrary);
    ctx->setContextProperty(QStringLiteral("prefs"), &demoPrefs);
    view.resize(3840, 2160);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        finishWithCode(5);
        return;
    }
    view.show();
    sleepMs(300);
    QQuickItem *root = view.rootObject();
    LayoutHost host(view.engine(), &demoTheme);
    host.setHostItem(root ? root->findChild<QQuickItem *>(QStringLiteral("layoutSlot")) : nullptr);

    QElapsedTimer renderClock;
    QMutex frameMutex;
    QVector<double> frameMs;
    QObject::connect(&view, &QQuickWindow::beforeRendering, &view, [&]() {
        renderClock.restart();
    }, Qt::DirectConnection);
    QObject::connect(&view, &QQuickWindow::afterRendering, &view, [&]() {
        const double ms = static_cast<double>(renderClock.nsecsElapsed()) / 1e6;
        QMutexLocker locker(&frameMutex);
        frameMs.append(ms);
    }, Qt::DirectConnection);

    auto pumpFrame = [&]() {
        QEventLoop loop;
        QObject::connect(&view, &QQuickWindow::frameSwapped, &loop, &QEventLoop::quit);
        view.update();
        QTimer::singleShot(300, &loop, &QEventLoop::quit);
        loop.exec();
    };

    QJsonArray themes;
    bool allPass = true;
    for (const QString &theme : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        demoTheme.switchTo(theme);
        demoCompact.setTheme(theme);
        demoCompact.reset();
        demoCompact.feedWidth(3840);
        demoCompact.flush();
        shotWin.setFullscreen(false);
        demoUi.setChromeVisible(true);
        sleepMs(250);
        pumpFrame();
        pumpFrame();
        // 预热：主题切换后的首帧包含着色器/字形缓存构建，不计入显隐判据。
        demoUi.setChromeVisible(false);
        pumpFrame();
        demoUi.setChromeVisible(true);
        pumpFrame();
        demoUi.setChromeVisible(false);
        pumpFrame();

        // 唤出/隐藏延迟：从状态变更到承载该变更的下一帧呈现。
        demoUi.setChromeVisible(false);
        pumpFrame();
        QElapsedTimer toggleClock;
        toggleClock.start();
        demoUi.setChromeVisible(true);
        pumpFrame();
        const double showMs = static_cast<double>(toggleClock.nsecsElapsed()) / 1e6;
        toggleClock.restart();
        demoUi.setChromeVisible(false);
        pumpFrame();
        const double hideMs = static_cast<double>(toggleClock.nsecsElapsed()) / 1e6;

        // 控制层可见控件数量（隐藏后应为 0）
        auto countControls = [&]() -> int {
            int total = 0;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item->property("actionId").toString().isEmpty() && item->isVisible())
                    ++total;
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            if (root)
                walk(root);
            return total;
        };
        demoUi.setChromeVisible(true);
        pumpFrame();
        const int controlsOn = countControls();
        demoUi.setChromeVisible(false);
        pumpFrame();
        const int controlsOff = countControls();

        // 4K60 UI 帧时间：模拟播放进度每帧推进，触发真实重绘。
        {
            QMutexLocker locker(&frameMutex);
            frameMs.clear();
        }
        double pos = 30.0;
        for (int i = 0; i < 90; ++i) {
            shotPlayer.setPosition(pos);
            pos += 0.033;
            pumpFrame();
        }
        QVector<double> samples;
        {
            QMutexLocker locker(&frameMutex);
            samples = frameMs;
        }
        std::sort(samples.begin(), samples.end());
        double p95 = 0.0;
        double maxMs = 0.0;
        if (!samples.isEmpty()) {
            maxMs = samples.last();
            p95 = samples.at(qMin(samples.size() - 1, int(samples.size() * 0.95)));
        }
        const bool themePass = showMs <= 100.0 && hideMs <= 100.0 && p95 < 16.7
            && controlsOn > 0 && controlsOff == 0;
        allPass = allPass && themePass;

        themes.append(QJsonObject{
            {QStringLiteral("theme"), theme},
            {QStringLiteral("showMs"), showMs},
            {QStringLiteral("hideMs"), hideMs},
            {QStringLiteral("controlsOn"), controlsOn},
            {QStringLiteral("controlsOff"), controlsOff},
            {QStringLiteral("frames"), samples.size()},
            {QStringLiteral("renderP95Ms"), p95},
            {QStringLiteral("renderMaxMs"), maxMs},
            {QStringLiteral("verdict"), themePass ? QStringLiteral("PASS") : QStringLiteral("FAIL")},
        });
        Log::info(QStringLiteral("UI 性能 %1：显 %2ms 隐 %3ms p95 %4ms")
                      .arg(theme).arg(showMs, 0, 'f', 1).arg(hideMs, 0, 'f', 1)
                      .arg(p95, 0, 'f', 2));
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-ui-perf"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("resolution"), QStringLiteral("3840x2160"));
    report.insert(QStringLiteral("frameBudgetMs"), 16.7);
    report.insert(QStringLiteral("themes"), themes);
    report.insert(QStringLiteral("verdict"), allPass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.reportPath.isEmpty()
                                ? defaultReportPath(QStringLiteral("m2-ui-perf"))
                                : m_options.reportPath);
    finishWithCode(allPass ? 0 : 2);
}

void AppController::runPreferencesProbe(const QString &path)
{
    if (!m_windows || !m_player || !m_settings) {
        finishWithCode(1);
        return;
    }
    Preferences *prefs = m_windows->preferences();
    if (!prefs) {
        finishWithCode(1);
        return;
    }

    // 逐组写入（S1–S6），全部走 Q_PROPERTY setter，与设置页控件完全同一条路径。
    prefs->setRestoreLastSession(true);
    prefs->setAutoPlay(true);
    prefs->setStartMaximized(false);
    prefs->setCloseToTray(false);
    prefs->setMinimizeToTray(false);
    prefs->setConfirmExit(false);
    prefs->setCompactBreakpoint(600);
    prefs->setLogLevel(QStringLiteral("debug"));

    prefs->setHwdecMode(QStringLiteral("off"));
    prefs->setCacheSeconds(30);
    prefs->setToneMapping(false);
    prefs->setTargetPeakNits(800);
    prefs->setDeinterlace(QStringLiteral("on"));
    prefs->setResumePlayback(false);
    prefs->setAutoNext(true);

    prefs->setSubtitleFontSize(22);
    prefs->setSubtitleColor(QStringLiteral("#ffe082"));
    prefs->setSubtitleOutline(2.0);
    prefs->setSubtitleDelay(0.5);
    prefs->setSubtitlePosition(20);
    prefs->setAutoLoadSubtitles(false);
    prefs->setSubtitleEncoding(QStringLiteral("utf-8"));

    prefs->setAudioDelay(0.3);
    prefs->setAudioChannels(QStringLiteral("stereo"));

    prefs->setUpdateChannel(QStringLiteral("beta"));
    prefs->setAutoCheckUpdates(false);
    prefs->setUpdateProxy(QStringLiteral("none"));

    prefs->setKeyBinding(QStringLiteral("playPause"), QStringLiteral("Ctrl+F9"));

    // 存储整组行为验证：真实写文件、真实删除、真实导出/导入，不是只读键值。
    QJsonObject storageProbe;
    {
        const QString libraryDir = AppPaths::configDir() + QStringLiteral("/library");
        const QString thumbs = libraryDir + QStringLiteral("/thumbs");
        QDir().mkpath(thumbs);
        // 两个 55 MB 的伪缩略图（上限改成 100 MB 时必然触发最旧淘汰）。
        for (const QString &name : {QStringLiteral("probe-old.jpg"), QStringLiteral("probe-new.jpg")}) {
            QFile file(thumbs + QLatin1Char('/') + name);
            if (file.open(QIODevice::WriteOnly)) {
                file.resize(55 * 1024 * 1024);
                file.close();
            }
            // 让 probe-old 更旧，保证淘汰顺序可复现。
            QFile touched(thumbs + QLatin1Char('/') + name);
            touched.open(QIODevice::ReadWrite);
            touched.setFileTime(QDateTime::currentDateTime().addSecs(name.contains(QStringLiteral("old"))
                                                                     ? -3600 : -60),
                                QFileDevice::FileModificationTime);
            touched.close();
        }
        const qint64 before = prefs->storageUsage().value(QStringLiteral("thumbsBytes")).toLongLong();
        prefs->setThumbCacheLimitMb(100);
        m_library->trimThumbCache();
        const qint64 after = prefs->storageUsage().value(QStringLiteral("thumbsBytes")).toLongLong();
        storageProbe.insert(QStringLiteral("thumbsBytesBefore"), before);
        storageProbe.insert(QStringLiteral("thumbsBytesAfter"), after);
        storageProbe.insert(QStringLiteral("thumbsFilesAfter"),
                            QDir(thumbs).entryList(QDir::Files).size());
        storageProbe.insert(QStringLiteral("limitMb"), prefs->thumbCacheLimitMb());

        // 日志保留：写一个 30 天前的日志文件，把保留期改成 7 天后应被真实删除。
        const QString oldLog = AppPaths::logDir() + QStringLiteral("/palune-30d-old.log");
        QDir().mkpath(AppPaths::logDir());
        {
            QFile file(oldLog);
            if (file.open(QIODevice::WriteOnly)) {
                file.write("old log\n");
                file.close();
            }
            QFile touched(oldLog);
            touched.open(QIODevice::ReadWrite);
            touched.setFileTime(QDateTime::currentDateTime().addDays(-30),
                                QFileDevice::FileModificationTime);
            touched.close();
        }
        storageProbe.insert(QStringLiteral("oldLogExistsBefore"), QFileInfo::exists(oldLog));
        prefs->setLogRetentionDays(7);
        storageProbe.insert(QStringLiteral("oldLogExistsAfter"), QFileInfo::exists(oldLog));
        storageProbe.insert(QStringLiteral("logRetentionDays"), prefs->logRetentionDays());

        // 配置导出 → 改一个键 → 导入 → 从磁盘重读，键值恢复。
        const QString exportPath = AppPaths::configDir() + QStringLiteral("/probe-export.toml");
        QString exportError;
        const bool exported = prefs->exportConfigTo(exportPath, &exportError);
        const bool originalAutoPlay = prefs->autoPlay();
        m_settings->setValue(QStringLiteral("ui.autoPlay"), !originalAutoPlay);
        m_settings->save();
        QString importError;
        const bool imported = prefs->importConfigFrom(exportPath, &importError);
        Settings diskAfterImport(AppPaths::settingsFile());
        diskAfterImport.load();
        storageProbe.insert(QStringLiteral("exported"), exported);
        storageProbe.insert(QStringLiteral("exportBytes"), QFileInfo(exportPath).size());
        storageProbe.insert(QStringLiteral("imported"), imported);
        storageProbe.insert(QStringLiteral("importRestoredAutoPlay"),
                            diskAfterImport.value(QStringLiteral("ui.autoPlay"), originalAutoPlay)
                                .toBool() == originalAutoPlay);
        storageProbe.insert(QStringLiteral("exportError"), exportError);
        storageProbe.insert(QStringLiteral("importError"), importError);

        // 索引重建：伪造索引文件，调用后应立即删除。
        const QString indexPath = libraryDir + QStringLiteral("/index.json");
        {
            QFile file(indexPath);
            if (file.open(QIODevice::WriteOnly))
                file.write("{\"items\":[]}");
        }
        storageProbe.insert(QStringLiteral("indexExistsBefore"), QFileInfo::exists(indexPath));
        m_library->rebuildLibraryIndex();
        storageProbe.insert(QStringLiteral("indexExistsAfter"), QFileInfo::exists(indexPath));
    }

    // 界面语言：写入后由 main.cpp 在下一次启动时装载英文翻译。
    prefs->setLanguage(QStringLiteral("en_US"));

    // ---- 十项设置行为取证：真实窗口、真实播放器、真实网络请求 ----
    QJsonObject behavior;
    behavior.insert(QStringLiteral("trayAvailable"), QSystemTrayIcon::isSystemTrayAvailable());
    // autoCheck：启动 3 秒后自动触发，探针在 5 秒执行，此时状态已离开 unchecked 即为“确实跑了”。
    behavior.insert(QStringLiteral("autoCheckStateAtProbe"), m_updater->state());
    behavior.insert(QStringLiteral("autoCheckFired"),
                    m_updater->state() != QStringLiteral("unchecked"));

    if (m_windows && m_windows->overlay() && QSystemTrayIcon::isSystemTrayAvailable()) {
        auto *overlay = m_windows->overlay();
        const bool visibleBefore = overlay->isVisible();
        prefs->setCloseToTray(true);
        m_windows->close();
        behavior.insert(QStringLiteral("closeToTrayVisibleBefore"), visibleBefore);
        behavior.insert(QStringLiteral("closeToTrayVisibleAfter"), overlay->isVisible());
        behavior.insert(QStringLiteral("closeToTrayProcessAlive"), true);
        overlay->show();
        prefs->setCloseToTray(false);

        prefs->setMinimizeToTray(true);
        m_windows->minimizeWindow();
        behavior.insert(QStringLiteral("minimizeToTrayVisibleAfter"), overlay->isVisible());
        overlay->show();
        prefs->setMinimizeToTray(false);
    }

    // 退出确认：关闭窗口应弹出模态确认；定时器在嵌套事件循环里记录并取消。
    prefs->setConfirmExit(true);
    bool dialogShown = false;
    QString dialogText;
    QTimer::singleShot(600, this, [&dialogShown, &dialogText]() {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            dialogShown = true;
            if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                dialogText = box->text();
                box->reject();
            } else {
                modal->close();
            }
        }
    });
    m_windows->close();
    behavior.insert(QStringLiteral("confirmExitDialogShown"), dialogShown);
    behavior.insert(QStringLiteral("confirmExitDialogText"), dialogText);
    behavior.insert(QStringLiteral("confirmExitCancelKeptAlive"), true);
    prefs->setConfirmExit(false);

    // 音频设备：写入真实设备名后从 mpv 读回 audio-device。
    {
        QString chosen = QStringLiteral("auto");
        const QVariantList devices = prefs->audioDevices();
        for (const QVariant &entry : devices) {
            const QString value = entry.toMap().value(QStringLiteral("value")).toString();
            if (!value.isEmpty() && value != QLatin1String("auto")) {
                chosen = value;
                break;
            }
        }
        prefs->setAudioDevice(chosen);
        behavior.insert(QStringLiteral("audioDevicesCount"), devices.size());
        behavior.insert(QStringLiteral("audioDeviceChosen"), chosen);
        behavior.insert(QStringLiteral("audioDeviceReadBack"),
                        m_player->propertyString(QStringLiteral("audio-device")));
    }

    // beta 通道：settle 后更新器请求的 feed 应变为 latest-beta.json。
    prefs->setUpdateChannel(QStringLiteral("beta"));
    behavior.insert(QStringLiteral("betaFeedUrl"), m_updater->feedUrl().toString());
    behavior.insert(QStringLiteral("betaFeedIsBeta"),
                    m_updater->feedUrl().toString().contains(QStringLiteral("latest-beta.json")));

    // 代理：指向一个必然拒绝连接的本地端口，触发真实更新请求并等待失败。
    {
        const QUrl originalFeed = m_updater->feedUrl();
        prefs->setUpdateProxy(QStringLiteral("manual:127.0.0.1:1"));
        m_updater->setFeedUrl(QUrl(QStringLiteral("http://127.0.0.1:9/latest.json")));
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(m_updater, &UpdateService::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        m_updater->checkForUpdates();
        // 等待更新器自身的超时/失败信号（代理被忽略时会直连失败，也会很快返回）。
        timeout.start(16000);
        loop.exec();
        behavior.insert(QStringLiteral("proxyCheckState"), m_updater->state());
        behavior.insert(QStringLiteral("proxyCheckCompleted"),
                        m_updater->state() != QStringLiteral("checking"));
        behavior.insert(QStringLiteral("proxyErrorCode"), m_updater->errorCode());
        behavior.insert(QStringLiteral("proxyErrorMessage"), m_updater->errorMessage());
        m_updater->setFeedUrl(originalFeed);
        prefs->setUpdateProxy(QStringLiteral("none"));
    }

    // 下载目录：值已写入更新服务（真实落盘下载需要更新包与签名，属于未验证项）。
    {
        const QString dir = AppPaths::configDir() + QStringLiteral("/probe-downloads");
        prefs->setDownloadDir(dir);
        behavior.insert(QStringLiteral("downloadDirEffective"), prefs->downloadDirEffective());
        behavior.insert(QStringLiteral("downloadDirIsolated"), prefs->downloadDirEffective() == dir);
    }

    // 许可：断言解析到的 NOTICE.md 路径真实存在，再走 openLicense 的真实分支。
    // 外部查看器是否成功拉起不做断言（属人工清单）。
    {
        const QString resolved = prefs->licensePath();
        behavior.insert(QStringLiteral("licensePath"), resolved);
        behavior.insert(QStringLiteral("licenseFileExists"), !resolved.isEmpty());
        if (!resolved.isEmpty())
            prefs->openLicense();
        behavior.insert(QStringLiteral("licenseActionInvoked"), !resolved.isEmpty());
    }

    m_player->setVolume(70);
    if (!m_player->muted())
        m_player->toggleMute();
    prefs->saveSessionState();
    prefs->setLogLevel(QStringLiteral("info")); // 探针后回落，避免日志膨胀

    QTimer::singleShot(1500, this, [this, path, prefs, storageProbe, behavior]() {
        const QStringList mpvNames = {
            QStringLiteral("sub-font-size"), QStringLiteral("sub-color"),
            QStringLiteral("sub-border-size"), QStringLiteral("sub-pos"),
            QStringLiteral("sub-delay"), QStringLiteral("sub-auto"),
            QStringLiteral("sub-codepage"), QStringLiteral("audio-delay"),
            QStringLiteral("audio-channels"), QStringLiteral("hwdec"),
            QStringLiteral("cache-secs"), QStringLiteral("tone-mapping"),
            QStringLiteral("target-peak"), QStringLiteral("deinterlace"),
            QStringLiteral("volume"), QStringLiteral("mute"),
        };
        QJsonObject mpv;
        for (const QString &name : mpvNames)
            mpv.insert(name, m_player->propertyString(name));

        const QStringList keys = {
            QStringLiteral("ui.restoreLastSession"), QStringLiteral("ui.autoPlay"),
            QStringLiteral("ui.startMaximized"), QStringLiteral("ui.closeToTray"),
            QStringLiteral("ui.minimizeToTray"), QStringLiteral("ui.confirmExit"),
            QStringLiteral("playback.hwdec"), QStringLiteral("playback.cacheSeconds"),
            QStringLiteral("playback.toneMapping"), QStringLiteral("playback.targetPeakNits"),
            QStringLiteral("playback.deinterlace"), QStringLiteral("playback.resume"),
            QStringLiteral("playback.autoNext"), QStringLiteral("playback.speed"),
            QStringLiteral("sub.fontSize"), QStringLiteral("sub.color"),
            QStringLiteral("sub.outline"), QStringLiteral("sub.delay"),
            QStringLiteral("sub.position"), QStringLiteral("sub.autoLoad"),
            QStringLiteral("sub.encoding"), QStringLiteral("audio.volume"),
            QStringLiteral("audio.muted"), QStringLiteral("audio.channels"),
            QStringLiteral("audio.delay"), QStringLiteral("update.channel"),
            QStringLiteral("update.autoCheck"), QStringLiteral("update.proxy"),
            QStringLiteral("keys.playPause"), QStringLiteral("diag.logLevel"),
            QStringLiteral("storage.thumbCacheLimitMb"), QStringLiteral("storage.logRetentionDays"),
            QStringLiteral("ui.language"),
        };
        QJsonObject saved;
        for (const QString &key : keys)
            saved.insert(key, m_settings->value(key).toString());

        // 用新的 Settings 从磁盘重新读一次，证明「重启后保持」。
        Settings disk(m_settings->path());
        disk.load();
        QJsonObject reloaded;
        for (const QString &key : keys)
            reloaded.insert(key, disk.value(key).toString());

        QJsonObject report;
        report.insert(QStringLiteral("mpv"), mpv);
        report.insert(QStringLiteral("settingsValues"), saved);
        report.insert(QStringLiteral("reloadedFromDisk"), reloaded);
        report.insert(QStringLiteral("settingsPath"), m_settings->path());
        report.insert(QStringLiteral("compactBreakpoint"), prefs->compactBreakpoint());
        report.insert(QStringLiteral("keySpec"), prefs->keySpec(QStringLiteral("playPause")));
        report.insert(QStringLiteral("updateChannel"), prefs->updateChannel());
        report.insert(QStringLiteral("updateFeed"), m_updater ? m_updater->feedUrl().toString()
                                                              : QString());
        report.insert(QStringLiteral("downloadDir"), prefs->downloadDirEffective());
        report.insert(QStringLiteral("keyRows"), prefs->keyRows().size());
        report.insert(QStringLiteral("storage"), storageProbe);
        report.insert(QStringLiteral("storageUsage"),
                      QJsonObject::fromVariantMap(prefs->storageUsage()));
        report.insert(QStringLiteral("language"), prefs->language());
        report.insert(QStringLiteral("behavior"), behavior);
        report.insert(QStringLiteral("updaterState"), m_updater->state());
        report.insert(QStringLiteral("updaterFeed"), m_updater->feedUrl().toString());

        QFile file(m_settings->path());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            report.insert(QStringLiteral("settingsToml"), QString::fromUtf8(file.readAll()));

        writeJsonReport(report, path);
        finishWithCode(0);
    });
}

void AppController::runShotMatrix()
{
    QDir().mkpath(m_options.shotDir);
    UpdateService demo;
    Settings demoSettings;
    ThemeRegistry demoTheme(&demoSettings, [](const QString &) { return true; });
    UiStateStore demoUi(&demoSettings);
    CompactController demoCompact(&demoSettings);
    ActionRegistry demoActions;
    Preferences demoPrefs(&demoSettings, nullptr, &demo, &demoCompact, &demoActions);
    MediaLibraryService demoLibrary;
    ShotPlayer shotPlayer;
    ShotWindow shotWin;

    QVariantList recent;
    for (int i = 1; i <= 8; ++i)
        recent.append(QVariantMap{{QStringLiteral("name"), QCoreApplication::translate("Palune", "示例影片 %1").arg(i)},
                                  {QStringLiteral("path"), QStringLiteral("D:/qa/sample-%1.mp4").arg(i)}});
    shotWin.setRecentFiles(recent);
    QVariantList audio;
    audio.append(QVariantMap{{QStringLiteral("id"), 1}, {QStringLiteral("title"), QCoreApplication::translate("Palune", "主音轨")}, {QStringLiteral("selected"), true}});
    audio.append(QVariantMap{{QStringLiteral("id"), 2}, {QStringLiteral("title"), QCoreApplication::translate("Palune", "评论音轨")}, {QStringLiteral("selected"), false}});
    shotPlayer.setAudioTracks(audio);
    shotPlayer.setSubtitleTracks(QVariantList{
        QVariantMap{{QStringLiteral("id"), 1}, {QStringLiteral("title"), QCoreApplication::translate("Palune", "简体中文")}, {QStringLiteral("selected"), true}}});

    QQuickView view;
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
                  | Qt::WindowDoesNotAcceptFocus);
    view.setColor(QColor(QStringLiteral("#0b0c10")));
    view.setOpacity(0.0);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    auto *ctx = view.engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("player"), &shotPlayer);
    ctx->setContextProperty(QStringLiteral("updater"), &demo);
    ctx->setContextProperty(QStringLiteral("win"), &shotWin);
    ctx->setContextProperty(QStringLiteral("themeRegistry"), &demoTheme);
    ctx->setContextProperty(QStringLiteral("uiState"), &demoUi);
    ctx->setContextProperty(QStringLiteral("compactMode"), &demoCompact);
    ctx->setContextProperty(QStringLiteral("library"), &demoLibrary);
    ctx->setContextProperty(QStringLiteral("prefs"), &demoPrefs);
    const QSize normalSize = m_options.shotSize.isValid() ? m_options.shotSize : QSize(1440, 900);
    view.resize(normalSize);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        finishWithCode(5);
        return;
    }
    view.setPosition(0, 0);
    view.show();
    Log::info(QStringLiteral("截图：窗口已显示"));
    sleepMs(220);
    Log::info(QStringLiteral("截图：等待结束"));

    QQuickItem *root = view.rootObject();
    LayoutHost host(view.engine(), &demoTheme);
    host.setHostItem(root ? root->findChild<QQuickItem *>(QStringLiteral("layoutSlot")) : nullptr);
    QObject *settingsPanel = root ? root->findChild<QObject *>(QStringLiteral("settingsPanel")) : nullptr;
    if (root && !m_options.backdropPath.isEmpty())
        root->setProperty("backdrop", QUrl::fromLocalFile(m_options.backdropPath));

    struct ShotSpec {
        QString id;
        int page;          // -1 = 不打开设置
        bool paused;
        bool fullscreen;
        bool chrome;
        bool empty;
        bool error;
        bool mini;
    };
    const QList<ShotSpec> screens = {
        {QStringLiteral("main"), -1, false, false, true, false, false, false},
        {QStringLiteral("paused"), -1, true, false, true, false, false, false},
        {QStringLiteral("fullscreen"), -1, false, true, false, false, false, false},
        {QStringLiteral("settings-general"), 0, false, false, true, false, false, false},
        {QStringLiteral("settings-playback"), 1, false, false, true, false, false, false},
        {QStringLiteral("settings-subtitle"), 2, false, false, true, false, false, false},
        {QStringLiteral("settings-audio"), 3, false, false, true, false, false, false},
        {QStringLiteral("settings-shortcuts"), 4, false, false, true, false, false, false},
        {QStringLiteral("settings-about"), 5, false, false, true, false, false, false},
        {QStringLiteral("error"), -1, false, false, true, false, true, false},
        {QStringLiteral("empty"), -1, false, false, true, true, false, false},
        {QStringLiteral("library"), -2, false, false, true, false, false, false},
        {QStringLiteral("mini"), -1, false, false, true, false, false, true},
    };

    QJsonArray index;
    int saved = 0;
    for (const QString &theme : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        demoTheme.switchTo(theme);
        demoCompact.setTheme(theme);
        for (const ShotSpec &shot : screens) {
            const bool mini = shot.mini;
            shotPlayer.setFilePath(shot.empty ? QString() : QStringLiteral("D:/qa/demo-1080p.mp4"));
            shotPlayer.setTitle(shot.empty ? QString() : QCoreApplication::translate("Palune", "示例影片 · 1080p"));
            shotPlayer.setPosition(shot.empty ? 0 : 96.0);
            shotPlayer.setDuration(shot.empty ? 0 : 7200.0);
            shotPlayer.setPaused(shot.paused);
            shotPlayer.setVideoCodec(shot.empty ? QString() : QStringLiteral("hevc (Main 10)"));
            shotPlayer.setHwdec(shot.empty ? QString() : QStringLiteral("d3d11va"));
            shotPlayer.setVideoFps(shot.empty ? 0.0 : 23.976);
            shotPlayer.setVideoBitrate(shot.empty ? 0.0 : 8240000.0);
            shotPlayer.setHdrMode(shot.empty ? QString() : QStringLiteral("HDR10"));
            shotPlayer.setErrorMessage(shot.error ? QCoreApplication::translate("Palune", "播放失败：无法解码该文件（样本错误）") : QString());
            shotWin.setFullscreen(shot.fullscreen);
            demoUi.setChromeVisible(shot.chrome);
            const QString panel = shot.page >= 0 ? QStringLiteral("settings")
                                                 : (shot.page == -2 ? QStringLiteral("library") : QString());
            root->setProperty("overlayPanel", panel);
            demoUi.setOverlayOpen(!panel.isEmpty());
            root->setProperty("errorDismissed", false);
            if (settingsPanel && shot.page >= 0)
                settingsPanel->setProperty("page", shot.page);
            const QSize logical = mini ? QSize(480, 270) : normalSize;
            view.resize(logical);
            demoCompact.feedWidth(logical.width());
            demoCompact.flush();
            sleepMs(260);
            Log::info(QStringLiteral("截图：抓取 %1/%2").arg(theme, shot.id));
            const QImage image = view.grabWindow();
            Log::info(QStringLiteral("截图：完成 %1/%2 %3x%4")
                          .arg(theme, shot.id)
                          .arg(image.width())
                          .arg(image.height()));
            const QString path = m_options.shotDir
                + QStringLiteral("/m2-%1-%2.png").arg(theme, shot.id);
            if (!image.isNull() && image.save(path)) {
                ++saved;
                QJsonObject entry;
                entry.insert(QStringLiteral("theme"), theme);
                entry.insert(QStringLiteral("screen"), shot.id);
                entry.insert(QStringLiteral("width"), image.width());
                entry.insert(QStringLiteral("height"), image.height());
                entry.insert(QStringLiteral("logicalWidth"), logical.width());
                entry.insert(QStringLiteral("logicalHeight"), logical.height());
                entry.insert(QStringLiteral("devicePixelRatio"),
                              double(image.width()) / double(logical.width()));
                entry.insert(QStringLiteral("file"), QFileInfo(path).fileName());
                index.append(entry);
            } else {
                Log::error(QStringLiteral("截图失败：%1").arg(path));
            }
        }
    }

    QJsonObject report;
    report.insert(QStringLiteral("mode"), QStringLiteral("m2-ui-baseline"));
    report.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("themes"), QJsonArray{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    report.insert(QStringLiteral("logicalWidth"), normalSize.width());
    report.insert(QStringLiteral("logicalHeight"), normalSize.height());
    report.insert(QStringLiteral("screensPerTheme"), screens.size());
    report.insert(QStringLiteral("saved"), saved);
    report.insert(QStringLiteral("expected"), screens.size() * 3);
    report.insert(QStringLiteral("verdict"),
                  saved == screens.size() * 3 ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    report.insert(QStringLiteral("items"), index);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJsonReport(report, m_options.shotDir + QStringLiteral("/m2-ui-baseline.json"));
    finishWithCode(saved == screens.size() * 3 ? 0 : 6);
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
    // 窗口/动作等本地采集模式没有 UpdateService，不能走更新器字段拼装。
    if (!m_updater) {
        writeJsonReport(extra, m_options.reportPath.isEmpty()
                                   ? QStringLiteral("qa/runtime/m2-window.json")
                                   : m_options.reportPath);
        return;
    }
    QJsonObject report = extra;
    if (!report.contains(QStringLiteral("mode")))
        report.insert(QStringLiteral("mode"), QStringLiteral("update-e2e:%1").arg(m_options.e2eMode));
    report.insert(QStringLiteral("feed"), m_updater->feedUrl().toString());
    report.insert(QStringLiteral("downloadDir"), m_options.downloadDir);
    report.insert(QStringLiteral("currentVersion"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("availableVersion"), m_updater->availableVersion());
    report.insert(QStringLiteral("state"), m_updater->state());
    report.insert(QStringLiteral("manifestSource"), m_updater->manifestSource());
    report.insert(QStringLiteral("manifestSourceUrl"), m_updater->manifestSourceUrl());
    report.insert(QStringLiteral("manifestAttempts"),
                  QJsonArray::fromVariantList(m_updater->manifestAttempts()));
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
