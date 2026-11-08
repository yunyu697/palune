#pragma once

#include <QLibrary>
#include <QObject>
#include <QTimer>

struct mpv_handle;
struct mpv_event;

namespace palune {

// libmpv 适配层（M0 最小实现）。
// 硬约束：libmpv 必须是动态库（运行时加载 libmpv-2.dll / libmpv.so.2），
// 禁止静态链接 libmpv/FFmpeg。加载失败时明确报错，不静默降级。
class MpvClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString loadError READ loadError CONSTANT)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(int startFileCount READ startFileCount NOTIFY playbackEventsChanged)
    Q_PROPERTY(int endFileCount READ endFileCount NOTIFY playbackEventsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    // 技术信息条（设计稿 §4：字段映射 mpv 属性，无文件时统一为空，由 QML 显示 —）。
    Q_PROPERTY(QString videoCodec READ videoCodec NOTIFY videoInfoChanged)
    Q_PROPERTY(double videoFps READ videoFps NOTIFY videoInfoChanged)
    Q_PROPERTY(double videoBitrate READ videoBitrate NOTIFY videoInfoChanged)
    Q_PROPERTY(QString hwdec READ hwdec NOTIFY videoInfoChanged)
    Q_PROPERTY(QString hdrMode READ hdrMode NOTIFY videoInfoChanged)

public:
    explicit MpvClient(QObject *parent = nullptr);
    ~MpvClient() override;

    bool initialize();
    bool available() const { return m_available; }
    QString loadError() const { return m_loadError; }
    bool setVideoWindow(qintptr wid);

    Q_INVOKABLE bool openFile(const QString &path);
    bool openTestPattern();
    QString mpvVersion();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void seek(double seconds);
    // 暂停态下强制重绘当前帧（同位置 seek），用于失焦/最小化恢复时清除合成残留。
    Q_INVOKABLE void refreshFrame();
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setSpeed(double speed);
    // M2 快捷键：逐帧步进与字幕延迟。
    Q_INVOKABLE void frameStep(bool forward);
    Q_INVOKABLE void addSubtitleDelay(double delta);
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id); // -1 = 关闭字幕
    // 长时压测用：循环播放避免样本先于压测时长结束（产品默认关闭）。
    void setLoopFile(bool enabled);

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString title() const { return m_title; }
    QString filePath() const { return m_filePath; }
    double speed() const { return m_speed; }
    double subtitleDelay() const { return m_subtitleDelay; }
    QVariantList audioTracks() const { return m_audioTracks; }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    int startFileCount() const { return m_startFileCount; }
    int endFileCount() const { return m_endFileCount; }
    QString errorMessage() const { return m_errorMessage; }
    QString videoCodec() const { return m_videoCodec; }
    double videoFps() const { return m_videoFps; }
    double videoBitrate() const { return m_videoBitrate; }
    QString hwdec() const { return m_hwdec; }
    QString hdrMode() const { return m_hdrMode; }
    double audioPts() const { return m_audioPts; }

    // M0 证据采集
    bool sampleVideoLuminance(double *mean, int *width, int *height, QString *error);
    qint64 frameDropCount() const { return m_frameDropCount; }
    qint64 mistimedFrameCount() const { return m_mistimedFrameCount; }
    int videoReconfigCount() const { return m_videoReconfigCount; }
    bool eofReached() const { return m_eofReached; }
    QString backendDescription() const;

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void volumeChanged();
    void mutedChanged();
    void titleChanged();
    void filePathChanged();
    void speedChanged();
    void tracksChanged();
    void playbackEventsChanged();
    void fileLoaded();
    void endFile(const QString &reason);
    void errorOccurred(const QString &message);
    void errorMessageChanged();
    void videoInfoChanged();

private slots:
    void poll();

private:
    bool loadLibrary();
    bool resolveSymbols();
    void applyOptions(qintptr wid);
    void handleEvents();
    void updateProperties();
    // 解码器 / 帧率 / 码率 / HDR 等信息不是每 5ms 都需要，按低频采样。
    void updateVideoInfo();
    void clearVideoInfo();
    void refreshTrackList();
    template <typename T>
    bool resolve(T &fn, const char *name);

    struct Api;
    Api *m_api = nullptr;
    QLibrary *m_library = nullptr;
    mpv_handle *m_mpv = nullptr;
    QTimer m_timer;

    bool m_available = false;
    QString m_loadError;
    double m_position = 0;
    double m_duration = 0;
    double m_volume = 100;
    bool m_paused = false;
    bool m_muted = false;
    bool m_eofReached = false;
    QString m_title;
    QString m_filePath;
    qint64 m_frameDropCount = 0;
    qint64 m_mistimedFrameCount = 0;
    int m_videoReconfigCount = 0;
    bool m_initialized = false;
    bool m_pendingFile = false;
    double m_speed = 1.0;
    double m_subtitleDelay = 0.0;
    double m_audioPts = 0.0;
    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    qint64 m_trackCount = -1;
    int m_startFileCount = 0;
    int m_endFileCount = 0;
    QString m_errorMessage;
    QString m_videoCodec;
    QString m_hwdec;
    QString m_hdrMode;
    double m_videoFps = 0.0;
    double m_videoBitrate = 0.0;
    int m_propTick = 0;
};

} // namespace palune
