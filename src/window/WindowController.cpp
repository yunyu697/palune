#include "window/WindowController.h"

#include "diag/Log.h"
#include "mpv/MpvClient.h"
#include "update/UpdateService.h"

#include <QCoreApplication>
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

WindowController::WindowController(MpvClient *player, UpdateService *updater, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_updater(updater)
{
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
    m_video = new VideoWindow();
    m_overlay = new OverlayWindow();

    if (!loadQml()) {
        Log::error(QStringLiteral("QML 加载失败，无法启动窗口"));
        return false;
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    QRect r(available.center() - QPoint(width / 2, height / 2), QSize(width, height));
    m_normalGeometry = r;

    m_video->show();
    m_overlay->show();
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

    m_overlay->setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (m_overlay->status() != QQuickView::Ready) {
        for (const auto &error : m_overlay->errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        return false;
    }
    return true;
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
        QStringLiteral("媒体文件 (*.mp4 *.mkv *.mov *.webm *.avi *.m4a *.mp3 *.flac *.wav);;所有文件 (*)"));
    if (!file.isEmpty())
        openFile(QUrl::fromLocalFile(file));
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
