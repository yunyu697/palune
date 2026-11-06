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
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void stop();

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString title() const { return m_title; }
    QString filePath() const { return m_filePath; }

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
    void fileLoaded();
    void endFile(const QString &reason);
    void errorOccurred(const QString &message);

private slots:
    void poll();

private:
    bool loadLibrary();
    bool resolveSymbols();
    void applyOptions(qintptr wid);
    void handleEvents();
    void updateProperties();
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
};

} // namespace palune
