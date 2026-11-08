#include "window/OverlayWindow.h"

#include "diag/Log.h"

#include <QCloseEvent>
#include <QShowEvent>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#pragma comment(lib, "comctl32.lib")
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

    // 失焦/恢复时重新提交一帧。透明 QQuickView 的缓冲区在遮挡期间不会自动失效，
    // 只靠 DWM 恢复会带回遮挡前的像素（暂停态尤其明显，因为没有新视频帧覆盖）。
    connect(this, &QWindow::activeChanged, this, [this]() {
        if (isActive())
            scheduleRepaint();
    });
    connect(this, &QWindow::visibilityChanged, this, [this]() {
        if (isVisible() && visibility() != QWindow::Minimized)
            scheduleRepaint();
    });
}

void OverlayWindow::scheduleRepaint()
{
    // 放到事件循环下一拍：避免在 WM_NCACTIVATE 等同步消息里重入 Qt 的渲染路径。
    QTimer::singleShot(0, this, &OverlayWindow::forceRepaint);
}

void OverlayWindow::forceRepaint()
{
    if (!isVisible())
        return;
    update();
#if defined(Q_OS_WIN)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd)
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
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

    // WS_THICKFRAME 是为了保留系统缩放/贴边命中，但它同时给窗口加了一圈 1px 非客户区。
    // 无边框窗口的 QML 只绘制客户区，这 1px 就成了「窗口外缘一圈方框」，最大化/全屏
    // 时几何变化会再露一次。接管 WM_NCCALCSIZE 让客户区覆盖整窗即可消除，
    // 同时保留 WS_THICKFRAME 的缩放与 Aero Snap 命中。
    if (!m_subclassInstalled) {
        SetWindowSubclass(hwnd, &OverlayWindow::nativeEventProc, 0,
                          reinterpret_cast<DWORD_PTR>(this));
        m_subclassInstalled = true;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Log::debug(QStringLiteral("UI 覆盖窗口已设置为应用窗口（WS_EX_APPWINDOW）"));
#endif
}

#if defined(Q_OS_WIN)
LRESULT CALLBACK OverlayWindow::nativeEventProc(HWND hwnd, UINT msg, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR id, DWORD_PTR ref)
{
    Q_UNUSED(lParam)
    Q_UNUSED(id)
    auto *self = reinterpret_cast<OverlayWindow *>(ref);
    if (msg == WM_NCCALCSIZE && wParam == TRUE)
        return 0; // 客户区 = 整窗，不留系统非客户区边框
    // 失焦时 Windows 会按 WS_THICKFRAME 重画一圈浅灰非客户区边框（实机表现为窗口外缘方框）。
    // 不进入 DefWindowProc 的非客户区绘制路径，直接吞掉非客户区绘制；
    // 但必须显式让客户区重绘，否则激活切换后客户端缓冲不失效，会残留上一窗口的合成快照。
    if (msg == WM_NCACTIVATE) {
        if (self)
            self->scheduleRepaint();
        return TRUE;
    }
    if (msg == WM_NCPAINT)
        return TRUE;
    if (msg == WM_ACTIVATE || msg == WM_SIZE || msg == WM_SHOWWINDOW) {
        if (self)
            self->scheduleRepaint();
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
#endif

} // namespace palune
