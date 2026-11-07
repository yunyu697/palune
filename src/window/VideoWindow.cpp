#include "window/VideoWindow.h"

#include "diag/Log.h"

#include <QPalette>
#include <QResizeEvent>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace palune {

#if defined(Q_OS_WIN)
namespace {
// libmpv 的 w32 后端要求宿主是独立窗口；STATIC 类的背景刷子默认是白色的，
// 未出画时整块发白（实机表现为「白块」）。用自带黑底的窗口类，空闲态就是黑场。
const wchar_t *kSurfaceClass = L"PaluneVideoSurface";

void ensureSurfaceClass()
{
    static bool registered = false;
    if (registered)
        return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = kSurfaceClass;
    RegisterClassExW(&wc);
    registered = true;
}
} // namespace
#endif

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);
    setWindowTitle(QStringLiteral("Palune Video Surface"));
}

void VideoWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_surface)
        m_surface->setGeometry(rect());
#if defined(Q_OS_WIN)
    if (m_nativeSurface) {
        // 必须用原生客户区尺寸：Qt 的 width()/height() 是逻辑像素，4K@200% 下
        // 会让 mpv 渲染窗只覆盖窗口的 1/4（白块与画面偏移的根因）。
        RECT rc{};
        GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
        SetWindowPos(reinterpret_cast<HWND>(m_nativeSurface), nullptr, 0, 0, rc.right - rc.left,
                     rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}

qintptr VideoWindow::videoSurfaceHandle()
{
#if defined(Q_OS_WIN)
    if (!m_nativeSurface) {
        // 直接用 Win32 子窗承载 libmpv：Qt 的 QWidget 子窗会被 mpv 的 w32 后端拒绝创建渲染窗。
        ensureSurfaceClass();
        RECT rc = {0, 0, 0, 0};
        GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
        m_nativeSurface = reinterpret_cast<quintptr>(
            CreateWindowExW(0, kSurfaceClass, L"", WS_CHILD | WS_CLIPCHILDREN,
                            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                            reinterpret_cast<HWND>(winId()), nullptr,
                            GetModuleHandleW(nullptr), nullptr));
        Log::info(QStringLiteral("视频子窗（PaluneVideoSurface）：hwnd=%1 size=%2x%3")
                      .arg(m_nativeSurface)
                      .arg(rc.right - rc.left)
                      .arg(rc.bottom - rc.top));
        return static_cast<qintptr>(m_nativeSurface);
    }
    return static_cast<qintptr>(m_nativeSurface);
#else
    if (!m_surface) {
        m_surface = new QWidget(this);
        m_surface->setAttribute(Qt::WA_NativeWindow, true);
        m_surface->setAttribute(Qt::WA_OpaquePaintEvent, true);
        m_surface->setAutoFillBackground(true);
        QPalette p = m_surface->palette();
        p.setColor(QPalette::Window, Qt::black);
        m_surface->setPalette(p);
        m_surface->setGeometry(rect());
        m_surface->show();
    }
    return m_surface->winId();
#endif
}

void VideoWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyNativeStyles();
}

void VideoWindow::setVideoSurfaceVisible(bool visible)
{
#if defined(Q_OS_WIN)
    if (m_nativeSurface) {
        ShowWindow(reinterpret_cast<HWND>(m_nativeSurface), visible ? SW_SHOWNA : SW_HIDE);
    } else if (visible) {
        videoSurfaceHandle();
    }
#else
    if (m_surface)
        m_surface->setVisible(visible);
#endif
}

void VideoWindow::applyNativeStyles()
{
#if defined(Q_OS_WIN)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;      // 不进任务栏、不进 Alt-Tab
    ex |= WS_EX_NOACTIVATE;      // 不抢焦点
    ex &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    Log::debug(QStringLiteral("视频窗口已设置 WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE"));
#endif
}

} // namespace palune
