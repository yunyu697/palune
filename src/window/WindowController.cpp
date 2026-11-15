#include "window/WindowController.h"

#include "core/AppPaths.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "media/MediaLibraryService.h"
#include "mpv/MpvClient.h"
#include "settings/Preferences.h"
#include "settings/Settings.h"
#include "theme/LayoutHost.h"
#include "theme/ThemeRegistry.h"
#include "ui/ActionRegistry.h"
#include "ui/CompactController.h"
#include "ui/PlayQueue.h"
#include "ui/UiStateStore.h"
#include "update/UpdateService.h"
#include "window/SettingsWindow.h"
#include "window/SystemChrome.h"

#include <QCoreApplication>
#include <QDir>
#include <functional>
#include <algorithm>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

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

bool isSubtitleFile(const QFileInfo &info)
{
    static const QStringList suffixes = {
        QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"),
        QStringLiteral("vtt"), QStringLiteral("sub"), QStringLiteral("idx"),
        QStringLiteral("sup")
    };
    return info.isFile() && suffixes.contains(info.suffix().toLower());
}

QStringList expandMediaPaths(const QVariantList &urls, QVariantList *sidecarSubtitles)
{
    QStringList media;
    for (const QVariant &value : urls) {
        const QUrl url = value.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : value.toString();
        if (path.isEmpty())
            continue;
        const QFileInfo info(path);
        if (info.isDir()) {
            const QFileInfoList entries = QDir(path).entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo &entry : entries) {
                if (isMediaFile(entry))
                    media << entry.absoluteFilePath();
            }
        } else if (isSubtitleFile(info) && sidecarSubtitles) {
            sidecarSubtitles->append(info.absoluteFilePath());
        } else if (path.contains(QStringLiteral("://"))) {
            media << path;
        } else if (info.exists() || isMediaFile(info)) {
            media << info.absoluteFilePath();
        }
    }
    return media;
}
} // namespace

WindowController::WindowController(MpvClient *player,
                                   UpdateService *updater,
                                   Settings *settings,
                                   MediaLibraryService *library,
                                   QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_updater(updater)
    , m_settings(settings)
    , m_library(library)
{
    m_themeRegistry = new ThemeRegistry(m_settings, {}, this);
    m_cornerRadius = ThemeRegistry::windowRadius(m_themeRegistry->currentTheme());
    m_uiState = new UiStateStore(m_settings, this);
    m_compactMode = new CompactController(m_settings, this);
    m_actions = new ActionRegistry(this);
    m_actions->setInvoker([this](const QString &id, const QVariant &value) { action(id, value); });
    m_preferences = new Preferences(m_settings, m_player, m_updater, m_compactMode, m_actions, this);
    // 播放队列独立于「最近打开」：自动持久化到 configDir/queue.json。
    m_queue = new PlayQueue(this);
    m_queue->setPersistPath(QDir(AppPaths::configDir()).filePath(QStringLiteral("queue.json")));
    connect(m_queue, &PlayQueue::playRequested, this, &WindowController::playQueueIndex);
    m_queue->load(m_queue->persistPath());
    // 本地媒体库：根目录与索引来自 settings.toml / configDir/library/index.json。
    if (m_library) {
        m_library->setSettings(m_settings);
        m_library->load();
    }
    m_compactMode->setTheme(m_themeRegistry->currentTheme());
    connect(m_themeRegistry, &ThemeRegistry::currentThemeChanged, this, [this]() {
        m_compactMode->setTheme(m_themeRegistry->currentTheme());
        const int radius = ThemeRegistry::windowRadius(m_themeRegistry->currentTheme());
        if (radius != m_cornerRadius) {
            m_cornerRadius = radius;
            emit windowRadiusChanged();
        }
        applyWindowRegion();
        if (m_overlay)
            m_compactMode->feedWidth(m_overlay->width());
    });

    // 播放位置记忆：每秒落盘一次，强杀后按文件恢复；fileLoaded 后再 seek 避免加载中被丢弃。
    if (m_player) {
        connect(m_player, &MpvClient::fileLoaded, this, [this]() {
            if (m_pendingResume > 3.0 && m_player) {
                m_player->seek(m_pendingResume);
                Log::info(QStringLiteral("续播到 %1s").arg(m_pendingResume, 0, 'f', 1));
            }
            m_pendingResume = -1.0;
            // 「启动时自动播放」关闭时，文件加载完成后停在暂停态。
            if (m_preferences && !m_preferences->autoPlay() && m_player && !m_player->paused())
                m_player->setPaused(true);
        });
        // 「自动播放下一集」开启时，优先走播放队列；没有队列才回退到同目录顺序。
        connect(m_player, &MpvClient::endFile, this, [this](const QString &reason) {
            if (!m_preferences || !m_preferences->autoNext() || reason != QLatin1String("eof"))
                return;
            if (m_queue && m_queue->count() > 1) {
                const int next = m_queue->currentIndex() + 1;
                if (next < m_queue->count())
                    m_queue->setCurrentIndex(next);
                return;
            }
            playSibling(1);
        });
        connect(m_player, &MpvClient::positionChanged, this, [this]() {
            const double pos = m_player ? m_player->position() : 0.0;
            if (pos <= 3.0)
                return;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastResumeSaveMs < 1000)
                return;
            m_lastResumeSaveMs = now;
            saveResumePosition();
            // 媒体服务器条目：每 10 秒把播放位置回写给服务器。
            if (m_library && m_player->filePath().contains(QStringLiteral("://"))
                && now - m_lastRemoteReportMs >= 10000) {
                m_lastRemoteReportMs = now;
                m_library->reportRemoteProgress(m_player->filePath(), pos);
            }
        });
    }
}

void WindowController::saveResumePosition()
{
    if (!m_player || !m_settings)
        return;
    const QString path = m_player->filePath();
    if (path.isEmpty())
        return;
    m_settings->setResumePosition(path, m_player->position());
    m_settings->save();
}

WindowController::~WindowController()
{
    // 退出时给远端条目补停止事件，服务器才能落下最终播放位置。
    if (m_library && m_player && !m_player->filePath().isEmpty()
        && m_player->filePath().contains(QStringLiteral("://"))) {
        m_library->reportRemoteProgress(m_player->filePath(), m_player->position(), true);
    }
    delete m_settingsWindow;
    m_settingsWindow = nullptr;
    delete m_overlay;
    m_overlay = nullptr;
    delete m_video;
    m_video = nullptr;
}

bool WindowController::start(int width, int height)
{
    QElapsedTimer step;
    step.start();
    // 设计稿 §5.2：Capsule 的设计基准是 1100×680（1180×700 是 A/B 的基准）。
    // 只影响「首次启动/没有保存几何」的情况，用户已保存的几何优先级更高。
    if (m_themeRegistry && m_themeRegistry->currentTheme() == QLatin1String("C")) {
        width = 1100;
        height = 680;
    }
    m_video = new VideoWindow();
    m_overlay = new OverlayWindow();

    if (!loadQml()) {
        Log::error(QStringLiteral("QML 加载失败，无法启动窗口"));
        return false;
    }
    Log::debug(QStringLiteral("启动计时：QML 加载完成 %1ms").arg(step.elapsed()));

    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    QRect r(available.center() - QPoint(width / 2, height / 2), QSize(width, height));
    // 恢复上次窗口几何；越界（比如外接屏拔掉）时回退到主屏居中。
    if (m_settings && m_settings->hasWindowGeometry()) {
        const QRect saved = m_settings->windowGeometry();
        if (available.intersects(saved) && saved.width() >= 480 && saved.height() >= 320)
            r = saved;
    }
    m_normalGeometry = r;

    m_video->show();
    Log::debug(QStringLiteral("启动计时：视频窗口显示 %1ms").arg(step.elapsed()));
    m_overlay->show();
    Log::debug(QStringLiteral("启动计时：覆盖窗口显示 %1ms").arg(step.elapsed()));
    connect(m_overlay, &QWindow::widthChanged, this, [this]() {
        if (m_compactMode && m_overlay)
            m_compactMode->feedWidth(m_overlay->width());
    });
    // 用户用系统拖拽/边缘缩放改变覆盖窗后，视频窗必须跟随，否则画面和 UI 会错位。
    connect(m_overlay, &QWindow::xChanged, this, &WindowController::syncVideoToOverlay);
    connect(m_overlay, &QWindow::yChanged, this, &WindowController::syncVideoToOverlay);
    connect(m_overlay, &QWindow::widthChanged, this, &WindowController::syncVideoToOverlay);
    connect(m_overlay, &QWindow::heightChanged, this, &WindowController::syncVideoToOverlay);
    // 失焦再获焦 / 最小化恢复：视频窗与覆盖窗是**两个同级顶层窗口**。
    // 用户切到其它窗口时，那个窗口会被插到两者之间；切回播放器时只有覆盖窗被抬到最前，
    // 视频窗仍留在其它窗口下面，而覆盖窗在视频区域是透明的 —— 于是「上一个窗口的画面
    // 残留在播放器上」（暂停态最明显，因为画面不变）。
    // 修复：每次重新激活时把视频窗重新插回覆盖窗正下方（SetWindowPos 的第二参数即 z 序锚点），
    // 并补一次同位置 seek，确保暂停态的视频 swapchain 重新提交当前帧。
    connect(m_overlay, &QWindow::activeChanged, this, [this]() {
        if (!m_overlay || !m_overlay->isActive())
            return;
        syncVideoToOverlay();
        if (m_player && m_player->paused())
            m_player->refreshFrame();
    });
    connect(m_overlay, &QWindow::visibilityChanged, this, [this]() {
        if (!m_overlay || !m_overlay->isVisible() || m_overlay->visibility() == QWindow::Minimized)
            return;
        syncVideoToOverlay();
        if (m_player && m_player->paused())
            m_player->refreshFrame();
    });
    m_compactMode->feedWidth(m_overlay->width());
    // 双窗分层看护。覆盖窗是透明的，视频由下方的独立顶层窗提供；任何一次
    // 系统重排（遮挡恢复、Alt+Tab、贴边/最大化、跨屏、DPI、休眠唤醒）都可能有
    // 其它窗口被插到两窗之间，此时透明区域就会漏出桌面或上一个窗口的内容，
    // 只剩一层 QML UI 浮在上面。这里在原生消息之外再加一层低频巡检兜底，
    // 并且每次修正后补一帧重绘，不依赖「动一下鼠标就恢复」这种副作用。
    if (!m_layerWatchdog) {
        m_layerWatchdog = new QTimer(this);
        m_layerWatchdog->setInterval(400);
        m_layerWatchdog->setTimerType(Qt::CoarseTimer);
        connect(m_layerWatchdog, &QTimer::timeout, this, [this]() { enforceLayering(); });
        m_layerWatchdog->start();
    }
    connect(m_overlay, &OverlayWindow::nativeStateChanged, this, [this]() { enforceLayering(); });
    connect(m_overlay, &QWindow::screenChanged, this, [this]() {
        syncVideoToOverlay();
        if (m_player && m_player->paused() && !m_player->filePath().isEmpty())
            m_player->refreshFrame();
    });
    // 先按恢复几何摆好两窗，再最大化；顺序反过来会被 applyGeometry 的 SetWindowPos
    // 覆盖回普通尺寸（实测「设置 startMaximized 后启动仍是小窗」）。
    applyGeometry(r);
    applyWindowRegion();
    if (m_settings && (m_settings->windowMaximized() || (m_preferences && m_preferences->startMaximized()))) {
        // 同步内部状态，否则启动即最大化时再点「最大化」会变成“再次最大化”，无法还原。
        m_restoreGeometry = r;
        m_maximized = true;
        m_overlay->showMaximized();
    }
    m_overlay->requestActivate();
    syncVideoToOverlay();
    // 播放器属性（硬解/缓存/字幕/音频/通道）在 mpv 建好之后统一作用一次。
    if (m_preferences)
        m_preferences->applyAll();

    // 设置窗是独立顶层窗口：不再随主窗移动/缩放（只在切换主题时换自身尺寸与圆角）。
    if (m_themeRegistry) {
        connect(m_themeRegistry, &ThemeRegistry::currentThemeChanged, this, [this]() {
            if (m_settingsWindow && m_settingsWindow->isOpen() && m_overlay)
                m_settingsWindow->refreshForTheme(m_overlay->geometry());
        });
    }

    if (m_player && m_player->available())
        m_player->setVideoWindow(m_video->videoSurfaceHandle());

    connect(m_overlay, &OverlayWindow::closed, this, &WindowController::close);
    if (m_player) {
        connect(m_player, &MpvClient::titleChanged, this, &WindowController::updateTitle);
        connect(m_player, &MpvClient::filePathChanged, this, &WindowController::updateTitle);
        connect(m_player, &MpvClient::filePathChanged, this, [this]() {
            m_video->setVideoSurfaceVisible(!m_player->filePath().isEmpty());
        });
        m_video->setVideoSurfaceVisible(false);
    }

    m_ready = true;
    CrashBreadcrumb::record(QStringLiteral("主窗口已创建"));
    Log::info(QStringLiteral("主窗口已创建：%1x%2").arg(width).arg(height));
    return true;
}

bool WindowController::loadQml()
{
    if (!m_overlay)
        return false;
    auto *ctx = m_overlay->engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("player"), m_player);
    ctx->setContextProperty(QStringLiteral("updater"), m_updater);
    ctx->setContextProperty(QStringLiteral("win"), this);
    ctx->setContextProperty(QStringLiteral("themeRegistry"), m_themeRegistry);
    ctx->setContextProperty(QStringLiteral("uiState"), m_uiState);
    ctx->setContextProperty(QStringLiteral("compactMode"), m_compactMode);
    ctx->setContextProperty(QStringLiteral("actions"), m_actions);
    ctx->setContextProperty(QStringLiteral("library"), m_library);
    ctx->setContextProperty(QStringLiteral("prefs"), m_preferences);
    ctx->setContextProperty(QStringLiteral("queue"), m_queue);

    m_overlay->setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (m_overlay->status() != QQuickView::Ready) {
        for (const auto &error : m_overlay->errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        return false;
    }

    // VideoHost 在布局树外：它由 Main.qml 的 shell 层持有，LayoutHost 只接管布局槽位。
    QQuickItem *slot = m_overlay->rootObject()
        ? m_overlay->rootObject()->findChild<QQuickItem *>(QStringLiteral("layoutSlot"))
        : nullptr;
    if (!slot) {
        Log::error(QStringLiteral("QML 中缺少 layoutSlot，布局无法挂载"));
        return false;
    }
    m_layoutHost = new LayoutHost(m_overlay->engine(), m_themeRegistry, this);
    m_layoutHost->setHostItem(slot);
    connect(m_layoutHost, &LayoutHost::layoutBuilt, this,
            [this](const QString &theme, double ms) {
                Log::info(QStringLiteral("布局构建完成：%1（%2ms）")
                              .arg(theme, QString::number(ms, 'f', 2)));
                restoreFocus();
            });
    if (m_layoutHost->layoutContainsVideoHost())
        Log::error(QStringLiteral("分层违规：VideoHost 出现在布局树内"));
    return true;
}

void WindowController::restoreFocus()
{
    if (!m_overlay || !m_uiState)
        return;
    QQuickItem *root = m_overlay->rootObject();
    if (!root)
        return;
    const QString actionId = m_uiState->focusActionId();
    if (actionId.isEmpty())
        return;
    // 布局重建后旧控件已销毁，新布局里同 actionId 的控件等价；按 ID 找回并确定性设置焦点。
    // QML 侧 Component.onCompleted + Qt.callLater 依赖事件循环与窗口激活状态，不够可靠。
    // 注意：QQmlComponent 创建的布局根没有 QObject 父对象，findChildren 找不到，
    // 必须沿 QQuickItem 的 childItems() 树递归。
    std::function<QQuickItem *(QQuickItem *)> findVisible =
        [&](QQuickItem *item) -> QQuickItem * {
            if (item->property("actionId").toString() == actionId && item->isVisible())
                return item;
            for (QQuickItem *child : item->childItems()) {
                if (QQuickItem *found = findVisible(child))
                    return found;
            }
            return nullptr;
        };
    QQuickItem *target = findVisible(root);
    if (!target) {
        Log::warn(QStringLiteral("焦点恢复失败：布局中找不到可见的动作 %1").arg(actionId));
        return;
    }
    target->forceActiveFocus(Qt::OtherFocusReason);
    Log::info(QStringLiteral("焦点恢复：%1（focus=%2，activeFocus=%3）")
                  .arg(actionId)
                  .arg(target->hasFocus() ? "true" : "false",
                       target->hasActiveFocus() ? "true" : "false"));
}

void WindowController::saveWindowState()
{
    if (!m_settings || !m_overlay)
        return;
    if (!m_fullscreen)
        m_settings->setWindowGeometry(m_overlay->geometry());
    m_settings->setWindowMaximized(m_overlay->visibility() == QWindow::Maximized);
    QString error;
    if (!m_settings->save(&error))
        Log::warn(QStringLiteral("窗口状态保存失败：%1").arg(error));
    else
        CrashBreadcrumb::record(QStringLiteral("窗口状态已保存"));
}

void WindowController::rememberFile(const QString &path)
{
    if (!m_settings || path.isEmpty())
        return;
    m_settings->addRecentFile(path);
    QString error;
    if (!m_settings->save(&error))
        Log::warn(QStringLiteral("最近文件保存失败：%1").arg(error));
    emit recentFilesChanged();
}

QVariantList WindowController::recentFiles() const
{
    QVariantList list;
    if (!m_settings)
        return list;
    for (const QString &path : m_settings->recentFiles())
        list << QVariantMap{{QStringLiteral("path"), path},
                            {QStringLiteral("name"), QFileInfo(path).fileName()}};
    return list;
}

QString WindowController::version() const
{
    return QStringLiteral(PALUNE_VERSION);
}

QString WindowController::configDir() const
{
    return AppPaths::configDir();
}

QString WindowController::crashDir() const
{
    return AppPaths::crashDir();
}

QString WindowController::logFile() const
{
    return AppPaths::logFile();
}

void WindowController::updateTitle()
{
#ifdef Q_OS_WIN
    const QString wide = QStringLiteral("Palune — ") + m_player->title();
    SetWindowTextW(reinterpret_cast<HWND>(m_overlay->winId()),
                   reinterpret_cast<LPCWSTR>(wide.utf16()));
#endif
    m_windowTitle = m_player->title().isEmpty() ? QStringLiteral("Palune")
                                                : QStringLiteral("Palune — %1").arg(m_player->title());
    emit windowTitleChanged();
}

ApplyResult WindowController::applyGeometry(const QRect &rect)
{
    ApplyResult result;
    QElapsedTimer timer;
    timer.start();
#ifdef Q_OS_WIN
    // 直接用 SetWindowPos 连续调整两窗，避免 QQuickView::setGeometry 的同步重排拉大两窗几何差；
    // 同时把视频窗放到覆盖窗下方，保证 UI 始终在视频画面之上。
    // 注意：SetWindowPos 收的是**物理像素**，而 Qt 的 QRect 是**逻辑像素**；
    // 4K@200% 下若直接传逻辑值，窗口只有一半大小，QML 会按完整布局渲染并被裁掉
    // （实测表现就是「UI 显示不全 + 进度条/按钮被截断」）。
    HWND videoHwnd = m_video ? reinterpret_cast<HWND>(m_video->winId()) : nullptr;
    HWND overlayHwnd = m_overlay ? reinterpret_cast<HWND>(m_overlay->winId()) : nullptr;
    const qreal dpr = m_overlay ? m_overlay->devicePixelRatio() : 1.0;
    // 视频区可以按布局要求内缩（B 的 344px 检查器）：覆盖窗仍是整窗，视频窗只占主区。
    const QRect videoRect(rect.x() + qRound(m_insetLeft), rect.y() + qRound(m_insetTop),
                          qMax(0, rect.width() - qRound(m_insetLeft) - qRound(m_insetRight)),
                          qMax(0, rect.height() - qRound(m_insetTop) - qRound(m_insetBottom)));
    const QRect native(qRound(rect.x() * dpr), qRound(rect.y() * dpr),
                       qRound(rect.width() * dpr), qRound(rect.height() * dpr));
    const QRect videoNative(qRound(videoRect.x() * dpr), qRound(videoRect.y() * dpr),
                            qRound(videoRect.width() * dpr), qRound(videoRect.height() * dpr));
    if (videoHwnd) {
        SetWindowPos(videoHwnd, overlayHwnd, videoNative.x(), videoNative.y(),
                     videoNative.width(), videoNative.height(), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (overlayHwnd) {
        SetWindowPos(overlayHwnd, nullptr, native.x(), native.y(), native.width(),
                     native.height(), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (videoHwnd && overlayHwnd)
        result.overlayAbove = GetWindow(videoHwnd, GW_HWNDPREV) == overlayHwnd;
    applyWindowRegion();
#else
    if (m_video)
        m_video->setGeometry(rect);
    if (m_overlay)
        m_overlay->setGeometry(rect);
#endif
    result.syncMicros = timer.nsecsElapsed() / 1000;
    m_lastSyncMicros = result.syncMicros;

    // 用原生窗口矩形做对比：SetWindowPos 之后 Qt 的 geometry() 缓存尚未同步，
    // 直接读会得到陈旧值，产生假偏移。
#ifdef Q_OS_WIN
    auto nativeRect = [](qintptr handle) {
        RECT rc{};
        GetWindowRect(reinterpret_cast<HWND>(handle), &rc);
        return QRect(QPoint(rc.left, rc.top), QSize(rc.right - rc.left, rc.bottom - rc.top));
    };
    if (videoHwnd && overlayHwnd) {
        const QRect v = nativeRect(reinterpret_cast<qintptr>(videoHwnd));
        const QRect o = nativeRect(reinterpret_cast<qintptr>(overlayHwnd));
        result.dx = qAbs(v.x() - o.x());
        result.dy = qAbs(v.y() - o.y());
        result.dw = qAbs(v.width() - o.width());
        result.dh = qAbs(v.height() - o.height());
    }
#endif
#ifndef Q_OS_WIN
    if (m_video && m_overlay) {
        const QRect v = m_video->geometry();
        const QRect o = m_overlay->geometry();
        result.dx = qAbs(v.x() - o.x());
        result.dy = qAbs(v.y() - o.y());
        result.dw = qAbs(v.width() - o.width());
        result.dh = qAbs(v.height() - o.height());
    }
#endif
    return result;
}

ApplyResult WindowController::setFullscreenForTest(bool on)
{
    if (on) {
        if (!m_fullscreen)
            m_normalGeometry = m_overlay->geometry();
        m_fullscreen = true;
        QScreen *screen = m_overlay ? m_overlay->screen() : nullptr;
        const QRect target = screen ? screen->geometry() : QGuiApplication::primaryScreen()->geometry();
        ApplyResult r = applyGeometry(target);
        emit fullscreenChanged();
        return r;
    }
    m_fullscreen = false;
    ApplyResult r = applyGeometry(m_normalGeometry);
    emit fullscreenChanged();
    return r;
}

ApplyResult WindowController::toggleFullscreenForTest()
{
    return setFullscreenForTest(!m_fullscreen);
}

ApplyResult WindowController::restoreForTest(bool minimizeWindow)
{
    if (minimizeWindow) {
        m_minimized = true;
        if (m_overlay)
            m_overlay->showMinimized();
        if (m_video)
            m_video->hide();
        return {};
    }
    m_minimized = false;
    QRect target = m_normalGeometry;
    if (m_video)
        m_video->show();
    if (m_overlay) {
        m_overlay->showNormal();
        target = m_overlay->geometry();
    }
    ApplyResult r = applyGeometry(target);
    activate();
    return r;
}

void WindowController::setFullscreen(bool on)
{
    setFullscreenForTest(on);
}

void WindowController::toggleFullscreen()
{
    setFullscreen(!m_fullscreen);
}

void WindowController::minimize()
{
    restoreForTest(true);
}

void WindowController::restore()
{
    restoreForTest(false);
}

void WindowController::activate()
{
    if (m_overlay) {
        m_overlay->raise();
        m_overlay->requestActivate();
    }
}

void WindowController::close()
{
    // 「关闭到托盘」：只隐藏两个窗口，进程与播放器状态保留在托盘。
    if (!m_forceQuit && m_preferences && m_preferences->closeToTray()
        && QSystemTrayIcon::isSystemTrayAvailable()) {
        m_minimized = true;
        if (m_overlay)
            m_overlay->hide();
        if (m_video)
            m_video->hide();
        CrashBreadcrumb::record(QStringLiteral("关闭到托盘"));
        Log::info(QStringLiteral("关闭到托盘：窗口已隐藏"));
        return;
    }
    if (!m_forceQuit && m_preferences && m_preferences->confirmExit()) {
        const auto reply = QMessageBox::question(nullptr, QStringLiteral("退出 Palune"),
                                                 QStringLiteral("确定要退出 Palune 吗？"));
        if (reply != QMessageBox::Yes)
            return;
    }
    if (m_preferences)
        m_preferences->saveSessionState();
    saveResumePosition();
    saveWindowState();
    CrashBreadcrumb::record(QStringLiteral("用户关闭窗口"));
    QCoreApplication::quit();
}

void WindowController::quitApplication()
{
    m_forceQuit = true;
    close();
}

void WindowController::moveBy(double dx, double dy)
{
    if (!m_overlay || !m_video || m_fullscreen)
        return;
    const QRect o = m_overlay->geometry();
    applyGeometry(QRect(o.topLeft() + QPoint(qRound(dx), qRound(dy)), o.size()));
}

void WindowController::startSystemMove()
{
    // 无边框窗口自己算位移会丢 Snap、抖动且跨 DPI 会漂移；交给系统移动循环。
    if (m_overlay && !m_fullscreen)
        m_overlay->startSystemMove();
}

void WindowController::startSystemResize(int edges)
{
    if (m_overlay && !m_fullscreen)
        m_overlay->startSystemResize(Qt::Edges(static_cast<Qt::Edge>(edges)));
}

void WindowController::toggleMaximize()
{
    if (!m_overlay || m_fullscreen)
        return;
    if (m_maximized) {
        m_maximized = false;
        applyGeometry(m_restoreGeometry.isEmpty() ? m_normalGeometry : m_restoreGeometry);
    } else {
        m_restoreGeometry = m_overlay->geometry();
        m_maximized = true;
        QScreen *screen = m_overlay->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        applyGeometry(screen ? screen->availableGeometry() : m_restoreGeometry);
    }
    emit maximizedChanged();
}

void WindowController::minimizeWindow()
{
    if (!m_overlay)
        return;
    m_minimized = true;
    if (m_preferences && m_preferences->minimizeToTray()
        && QSystemTrayIcon::isSystemTrayAvailable()) {
        m_overlay->hide();
        if (m_video)
            m_video->hide();
        Log::info(QStringLiteral("最小化到托盘：窗口已隐藏"));
        return;
    }
    m_overlay->showMinimized();
    if (m_video)
        m_video->hide();
}

void WindowController::syncVideoToOverlay()
{
    if (m_syncing || !m_overlay || !m_video || m_minimized)
        return;
    m_syncing = true;
#if defined(Q_OS_WIN)
    // 用户拖拽时 Qt 缓存的 geometry 可能还没刷新，直接读原生矩形最稳，
    // 否则 SetWindowPos 会把窗口“吸回”旧位置（实机表现就是拖不动）。
    HWND videoHwnd = reinterpret_cast<HWND>(m_video->winId());
    HWND overlayHwnd = reinterpret_cast<HWND>(m_overlay->winId());
    if (videoHwnd && overlayHwnd) {
        RECT rc{};
        GetWindowRect(overlayHwnd, &rc);
        const qreal dpr = m_overlay->devicePixelRatio();
        const int left = qRound(m_insetLeft * dpr);
        const int top = qRound(m_insetTop * dpr);
        const int right = qRound(m_insetRight * dpr);
        const int bottom = qRound(m_insetBottom * dpr);
        const int w = qMax(0, (rc.right - rc.left) - left - right);
        const int h = qMax(0, (rc.bottom - rc.top) - top - bottom);
        SetWindowPos(videoHwnd, overlayHwnd, rc.left + left, rc.top + top, w, h,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        applyWindowRegion();
    }
#else
    applyGeometry(m_overlay->geometry());
#endif
    m_syncing = false;
}

void WindowController::enforceLayering()
{
#if defined(Q_OS_WIN)
    if (!m_overlay || !m_video || m_minimized)
        return;
    if (!m_overlay->isVisible() || m_overlay->visibility() == QWindow::Minimized)
        return;
    HWND videoHwnd = reinterpret_cast<HWND>(m_video->winId());
    HWND overlayHwnd = reinterpret_cast<HWND>(m_overlay->winId());
    if (!videoHwnd || !overlayHwnd)
        return;
    // GW_HWNDPREV 就是 z 序里的上一层：等于覆盖窗说明视频窗仍紧贴其下方。
    if (GetWindow(videoHwnd, GW_HWNDPREV) == overlayHwnd)
        return;
    SetWindowPos(videoHwnd, overlayHwnd, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // 重新插入 z 序会重建合成层：暂停态 mpv 不产新帧，必须重新提交当前帧，
    // 否则视频窗恢复为「未呈现」而透明区域继续漏出下层内容。
    if (m_player && m_player->paused() && !m_player->filePath().isEmpty())
        m_player->refreshFrame();
    if (m_overlay)
        m_overlay->requestRepaint();
    Log::info(QStringLiteral("双窗分层已修正：视频窗重新插入覆盖窗正下方"));
#endif
}

void WindowController::setVideoInsets(double left, double top, double right, double bottom)
{
    m_insetLeft = qMax(0.0, left);
    m_insetTop = qMax(0.0, top);
    m_insetRight = qMax(0.0, right);
    m_insetBottom = qMax(0.0, bottom);
    // 内缩变化要把视频窗立即对齐到覆盖窗当前几何，否则会短暂露出桌面底色。
    syncVideoToOverlay();
}

void WindowController::setWindowFrameColor(const QColor &color)
{
    if (!color.isValid() || color == m_frameColor)
        return;
    m_frameColor = color;
    // 边框色变了要重新下发系统窗框（DWM 属性不会自动跟随主题）。
    applyWindowRegion();
}

void WindowController::applyWindowRegion()
{
#if defined(Q_OS_WIN)
    // 无边框 + 透明背景的窗口，DWM 不会给圆角，只能自己裁 Region；
    // 逻辑圆角按 DPR 换算成物理像素，两窗用同一半径才不会出现视频露出直角。
    const qreal dpr = m_overlay ? m_overlay->devicePixelRatio() : 1.0;
    const int logical = (m_fullscreen || m_maximized) ? 0 : m_cornerRadius;
    const int radius = qRound(logical * dpr);
    auto applyRegion = [this, logical, radius](qintptr handle) {
        HWND hwnd = reinterpret_cast<HWND>(handle);
        if (!hwnd)
            return;
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2);
        if (SetWindowRgn(hwnd, region, TRUE) == 0)
            DeleteObject(region);
        // 系统窗框：圆角偏好 + 暗色模式 + 主题边框色一起下发（见 SystemChrome.h）。
        applySystemChrome(handle, m_frameColor, logical);
    };
    if (m_overlay)
        applyRegion(m_overlay->winId());
    if (m_video)
        applyRegion(m_video->winId());
#else
    Q_UNUSED(m_cornerRadius)
#endif
}

void WindowController::openFileDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, QStringLiteral("打开媒体文件"), QString(),
        QStringLiteral("媒体文件 (*.mp4 *.mkv *.mov *.webm *.avi *.ts *.m2ts *.flv *.mpg *.mpeg *.m4a *.mp3 *.flac *.wav);;所有文件 (*)"));
    if (files.isEmpty())
        return;
    QVariantList urls;
    for (const QString &file : files)
        urls.append(QUrl::fromLocalFile(file));
    m_queue->setUrls(urls, 0);
    beginQueuePlayback(files.first());
}

void WindowController::openFolderDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("打开媒体文件夹"));
    if (dir.isEmpty())
        return;
    QDir folder(dir);
    QFileInfoList entries = folder.entryInfoList(QDir::Files, QDir::Name);
    QStringList media;
    for (const QFileInfo &entry : entries) {
        if (isMediaFile(entry))
            media << entry.absoluteFilePath();
    }
    if (media.isEmpty()) {
        Log::warn(QStringLiteral("文件夹中没有可播放的媒体：%1").arg(dir));
        emit uiActionRequested(QStringLiteral("notice"),
                               QStringLiteral("文件夹中没有可播放的媒体文件"));
        return;
    }
    CrashBreadcrumb::record(QStringLiteral("打开文件夹：%1（%2 个媒体）").arg(dir).arg(media.size()));
    QVariantList urls;
    for (const QString &file : media)
        urls.append(QUrl::fromLocalFile(file));
    m_queue->setUrls(urls, 0);
    beginQueuePlayback(media.first());
}

void WindowController::openRecentFile(const QString &path)
{
    if (!path.isEmpty())
        openFile(QUrl::fromLocalFile(path));
}

void WindowController::handleDrop(const QVariantList &urls)
{
    QVariantList subtitles;
    const QStringList media = expandMediaPaths(urls, &subtitles);
    for (const QVariant &subtitle : subtitles)
        m_player->addSubtitleFile(subtitle.toString());
    if (media.isEmpty()) {
        if (!subtitles.isEmpty()) {
            emit uiActionRequested(QStringLiteral("notice"), QStringLiteral("已加载外挂字幕"));
        } else {
            emit uiActionRequested(QStringLiteral("notice"), QStringLiteral("拖入的内容里没有可播放的媒体"));
        }
        return;
    }
    const bool idle = !m_player || m_player->filePath().isEmpty();
    QVariantList queueUrls;
    for (const QString &path : media)
        queueUrls.append(path);
    if (idle) {
        m_queue->setUrls(queueUrls, 0);
        beginQueuePlayback(media.first());
        return;
    }
    // 正在播放时只追加，不打断当前播放；这与「拖放替换当前播放」的旧行为相反。
    const int first = m_queue->append(queueUrls);
    if (first >= 0) {
        emit uiActionRequested(QStringLiteral("notice"),
                               QStringLiteral("已加入队列：%1 个文件").arg(media.size()));
    }
}

void WindowController::openUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return;
    if (!trimmed.contains(QStringLiteral("://"))) {
        emit uiActionRequested(QStringLiteral("notice"),
                               QStringLiteral("地址需要带协议前缀，例如 https:// 或 rtsp://"));
        return;
    }
    m_queue->setUrls(QVariantList{trimmed}, 0);
    beginQueuePlayback(trimmed);
}

void WindowController::appendFilesDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, QStringLiteral("添加到播放队列"), QString(),
        QStringLiteral("媒体文件 (*.mp4 *.mkv *.mov *.webm *.avi *.ts *.m2ts *.flv *.mpg *.mpeg *.m4a *.mp3 *.flac *.wav);;所有文件 (*)"));
    if (files.isEmpty())
        return;
    QVariantList urls;
    for (const QString &file : files)
        urls.append(file);
    const int first = m_queue->append(urls);
    const bool idle = !m_player || m_player->filePath().isEmpty();
    if (idle && first >= 0)
        m_queue->setCurrentIndex(first);
    else
        emit uiActionRequested(QStringLiteral("notice"),
                               QStringLiteral("已加入队列：%1 个文件").arg(files.size()));
}

void WindowController::saveQueueDialog()
{
    if (m_queue->empty()) {
        emit uiActionRequested(QStringLiteral("notice"), QStringLiteral("播放队列为空，无需保存"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("保存播放队列"),
        QDir(AppPaths::configDir()).filePath(QStringLiteral("queue-export.json")),
        QStringLiteral("播放队列 (*.json)"));
    if (path.isEmpty())
        return;
    if (m_queue->saveTo(path))
        emit uiActionRequested(QStringLiteral("notice"), QStringLiteral("队列已保存"));
}

void WindowController::loadQueueDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("载入播放队列"), QString(),
        QStringLiteral("播放队列 (*.json);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    if (m_queue->loadFrom(path)) {
        emit uiActionRequested(QStringLiteral("notice"),
                               QStringLiteral("队列已载入：%1 个文件").arg(m_queue->count()));
    }
}

void WindowController::playQueueIndex(int index)
{
    if (!m_queue || index < 0 || index >= m_queue->count())
        return;
    const QString path = m_queue->items()
                             .at(index)
                             .toMap()
                             .value(QStringLiteral("path"))
                             .toString();
    if (!path.isEmpty())
        beginQueuePlayback(path);
}

void WindowController::beginQueuePlayback(const QString &path)
{
    if (path.isEmpty())
        return;
    m_playingFromQueue = true;
    openFile(path.contains(QStringLiteral("://")) ? QUrl(path) : QUrl::fromLocalFile(path));
    m_playingFromQueue = false;
}

void WindowController::setAspectAndNotify(const QString &mode)
{
    if (!m_player)
        return;
    m_player->setAspectRatio(mode);
    const QString label = mode == QLatin1String("fill") ? QStringLiteral("填充")
        : (mode == QLatin1String("auto") ? QStringLiteral("自动") : mode);
    emit uiActionRequested(QStringLiteral("notice"),
                           QStringLiteral("画面比例：%1").arg(label));
}

void WindowController::action(const QString &name)
{
    action(name, QVariant());
}

void WindowController::action(const QString &name, const QVariant &value)
{
    // 布局里的旧动作 ID（seekBy/seekForward/volumeSet…）继续走 C++；
    // 只有纯 UI 动作（设置、媒体库、提示）交给 QML shell。
    static const QStringList kLegacyIds = {
        QStringLiteral("seekTo"), QStringLiteral("seekBy"), QStringLiteral("seekForward"),
        QStringLiteral("seekBack"), QStringLiteral("volumeSet"), QStringLiteral("volumeBy"),
        QStringLiteral("speedSet"), QStringLiteral("speedCycle"), QStringLiteral("audioSet"),
        QStringLiteral("subtitleSet"), QStringLiteral("setTheme"),
        QStringLiteral("openSettings"), QStringLiteral("closeSettings"),
    };
    if (!m_actions->has(name) && !kLegacyIds.contains(name)) {
        emit uiActionRequested(name, value);
        return;
    }
    if (name == QLatin1String("openFile")) {
        openFileDialog();
    } else if (name == QLatin1String("openFolder")) {
        openFolderDialog();
    } else if (name == QLatin1String("playPause")) {
        if (m_player)
            m_player->togglePause();
        CrashBreadcrumb::record(QStringLiteral("播放/暂停"));
    } else if (name == QLatin1String("stop")) {
        if (m_player)
            m_player->stop();
    } else if (name == QLatin1String("seekTo")) {
        if (m_player)
            m_player->seek(qMax(0.0, qMin(value.toDouble(), m_player->duration())));
    } else if (name == QLatin1String("seekBy") || name == QLatin1String("seekForward")
               || name == QLatin1String("seekBack")) {
        if (m_player) {
            double delta = value.toDouble();
            if (name == QLatin1String("seekForward"))
                delta = 10;
            else if (name == QLatin1String("seekBack"))
                delta = -10;
            m_player->seek(qMax(0.0, m_player->position() + delta));
        }
    } else if (name == QLatin1String("seekForward10") || name == QLatin1String("seekForward60")
               || name == QLatin1String("seekForward1")) {
        const double delta = name == QLatin1String("seekForward1") ? 1
            : (name == QLatin1String("seekForward10") ? 10 : 60);
        if (m_player)
            m_player->seek(qMax(0.0, m_player->position() + delta));
    } else if (name == QLatin1String("seekBack10") || name == QLatin1String("seekBack60")
               || name == QLatin1String("seekBack1")) {
        const double delta = name == QLatin1String("seekBack1") ? 1
            : (name == QLatin1String("seekBack10") ? 10 : 60);
        if (m_player)
            m_player->seek(qMax(0.0, m_player->position() - delta));
    } else if (name == QLatin1String("seekToStart")) {
        if (m_player)
            m_player->seek(0.0);
    } else if (name == QLatin1String("seekToEnd")) {
        if (m_player && m_player->duration() > 0.0)
            m_player->seek(m_player->duration() - 0.5);
    } else if (name == QLatin1String("frameStepForward")) {
        if (m_player)
            m_player->frameStep(true);
    } else if (name == QLatin1String("frameStepBack")) {
        if (m_player)
            m_player->frameStep(false);
    } else if (name == QLatin1String("nextEpisode")) {
        if (m_queue && m_queue->count() > 1) {
            const int next = m_queue->currentIndex() + 1;
            if (next < m_queue->count()) {
                m_queue->setCurrentIndex(next);
                return;
            }
        }
        playSibling(1);
    } else if (name == QLatin1String("prevEpisode")) {
        if (m_queue && m_queue->count() > 1) {
            const int prev = m_queue->currentIndex() - 1;
            if (prev >= 0) {
                m_queue->setCurrentIndex(prev);
                return;
            }
        }
        playSibling(-1);
    } else if (name == QLatin1String("volumeSet")) {
        if (m_player)
            m_player->setVolume(qBound(0.0, value.toDouble(), 100.0));
    } else if (name == QLatin1String("volumeBy")) {
        if (m_player)
            m_player->setVolume(qBound(0.0, m_player->volume() + value.toDouble(), 100.0));
    } else if (name == QLatin1String("volumeUp")) {
        if (m_player)
            m_player->setVolume(qBound(0.0, m_player->volume() + 5.0, 100.0));
    } else if (name == QLatin1String("volumeDown")) {
        if (m_player)
            m_player->setVolume(qBound(0.0, m_player->volume() - 5.0, 100.0));
    } else if (name == QLatin1String("toggleMute")) {
        if (m_player)
            m_player->toggleMute();
    } else if (name == QLatin1String("speedSet")) {
        if (m_player)
            m_player->setSpeed(value.toDouble());
    } else if (name == QLatin1String("speedUp")) {
        if (m_player)
            m_player->setSpeed(qMin(4.0, m_player->speed() * 1.25));
    } else if (name == QLatin1String("speedDown")) {
        if (m_player)
            m_player->setSpeed(qMax(0.25, m_player->speed() / 1.25));
    } else if (name == QLatin1String("speedReset")) {
        if (m_player)
            m_player->setSpeed(1.0);
    } else if (name == QLatin1String("speedCycle")) {
        if (m_player) {
            static const double speeds[] = {0.5, 1.0, 1.25, 1.5, 2.0, 3.0};
            int index = 1;
            for (int i = 0; i < 6; ++i) {
                if (qFuzzyCompare(m_player->speed() + 1.0, speeds[i] + 1.0)) {
                    index = (i + 1) % 6;
                    break;
                }
            }
            m_player->setSpeed(speeds[index]);
        }
    } else if (name == QLatin1String("audioSet")) {
        if (m_player)
            m_player->setAudioTrack(value.toInt());
    } else if (name == QLatin1String("subtitleSet")) {
        if (m_player)
            m_player->setSubtitleTrack(value.toInt());
    } else if (name == QLatin1String("cycleAudio")) {
        if (!m_player)
            return;
        const QVariantList tracks = m_player->audioTracks();
        if (tracks.size() > 1) {
            int current = -1;
            for (int i = 0; i < tracks.size(); ++i) {
                if (tracks.at(i).toMap().value(QStringLiteral("selected")).toBool())
                    current = i;
            }
            const int next = (current + 1) % tracks.size();
            m_player->setAudioTrack(tracks.at(next).toMap().value(QStringLiteral("id")).toInt());
        }
    } else if (name == QLatin1String("cycleSubtitle")) {
        if (!m_player)
            return;
        const QVariantList tracks = m_player->subtitleTracks();
        int current = -2;
        for (int i = 0; i < tracks.size(); ++i) {
            if (tracks.at(i).toMap().value(QStringLiteral("selected")).toBool())
                current = i;
        }
        if (tracks.isEmpty()) {
            m_player->setSubtitleTrack(-1);
        } else if (current < 0) {
            m_player->setSubtitleTrack(tracks.first().toMap().value(QStringLiteral("id")).toInt());
        } else if (current + 1 >= tracks.size()) {
            m_player->setSubtitleTrack(-1);
        } else {
            m_player->setSubtitleTrack(tracks.at(current + 1).toMap().value(QStringLiteral("id")).toInt());
        }
    } else if (name == QLatin1String("toggleSubtitle")) {
        if (m_player) {
            const QVariantList tracks = m_player->subtitleTracks();
            const bool selected = std::any_of(tracks.cbegin(), tracks.cend(), [](const QVariant &track) {
                return track.toMap().value(QStringLiteral("selected")).toBool();
            });
            if (selected || tracks.isEmpty())
                m_player->setSubtitleTrack(-1);
            else
                m_player->setSubtitleTrack(tracks.first().toMap().value(QStringLiteral("id")).toInt());
        }
    } else if (name == QLatin1String("subtitleDelayUp")) {
        if (m_player)
            m_player->addSubtitleDelay(0.1);
    } else if (name == QLatin1String("subtitleDelayDown")) {
        if (m_player)
            m_player->addSubtitleDelay(-0.1);
    } else if (name == QLatin1String("cycleAspect")) {
        static const QStringList order = {QStringLiteral("auto"), QStringLiteral("16:9"),
                                          QStringLiteral("4:3"), QStringLiteral("21:9"),
                                          QStringLiteral("1:1"), QStringLiteral("fill")};
        const QString current = m_player ? m_player->aspectRatio() : QStringLiteral("auto");
        const int index = order.indexOf(current);
        setAspectAndNotify(order.at((index + 1) % order.size()));
    } else if (name == QLatin1String("screenshot")) {
        const QString dir = m_preferences ? m_preferences->screenshotDir() : QString();
        const QString saved = m_player ? m_player->screenshotToDir(dir) : QString();
        emit uiActionRequested(QStringLiteral("notice"),
                               saved.isEmpty() ? QStringLiteral("截图失败，请检查保存目录")
                                               : QStringLiteral("截图已保存：%1").arg(saved));
        CrashBreadcrumb::record(QStringLiteral("截图：%1").arg(saved));
    } else if (name == QLatin1String("openUrl")) {
        emit uiActionRequested(QStringLiteral("openUrlDialog"), QVariant());
    } else if (name == QLatin1String("addToQueue")) {
        appendFilesDialog();
    } else if (name == QLatin1String("clearQueue")) {
        if (m_queue)
            m_queue->clear();
    } else if (name == QLatin1String("saveQueue")) {
        saveQueueDialog();
    } else if (name == QLatin1String("loadQueue")) {
        loadQueueDialog();
    } else if (name == QLatin1String("toggleFullscreen")) {
        toggleFullscreen();
    } else if (name == QLatin1String("toggleMaximize")) {
        toggleMaximize();
    } else if (name == QLatin1String("minimizeWindow")) {
        minimizeWindow();
    } else if (name == QLatin1String("windowScaleUp") || name == QLatin1String("windowScaleDown")) {
        if (m_overlay) {
            const double factor = name == QLatin1String("windowScaleUp") ? 1.25 : 0.8;
            QRect r = m_overlay->geometry();
            QSize size(qBound(480, int(r.width() * factor), 7680),
                       qBound(270, int(r.height() * factor), 4320));
            r.setSize(size);
            r.moveCenter(m_overlay->geometry().center());
            applyGeometry(r);
        }
    } else if (name == QLatin1String("toggleChrome")) {
        if (m_uiState)
            m_uiState->setChromeVisible(!m_uiState->chromeVisible());
    } else if (name == QLatin1String("themeA") || name == QLatin1String("themeB")
               || name == QLatin1String("themeC")) {
        m_themeRegistry->switchTo(name.right(1));
        CrashBreadcrumb::record(QStringLiteral("切换主题为 %1").arg(m_themeRegistry->currentTheme()));
    } else if (name == QLatin1String("cycleTheme")) {
        m_themeRegistry->cycleTheme();
        CrashBreadcrumb::record(QStringLiteral("切换主题为 %1").arg(m_themeRegistry->currentTheme()));
    } else if (name == QLatin1String("setTheme")) {
        m_themeRegistry->switchTo(value.toString());
        CrashBreadcrumb::record(QStringLiteral("切换主题为 %1").arg(m_themeRegistry->currentTheme()));
    } else if (name == QLatin1String("openSettings")) {
        openSettingsWindow();
    } else if (name == QLatin1String("closeSettings")) {
        closeSettingsWindow();
    } else if (name == QLatin1String("openRecent")) {
        emit uiActionRequested(QStringLiteral("openLibrary"), QVariant());
    } else if (name == QLatin1String("exit")) {
        close();
    } else {
        // 其余 UI 动作（设置、媒体库）交给 QML shell。
        emit uiActionRequested(name, value);
    }
}

void WindowController::openSettingsWindow()
{
    if (!m_overlay)
        return;
    if (!m_settingsWindow) {
        m_settingsWindow = new SettingsWindow(this, m_themeRegistry, m_player, m_updater,
                                              m_settings, m_compactMode, m_overlay);
        // 仅交给 QObject 树管理生命周期；窗口层级由独立的顶层窗口决定，不做瞬态从属。
        static_cast<QObject *>(m_settingsWindow)->setParent(this);
    }
    m_settingsWindow->open(m_overlay->geometry());
    // 自动化与手工排查：直接落到指定设置页（例如 PALUNE_SETTINGS_PAGE=5 打开媒体库页）。
    const QString pageText = qEnvironmentVariable("PALUNE_SETTINGS_PAGE");
    if (!pageText.isEmpty()) {
        bool ok = false;
        const int page = pageText.toInt(&ok);
        if (ok)
            m_settingsWindow->setPage(page);
    }
}

void WindowController::closeSettingsWindow()
{
    if (!m_settingsWindow)
        return;
    m_settingsWindow->hide();
    if (m_overlay)
        m_overlay->requestActivate();
}

void WindowController::playSibling(int delta)
{
    if (!m_player)
        return;
    const QFileInfo current(m_player->filePath());
    if (!current.exists())
        return;
    const QDir dir(current.absolutePath());
    QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    QStringList media;
    for (const QFileInfo &entry : entries) {
        if (isMediaFile(entry))
            media << entry.absoluteFilePath();
    }
    const int index = media.indexOf(current.absoluteFilePath());
    if (index < 0 || media.size() < 2)
        return;
    const int next = (index + delta + media.size()) % media.size();
    QVariantList urls;
    for (const QString &file : media)
        urls.append(file);
    // 同目录切换同步成队列，后续 N/P 与「自动下一集」都按这份队列走。
    m_queue->setUrls(urls, next);
    beginQueuePlayback(media.at(next));
}

void WindowController::openPath(const QString &path)
{
    if (path.isEmpty())
        return;
    openFile(path.contains(QStringLiteral("://")) ? QUrl(path) : QUrl::fromLocalFile(path));
}

void WindowController::openFile(const QUrl &url)
{
    QString path;
    if (url.isLocalFile())
        path = url.toLocalFile();
    else
        path = url.toString();
    if (path.isEmpty())
        return;
    // 切文件前先记住当前文件的进度。
    saveResumePosition();
    // 远端条目切换时补一条停止事件，服务器据此落最终位置。
    {
        const QString previous = m_player->filePath();
        if (m_library && !previous.isEmpty() && previous.contains(QStringLiteral("://")))
            m_library->reportRemoteProgress(previous, m_player->position(), true);
    }
    m_pendingResume = m_settings ? m_settings->resumePosition(path) : -1.0;
    if (m_preferences && !m_preferences->resumePlayback())
        m_pendingResume = -1.0;
    if (m_pendingResume <= 3.0)
        m_pendingResume = -1.0;
    if (!m_player->openFile(path)) {
        Log::error(QStringLiteral("打开文件失败：%1").arg(path));
        return;
    }
    Log::info(QStringLiteral("开始播放：%1").arg(path));
    if (!m_playingFromQueue && m_queue)
        m_queue->setUrls(QVariantList{path}, 0);
    rememberFile(path);
    CrashBreadcrumb::record(QStringLiteral("打开媒体：%1").arg(path));
    updateTitle();
}

qintptr WindowController::overlayHandle() const
{
    return m_overlay ? m_overlay->winId() : 0;
}

qintptr WindowController::videoHandle() const
{
    return m_video ? m_video->winId() : 0;
}

} // namespace palune
