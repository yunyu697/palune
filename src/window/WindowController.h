#pragma once

#include <QObject>
#include <QRect>
#include <QVariantList>
#include <QUrl>

#include "window/OverlayWindow.h"
#include "window/VideoWindow.h"

namespace palune {

class MpvClient;
class UpdateService;
class Settings;
class ThemeRegistry;
class LayoutHost;
class UiStateStore;
class CompactController;
class MediaLibraryService;

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
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
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
    QString windowTitle() const { return m_windowTitle; }
    QVariantList recentFiles() const;
    QString version() const;
    QString configDir() const;
    QString crashDir() const;
    QString logFile() const;

    ThemeRegistry *themeRegistry() const { return m_themeRegistry; }
    LayoutHost *layoutHost() const { return m_layoutHost; }
    UiStateStore *uiState() const { return m_uiState; }
    CompactController *compactMode() const { return m_compactMode; }
    Settings *settings() const { return m_settings; }

    // 布局统一动作入口（布局只发动作名，不直接调用播放器）。
    // QML 不填默认参数，按参数个数区分两个重载。
    Q_INVOKABLE void action(const QString &name);
    Q_INVOKABLE void action(const QString &name, const QVariant &value);
    Q_INVOKABLE void openFolderDialog();
    Q_INVOKABLE void openRecentFile(const QString &path);

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
    void recentFilesChanged();
    void uiActionRequested(const QString &name, const QVariant &value);

private:
    bool loadQml();
    // 布局重建后按 UiStateStore 记录的动作 ID 恢复焦点（跨主题等价控件）。
    void restoreFocus();
    void updateTitle();
    void showWindows();
    void saveWindowState();
    void rememberFile(const QString &path);

    MpvClient *m_player = nullptr;
    UpdateService *m_updater = nullptr;
    Settings *m_settings = nullptr;
    MediaLibraryService *m_library = nullptr;
    ThemeRegistry *m_themeRegistry = nullptr;
    LayoutHost *m_layoutHost = nullptr;
    UiStateStore *m_uiState = nullptr;
    CompactController *m_compactMode = nullptr;
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
