#pragma once

#include <QColor>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace palune {

// 系统窗框的统一入口：圆角偏好 + 暗色模式 + 边框色，三者必须一起下发。
//
// 以前这三件事散在 OverlayWindow / SettingsWindow 各自的 showEvent 里，而且只设了
// 圆角偏好、没声明暗色模式，也没给边框色。Win11 会把这种窗口当成浅色应用，
// 在无边框透明窗外面画一圈系统亮边；深色界面下四角/四边那圈"很浅的白边"
// 主要就是它，加上 Main.qml 里自绘的 1px hairline 叠在一起。
//
// radius <= 0（最大化/全屏）时调成不圆角，避免与系统边框叠成两道。
inline void applySystemChrome(qintptr handle, const QColor &border, int radius)
{
#if defined(Q_OS_WIN)
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!hwnd)
        return;
    const DWORD corner = radius > 0 ? 2u /* DWMWCP_ROUND */ : 1u /* DWMWCP_DONOTROUND */;
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    if (border.isValid()) {
        const COLORREF color = RGB(border.red(), border.green(), border.blue());
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &color, sizeof(color));
    }
#else
    Q_UNUSED(handle)
    Q_UNUSED(border)
    Q_UNUSED(radius)
#endif
}

} // namespace palune
