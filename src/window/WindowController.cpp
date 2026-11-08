#include "window/WindowController.h"

#include "core/AppPaths.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "media/MediaLibraryService.h"
#include "mpv/MpvClient.h"
#include "settings/Settings.h"
#include "theme/LayoutHost.h"
#include "theme/ThemeRegistry.h"
#include "ui/ActionRegistry.h"
#include "ui/CompactController.h"
#include "ui/UiStateStore.h"
#include "update/UpdateService.h"
#include "window/SettingsWindow.h"

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
    m_compactMode->feedWidth(m_overlay->width());
    if (m_settings && m_settings->windowMaximized())
        m_overlay->showMaximized();
    m_overlay->requestActivate();
    applyGeometry(r);
    applyWindowRegion();

    // 设置窗跟随主窗位置/尺寸，并在主题切换时换尺寸与圆角。
    connect(m_overlay, &QWindow::xChanged, this, &WindowController::repositionSettingsWindow);
    connect(m_overlay, &QWindow::yChanged, this, &WindowController::repositionSettingsWindow);
    connect(m_overlay, &QWindow::widthChanged, this, &WindowController::repositionSettingsWindow);
    connect(m_overlay, &QWindow::heightChanged, this, &WindowController::repositionSettingsWindow);
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
    saveResumePosition();
    saveWindowState();
    CrashBreadcrumb::record(QStringLiteral("用户关闭窗口"));
    QCoreApplication::quit();
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

void WindowController::setVideoInsets(double left, double top, double right, double bottom)
{
    m_insetLeft = qMax(0.0, left);
    m_insetTop = qMax(0.0, top);
    m_insetRight = qMax(0.0, right);
    m_insetBottom = qMax(0.0, bottom);
    // 内缩变化要把视频窗立即对齐到覆盖窗当前几何，否则会短暂露出桌面底色。
    syncVideoToOverlay();
}

void WindowController::applyWindowRegion()
{
#if defined(Q_OS_WIN)
    // 无边框 + 透明背景的窗口，DWM 不会给圆角，只能自己裁 Region；
    // 逻辑圆角按 DPR 换算成物理像素，两窗用同一半径才不会出现视频露出直角。
    const qreal dpr = m_overlay ? m_overlay->devicePixelRatio() : 1.0;
    const int logical = (m_fullscreen || m_maximized) ? 0 : m_cornerRadius;
    const int radius = qRound(logical * dpr);
    auto applyRegion = [radius](qintptr handle) {
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
    const QString file = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("打开媒体文件"), QString(),
        QStringLiteral("媒体文件 (*.mp4 *.mkv *.mov *.webm *.avi *.ts *.flv *.m4a *.mp3 *.flac *.wav);;所有文件 (*)"));
    if (!file.isEmpty())
        openFile(QUrl::fromLocalFile(file));
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
    openFile(QUrl::fromLocalFile(media.first()));
}

void WindowController::openRecentFile(const QString &path)
{
    if (!path.isEmpty())
        openFile(QUrl::fromLocalFile(path));
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
        playSibling(1);
    } else if (name == QLatin1String("prevEpisode")) {
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
        // 交给 QObject 树管理生命周期（窗口层级仍由 transient parent 决定，互不裁剪）。
        static_cast<QObject *>(m_settingsWindow)->setParent(this);
    }
    m_settingsWindow->open(m_overlay->geometry());
}

void WindowController::closeSettingsWindow()
{
    if (!m_settingsWindow)
        return;
    m_settingsWindow->hide();
    if (m_overlay)
        m_overlay->requestActivate();
}

void WindowController::repositionSettingsWindow()
{
    if (m_settingsWindow && m_settingsWindow->isOpen() && m_overlay)
        m_settingsWindow->reposition(m_overlay->geometry());
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
    openFile(QUrl::fromLocalFile(media.at(next)));
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
    m_pendingResume = m_settings ? m_settings->resumePosition(path) : -1.0;
    if (m_pendingResume <= 3.0)
        m_pendingResume = -1.0;
    if (!m_player->openFile(path)) {
        Log::error(QStringLiteral("打开文件失败：%1").arg(path));
        return;
    }
    Log::info(QStringLiteral("开始播放：%1").arg(path));
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
