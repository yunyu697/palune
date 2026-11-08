#pragma once

#include <QQuickView>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace palune {

// UI 覆盖窗口：应用窗口身份（任务栏、Alt-Tab、焦点），透明背景，
// 视频由下方的独立 VideoWindow 呈现，两窗几何严格同步。
class OverlayWindow : public QQuickView
{
    Q_OBJECT
public:
    explicit OverlayWindow(QObject *parent = nullptr);

signals:
    void closed();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void applyNativeStyles();
    // 激活/显示状态变化后强制一帧完整重绘：透明覆盖窗被其它窗口遮挡时，
    // DWM 会保留遮挡前的合成快照；若不在重获焦点时重新提交帧，暂停态下
    // 上一窗口的画面会残留在播放器上（alpha10 问题 3）。
    void forceRepaint();
    void scheduleRepaint();

#if defined(Q_OS_WIN)
    static LRESULT CALLBACK nativeEventProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR id, DWORD_PTR ref);
    bool m_subclassInstalled = false;
#endif
};

} // namespace palune
