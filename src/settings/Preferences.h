#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace palune {

class Settings;
class MpvClient;
class CompactController;
class ActionRegistry;
class UpdateService;

// 设置项的唯一入口（S1–S6 整组补齐）。
// 约定：
// - 每个属性对应 settings.toml 的一个键；读取时带默认值，写入后立即原子落盘；
// - 「生效时机」写在属性注释上：即时 = 立刻作用到 mpv/窗口；重启 = 下次启动读取；
// - 所有属性共用 preferencesChanged 信号，QML 绑定在任一项变化后整体重算。
class Preferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int compactBreakpoint READ compactBreakpoint WRITE setCompactBreakpoint
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool restoreLastSession READ restoreLastSession WRITE setRestoreLastSession
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay NOTIFY preferencesChanged)
    Q_PROPERTY(bool startMaximized READ startMaximized WRITE setStartMaximized
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY preferencesChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool confirmExit READ confirmExit WRITE setConfirmExit NOTIFY preferencesChanged)
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY preferencesChanged)

    Q_PROPERTY(QString hwdecMode READ hwdecMode WRITE setHwdecMode NOTIFY preferencesChanged)
    Q_PROPERTY(int cacheSeconds READ cacheSeconds WRITE setCacheSeconds NOTIFY preferencesChanged)
    Q_PROPERTY(bool toneMapping READ toneMapping WRITE setToneMapping NOTIFY preferencesChanged)
    Q_PROPERTY(int targetPeakNits READ targetPeakNits WRITE setTargetPeakNits
                   NOTIFY preferencesChanged)
    Q_PROPERTY(QString deinterlace READ deinterlace WRITE setDeinterlace NOTIFY preferencesChanged)
    Q_PROPERTY(bool resumePlayback READ resumePlayback WRITE setResumePlayback
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoNext READ autoNext WRITE setAutoNext NOTIFY preferencesChanged)

    Q_PROPERTY(int subtitleFontSize READ subtitleFontSize WRITE setSubtitleFontSize
                   NOTIFY preferencesChanged)
    Q_PROPERTY(QString subtitleColor READ subtitleColor WRITE setSubtitleColor
                   NOTIFY preferencesChanged)
    Q_PROPERTY(double subtitleOutline READ subtitleOutline WRITE setSubtitleOutline
                   NOTIFY preferencesChanged)
    Q_PROPERTY(double subtitleDelay READ subtitleDelay WRITE setSubtitleDelay
                   NOTIFY preferencesChanged)
    Q_PROPERTY(int subtitlePosition READ subtitlePosition WRITE setSubtitlePosition
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoLoadSubtitles READ autoLoadSubtitles WRITE setAutoLoadSubtitles
                   NOTIFY preferencesChanged)
    Q_PROPERTY(QString subtitleEncoding READ subtitleEncoding WRITE setSubtitleEncoding
                   NOTIFY preferencesChanged)

    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY preferencesChanged)
    Q_PROPERTY(QString audioChannels READ audioChannels WRITE setAudioChannels
                   NOTIFY preferencesChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY preferencesChanged)

    Q_PROPERTY(QString updateChannel READ updateChannel WRITE setUpdateChannel
                   NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates
                   NOTIFY preferencesChanged)
    Q_PROPERTY(QString updateProxy READ updateProxy WRITE setUpdateProxy NOTIFY preferencesChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY preferencesChanged)

public:
    Preferences(Settings *settings, MpvClient *player, UpdateService *updater,
                CompactController *compact, ActionRegistry *actions,
                QObject *parent = nullptr);

    // 启动时（mpv 已初始化、窗口已建好）把当前配置一次性作用到播放器。
    void applyAll();
    // 关闭时把易变状态（音量/静音/速度）写回配置。
    void saveSessionState();

    int compactBreakpoint() const { return m_compactBreakpoint; }
    void setCompactBreakpoint(int value);
    bool restoreLastSession() const { return m_restoreLastSession; }
    void setRestoreLastSession(bool value);
    bool autoPlay() const { return m_autoPlay; }
    void setAutoPlay(bool value);
    bool startMaximized() const { return m_startMaximized; }
    void setStartMaximized(bool value);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool value);
    bool minimizeToTray() const { return m_minimizeToTray; }
    void setMinimizeToTray(bool value);
    bool confirmExit() const { return m_confirmExit; }
    void setConfirmExit(bool value);
    QString logLevel() const { return m_logLevel; }
    void setLogLevel(const QString &value);

    QString hwdecMode() const { return m_hwdecMode; }
    void setHwdecMode(const QString &value);
    int cacheSeconds() const { return m_cacheSeconds; }
    void setCacheSeconds(int value);
    bool toneMapping() const { return m_toneMapping; }
    void setToneMapping(bool value);
    int targetPeakNits() const { return m_targetPeakNits; }
    void setTargetPeakNits(int value);
    QString deinterlace() const { return m_deinterlace; }
    void setDeinterlace(const QString &value);
    bool resumePlayback() const { return m_resumePlayback; }
    void setResumePlayback(bool value);
    bool autoNext() const { return m_autoNext; }
    void setAutoNext(bool value);

    int subtitleFontSize() const { return m_subtitleFontSize; }
    void setSubtitleFontSize(int value);
    QString subtitleColor() const { return m_subtitleColor; }
    void setSubtitleColor(const QString &value);
    double subtitleOutline() const { return m_subtitleOutline; }
    void setSubtitleOutline(double value);
    double subtitleDelay() const { return m_subtitleDelay; }
    void setSubtitleDelay(double value);
    int subtitlePosition() const { return m_subtitlePosition; }
    void setSubtitlePosition(int value);
    bool autoLoadSubtitles() const { return m_autoLoadSubtitles; }
    void setAutoLoadSubtitles(bool value);
    QString subtitleEncoding() const { return m_subtitleEncoding; }
    void setSubtitleEncoding(const QString &value);

    QString audioDevice() const { return m_audioDevice; }
    void setAudioDevice(const QString &value);
    QString audioChannels() const { return m_audioChannels; }
    void setAudioChannels(const QString &value);
    double audioDelay() const { return m_audioDelay; }
    void setAudioDelay(double value);

    QString updateChannel() const { return m_updateChannel; }
    void setUpdateChannel(const QString &value);
    bool autoCheckUpdates() const { return m_autoCheckUpdates; }
    void setAutoCheckUpdates(bool value);
    QString updateProxy() const { return m_updateProxy; }
    void setUpdateProxy(const QString &value);
    QString downloadDir() const { return m_downloadDir; }
    void setDownloadDir(const QString &value);

    Q_INVOKABLE QVariantList audioDevices() const;
    Q_INVOKABLE QVariantList keyRows() const;
    Q_INVOKABLE void setKeyBinding(const QString &id, const QString &spec);
    Q_INVOKABLE void clearKeyBinding(const QString &id);
    Q_INVOKABLE void resetKeyBinding(const QString &id);
    Q_INVOKABLE QString specForKey(int key, int modifiers) const;
    Q_INVOKABLE QVariantList keyConflicts() const;
    Q_INVOKABLE void chooseDownloadDir();
    Q_INVOKABLE void chooseSubtitleFile();
    Q_INVOKABLE void openLicense();
    Q_INVOKABLE void openConfigDir();
    Q_INVOKABLE void openCrashDir();
    Q_INVOKABLE QString downloadDirEffective() const;

    QString keySpec(const QString &id) const;

signals:
    void preferencesChanged();

private:
    void store(const QString &key, const QVariant &value);
    QString stringValue(const QString &key, const QString &fallback) const;
    bool boolValue(const QString &key, bool fallback) const;
    int intValue(const QString &key, int fallback) const;
    double doubleValue(const QString &key, double fallback) const;
    void applyPlayback();
    void applySubtitles();
    void applyAudio();
    void applyUpdates();
    void reload();
    void rebuildKeyOverrides();

    Settings *m_settings = nullptr;
    MpvClient *m_player = nullptr;
    UpdateService *m_updater = nullptr;
    CompactController *m_compact = nullptr;
    ActionRegistry *m_actions = nullptr;

    int m_compactBreakpoint = 560;
    bool m_restoreLastSession = false;
    bool m_autoPlay = true;
    bool m_startMaximized = false;
    bool m_closeToTray = false;
    bool m_minimizeToTray = false;
    bool m_confirmExit = false;
    QString m_logLevel = QStringLiteral("info");

    QString m_hwdecMode = QStringLiteral("auto");
    int m_cacheSeconds = 0;
    bool m_toneMapping = true;
    int m_targetPeakNits = 1000;
    QString m_deinterlace = QStringLiteral("auto");
    bool m_resumePlayback = true;
    bool m_autoNext = false;

    int m_subtitleFontSize = 15;
    QString m_subtitleColor = QStringLiteral("#ffffff");
    double m_subtitleOutline = 1.0;
    double m_subtitleDelay = 0.0;
    int m_subtitlePosition = 8;
    bool m_autoLoadSubtitles = true;
    QString m_subtitleEncoding = QStringLiteral("auto");

    QString m_audioDevice = QStringLiteral("auto");
    QString m_audioChannels = QStringLiteral("auto");
    double m_audioDelay = 0.0;

    QString m_updateChannel = QStringLiteral("stable");
    bool m_autoCheckUpdates = true;
    QString m_updateProxy = QStringLiteral("system");
    QString m_downloadDir;
};

} // namespace palune
