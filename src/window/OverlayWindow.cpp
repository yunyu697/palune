#include "window/OverlayWindow.h"

#include "diag/Log.h"

#include <QCloseEvent>
#include <QShowEvent>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

namespace palune {

OverlayWindow::OverlayWindow(QObject *parent)
    : QQuickView()
{
    Q_UNUSED(parent)
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setTitle(QStringLiteral("Palune"));
}

void OverlayWindow::showEvent(QShowEvent *event)
{
    QQuickView::showEvent(event);
    applyNativeStyles();
}

void OverlayWindow::closeEvent(QCloseEvent *event)
{
    emit closed();
    event->accept();
}

void OverlayWindow::applyNativeStyles()
{
#if defined(Q_OS_WIN)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_APPWINDOW;   // 唯一的 Alt-Tab / 任务栏入口
    ex &= ~WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    // 无边框窗口补回可缩放/最大化样式，Windows 才会把 Alt+方向键与拖拽边缘
    // 识别为 Aero Snap；否则覆盖窗只能被程序移动，无法贴边。
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    // Windows 11：保留系统圆角（无边框窗口默认可能丢失）
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    Log::debug(QStringLiteral("UI 覆盖窗口已设置为应用窗口（WS_EX_APPWINDOW）"));
#endif
}

} // namespace palune
