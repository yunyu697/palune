#pragma once

#include <QWidget>

class QResizeEvent;

namespace palune {

// 视频承载窗口（Windows 主路径）：独立的无边框顶层窗口，
// 内部再放一个原生子窗口作为 mpv 的 wid。视频窗口在任务栏/Alt-Tab 中隐藏，
// 由 OverlayWindow 统一承担应用窗口身份。
class VideoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWindow(QWidget *parent = nullptr);

    qintptr videoSurfaceHandle();
    QWidget *surface() const { return m_surface; }
    // 空转时不显示视频面：mpv 尚未出画时该子窗会露出浅色底，实机表现为「白块」。
    void setVideoSurfaceVisible(bool visible);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyNativeStyles();

    QWidget *m_surface = nullptr;
    quintptr m_nativeSurface = 0;
};

} // namespace palune
