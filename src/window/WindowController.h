#pragma once

#include <QObject>
#include <QRect>
#include <QUrl>

#include "window/OverlayWindow.h"
#include "window/VideoWindow.h"

namespace palune {

class MpvClient;
class UpdateService;

// 双窗几何同步结果（M0 判据用）。
struct ApplyResult {
    qint64 syncMicros = 0; // 视频窗提交到 UI 窗提交的时差
    int dx = 0, dy = 0, dw = 0, dh = 0; // 两窗矩形差
    bool overlayAbove = false;
};

class WindowController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool fullscreen READ isFullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(bool ready READ ready CONSTANT)

public:
    WindowController(MpvClient *player, UpdateService *updater, QObject *parent = nullptr);
    ~WindowController() override;

    bool start(int width, int height);
    bool ready() const { return m_ready; }

    VideoWindow *videoWindow() const { return m_video; }
    OverlayWindow *overlay() const { return m_overlay; }

    bool isFullscreen() const { return m_fullscreen; }
    QString windowTitle() const { return m_windowTitle; }

    ApplyResult applyGeometry(const QRect &rect);
    ApplyResult toggleFullscreenForTest();
    ApplyResult setFullscreenForTest(bool on);
    ApplyResult restoreForTest(bool minimize);

    qintptr overlayHandle() const;
    qintptr videoHandle() const;
    qint64 lastSyncMicros() const { return m_lastSyncMicros; }
    QRect normalGeometry() const { return m_normalGeometry; }
    bool isMinimized() const { return m_minimized; }

public slots:
    void setFullscreen(bool on);
    void toggleFullscreen();
    void minimize();
    void close();
    void moveBy(double dx, double dy);
    void openFileDialog();
    void openFile(const QUrl &url);
    void restore();
    void activate();

signals:
    void fullscreenChanged();
    void windowTitleChanged();

private:
    bool loadQml();
    void updateTitle();
    void showWindows();

    MpvClient *m_player = nullptr;
    UpdateService *m_updater = nullptr;
    VideoWindow *m_video = nullptr;
    OverlayWindow *m_overlay = nullptr;
    bool m_ready = false;
    bool m_fullscreen = false;
    bool m_minimized = false;
    QRect m_normalGeometry;
    QString m_windowTitle = QStringLiteral("Palune");
    qint64 m_lastSyncMicros = 0;
};

} // namespace palune
