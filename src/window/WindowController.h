#pragma once

#include <QColor>
#include <QObject>
#include <QRect>
#include <QVariantList>
#include <QUrl>

class QTimer;

#include "window/OverlayWindow.h"
#include "window/VideoWindow.h"
#include "ui/PlayQueue.h"

namespace palune {

class MpvClient;
class UpdateService;
class Settings;
class ThemeRegistry;
class LayoutHost;
class UiStateStore;
class CompactController;
class MediaLibraryService;
class ActionRegistry;
class SettingsWindow;
class Preferences;
class PlayQueue;

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
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY maximizedChanged)
    Q_PROPERTY(int windowRadius READ windowRadius NOTIFY windowRadiusChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(PlayQueue *playQueue READ playQueue CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString configDir READ configDir CONSTANT)
    Q_PROPERTY(QString crashDir READ crashDir CONSTANT)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)

public:
    WindowController(MpvClient *player,
                     UpdateService *updater,
                     Settings *settings,
                     MediaLibraryService *library,
                     QObject *parent = nullptr);
    ~WindowController() override;

    bool start(int width, int height);
    bool ready() const { return m_ready; }

    VideoWindow *videoWindow() const { return m_video; }
    OverlayWindow *overlay() const { return m_overlay; }

    bool isFullscreen() const { return m_fullscreen; }
    bool isMaximized() const { return m_maximized; }
    // 当前主题的窗口圆角（逻辑像素），QML 壳层与原生窗口 Region 共用同一值。
    int windowRadius() const { return m_cornerRadius; }
    QString windowTitle() const { return m_windowTitle; }
    QVariantList recentFiles() const;
    PlayQueue *playQueue() const { return m_queue; }
    QString version() const;
    QString configDir() const;
    QString crashDir() const;
    QString logFile() const;

    ThemeRegistry *themeRegistry() const { return m_themeRegistry; }
    LayoutHost *layoutHost() const { return m_layoutHost; }
    UiStateStore *uiState() const { return m_uiState; }
    CompactController *compactMode() const { return m_compactMode; }
    ActionRegistry *actions() const { return m_actions; }
    Settings *settings() const { return m_settings; }
    Preferences *preferences() const { return m_preferences; }
    MediaLibraryService *library() const { return m_library; }

    // 布局统一动作入口（布局只发动作名，不直接调用播放器）。
    // QML 不填默认参数，按参数个数区分两个重载。
    Q_INVOKABLE void action(const QString &name);
    Q_INVOKABLE void action(const QString &name, const QVariant &value);
    Q_INVOKABLE void openFolderDialog();
    Q_INVOKABLE void openRecentFile(const QString &path);
    // 拖放统一入口：区分媒体 / 字幕 / 网络地址，多文件全部入队。
    Q_INVOKABLE void handleDrop(const QVariantList &urls);
    // 打开网络地址：http/https/rtsp/rtmp 等直接交给 mpv。
    Q_INVOKABLE void openUrl(const QString &url);
    // 队列相关：追加文件、保存/载入 .json 队列。
    Q_INVOKABLE void appendFilesDialog();
    Q_INVOKABLE void saveQueueDialog();
    Q_INVOKABLE void loadQueueDialog();
    // 媒体库条目点击：接受本地绝对路径或已编码的 URL。
    Q_INVOKABLE void openPath(const QString &path);
    // 无边框窗口的原生拖拽 / 边缘缩放 / 最大化：交给系统处理，保证与 Snap、多屏 DPI 一致。
    Q_INVOKABLE void startSystemMove();
    Q_INVOKABLE void startSystemResize(int edges);
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void minimizeWindow();
    // 布局 B 等需要给侧栏让位时，由布局下发视频区在窗口内的逻辑像素内缩。
    // 布局切换时新布局会在 onCompleted 里重新下发，A/C 下发 0 即恢复满窗。
    Q_INVOKABLE void setVideoInsets(double left, double top, double right, double bottom);
    // 窗口外框的边框色由主题（QML 令牌）下发，交给系统画；QML 不再自绘窗口级描边。
    Q_INVOKABLE void setWindowFrameColor(const QColor &color);

    ApplyResult applyGeometry(const QRect &rect);
    ApplyResult toggleFullscreenForTest();
    ApplyResult setFullscreenForTest(bool on);
    ApplyResult restoreForTest(bool minimize);

    // 设置是独立顶层窗口，不进入主窗 QML 层，避免被主窗 Region 裁剪或被视频子窗压住。
    void openSettingsWindow();
    void closeSettingsWindow();
    SettingsWindow *settingsWindow() const { return m_settingsWindow; }

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
    // 托盘「退出」必须绕过「关闭到托盘」。
    Q_INVOKABLE void quitApplication();
    void moveBy(double dx, double dy);
    void openFileDialog();
    void openFile(const QUrl &url);
    void restore();
    void activate();

signals:
    void fullscreenChanged();
    void maximizedChanged();
    void windowRadiusChanged();
    void windowTitleChanged();
    void recentFilesChanged();
    void uiActionRequested(const QString &name, const QVariant &value);

private:
    bool loadQml();
    // 布局重建后按 UiStateStore 记录的动作 ID 恢复焦点（跨主题等价控件）。
    void restoreFocus();
    void updateTitle();
    // 用户拖拽/缩放原生窗口后，把视频窗同步到覆盖窗几何。
    void syncVideoToOverlay();
    // 锁死双窗分层：视频窗必须紧贴覆盖窗正下方；被系统重排后立即插回。
    void enforceLayering();
    // 给两窗应用与主题一致的圆角 Region（Windows 无边框窗默认没有圆角）。
    void applyWindowRegion();
    void showWindows();
    void saveWindowState();
    void rememberFile(const QString &path);
    void playSibling(int delta);
    void saveResumePosition();
    void playQueueIndex(int index);
    void beginQueuePlayback(const QString &path);
    void setAspectAndNotify(const QString &mode);

    MpvClient *m_player = nullptr;
    UpdateService *m_updater = nullptr;
    Settings *m_settings = nullptr;
    MediaLibraryService *m_library = nullptr;
    ThemeRegistry *m_themeRegistry = nullptr;
    LayoutHost *m_layoutHost = nullptr;
    UiStateStore *m_uiState = nullptr;
    CompactController *m_compactMode = nullptr;
    ActionRegistry *m_actions = nullptr;
    Preferences *m_preferences = nullptr;
    PlayQueue *m_queue = nullptr;
    bool m_playingFromQueue = false;
    VideoWindow *m_video = nullptr;
    OverlayWindow *m_overlay = nullptr;
    SettingsWindow *m_settingsWindow = nullptr;
    bool m_ready = false;
    // 系统窗框边框色：由 QML 按主题下发，默认取深色底层色，避免首帧出现系统亮边。
    QColor m_frameColor = QColor(0x0d, 0x0e, 0x11);
    double m_pendingResume = -1.0;
    qint64 m_lastResumeSaveMs = 0;
    qint64 m_lastRemoteReportMs = 0;
    bool m_fullscreen = false;
    bool m_minimized = false;
    bool m_maximized = false;
    bool m_syncing = false;
    bool m_forceQuit = false;
    int m_cornerRadius = 14;
    // 视频区相对覆盖窗的内缩（逻辑像素）；0 表示视频铺满整窗（A/C 与全屏）。
    double m_insetLeft = 0.0;
    double m_insetTop = 0.0;
    double m_insetRight = 0.0;
    double m_insetBottom = 0.0;
    QRect m_normalGeometry;
    QRect m_restoreGeometry;
    QString m_windowTitle = QStringLiteral("Palune");
    qint64 m_lastSyncMicros = 0;
    // 双窗分层巡检（兜底系统消息覆盖不到的路径，如无激活变更的置顶遮挡、
    // 休眠唤醒后的合成层重建）。仅在窗口可见且未最小化时运行。
    QTimer *m_layerWatchdog = nullptr;
};

} // namespace palune
