#include "window/WindowController.h"

#include "core/AppPaths.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "media/MediaLibraryService.h"
#include "mpv/MpvClient.h"
#include "settings/Settings.h"
#include "theme/LayoutHost.h"
#include "theme/ThemeRegistry.h"
#include "ui/CompactController.h"
#include "ui/UiStateStore.h"
#include "update/UpdateService.h"

#include <QCoreApplication>
#include <QDir>
#include <functional>
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
    m_uiState = new UiStateStore(m_settings, this);
    m_compactMode = new CompactController(m_settings, this);
    m_compactMode->setTheme(m_themeRegistry->currentTheme());
    connect(m_themeRegistry, &ThemeRegistry::currentThemeChanged, this, [this]() {
        m_compactMode->setTheme(m_themeRegistry->currentTheme());
        if (m_overlay)
            m_compactMode->feedWidth(m_overlay->width());
    });
}

WindowController::~WindowController()
{
    delete m_overlay;
    m_overlay = nullptr;
    delete m_video;
    m_video = nullptr;
}

bool WindowController::start(int width, int height)
{
    QElapsedTimer step;
    step.start();
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
    m_compactMode->feedWidth(m_overlay->width());
    if (m_settings && m_settings->windowMaximized())
        m_overlay->showMaximized();
    m_overlay->requestActivate();
    applyGeometry(r);

    if (m_player && m_player->available())
        m_player->setVideoWindow(m_video->videoSurfaceHandle());

    connect(m_overlay, &OverlayWindow::closed, this, &WindowController::close);
    if (m_player) {
        connect(m_player, &MpvClient::titleChanged, this, &WindowController::updateTitle);
        connect(m_player, &MpvClient::filePathChanged, this, &WindowController::updateTitle);
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
    HWND videoHwnd = m_video ? reinterpret_cast<HWND>(m_video->winId()) : nullptr;
    HWND overlayHwnd = m_overlay ? reinterpret_cast<HWND>(m_overlay->winId()) : nullptr;
    if (videoHwnd) {
        SetWindowPos(videoHwnd, overlayHwnd, rect.x(), rect.y(), rect.width(), rect.height(),
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (overlayHwnd) {
        SetWindowPos(overlayHwnd, nullptr, rect.x(), rect.y(), rect.width(), rect.height(),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (videoHwnd && overlayHwnd)
        result.overlayAbove = GetWindow(videoHwnd, GW_HWNDPREV) == overlayHwnd;
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
    if (!m_player)
        return;
    if (name == QLatin1String("openFile")) {
        openFileDialog();
    } else if (name == QLatin1String("openFolder")) {
        openFolderDialog();
    } else if (name == QLatin1String("playPause")) {
        m_player->togglePause();
        CrashBreadcrumb::record(m_player->paused() ? QStringLiteral("暂停") : QStringLiteral("播放"));
    } else if (name == QLatin1String("seekTo")) {
        m_player->seek(qMax(0.0, qMin(value.toDouble(), m_player->duration())));
    } else if (name == QLatin1String("seekBy")) {
        m_player->seek(qMax(0.0, m_player->position() + value.toDouble()));
    } else if (name == QLatin1String("volumeSet")) {
        m_player->setVolume(qBound(0.0, value.toDouble(), 100.0));
    } else if (name == QLatin1String("volumeBy")) {
        m_player->setVolume(qBound(0.0, m_player->volume() + value.toDouble(), 100.0));
    } else if (name == QLatin1String("toggleMute")) {
        m_player->toggleMute();
    } else if (name == QLatin1String("speedSet")) {
        m_player->setSpeed(value.toDouble());
    } else if (name == QLatin1String("speedCycle")) {
        static const double speeds[] = {0.5, 1.0, 1.25, 1.5, 2.0, 3.0};
        int index = 1;
        for (int i = 0; i < 6; ++i) {
            if (qFuzzyCompare(m_player->speed() + 1.0, speeds[i] + 1.0)) {
                index = (i + 1) % 6;
                break;
            }
        }
        m_player->setSpeed(speeds[index]);
    } else if (name == QLatin1String("audioSet")) {
        m_player->setAudioTrack(value.toInt());
    } else if (name == QLatin1String("subtitleSet")) {
        m_player->setSubtitleTrack(value.toInt());
    } else if (name == QLatin1String("cycleAudio")) {
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
    } else if (name == QLatin1String("toggleFullscreen")) {
        toggleFullscreen();
    } else if (name == QLatin1String("exit")) {
        close();
    } else if (name == QLatin1String("cycleTheme")) {
        m_themeRegistry->cycleTheme();
        CrashBreadcrumb::record(QStringLiteral("切换主题为 %1").arg(m_themeRegistry->currentTheme()));
    } else if (name == QLatin1String("setTheme")) {
        m_themeRegistry->switchTo(value.toString());
        CrashBreadcrumb::record(QStringLiteral("切换主题为 %1").arg(m_themeRegistry->currentTheme()));
    } else if (name == QLatin1String("openRecent")) {
        openRecentFile(value.toString());
    } else {
        // openSettings / openLibrary 等纯 UI 动作交给 QML shell 处理。
        emit uiActionRequested(name, value);
    }
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
