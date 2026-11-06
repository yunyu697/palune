#include "window/VideoWindow.h"

#include "diag/Log.h"

#include <QPalette>
#include <QResizeEvent>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace palune {

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
        SetWindowPos(reinterpret_cast<HWND>(m_nativeSurface), nullptr, 0, 0, width(), height(),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}

qintptr VideoWindow::videoSurfaceHandle()
{
#if defined(Q_OS_WIN)
    if (!m_nativeSurface) {
        // 直接用 Win32 子窗承载 libmpv：Qt 的 QWidget 子窗会被 mpv 的 w32 后端拒绝创建渲染窗。
        RECT rc = {0, 0, 0, 0};
        GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
        m_nativeSurface = reinterpret_cast<quintptr>(
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                            reinterpret_cast<HWND>(winId()), nullptr,
                            GetModuleHandleW(nullptr), nullptr));
        Log::info(QStringLiteral("视频子窗（Win32 STATIC）：hwnd=%1 size=%2x%3")
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
