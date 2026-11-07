#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

namespace palune {

// M2 截图基线用的轻量上下文：布局只读播放器属性，截图不需要真的解码。
class ShotPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks WRITE setAudioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks WRITE setSubtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage WRITE setErrorMessage NOTIFY errorMessageChanged)
    // M3 技术信息条：与 MpvClient 同名属性，截图用固定样本值。
    Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY videoInfoChanged)
    Q_PROPERTY(double videoFps READ videoFps WRITE setVideoFps NOTIFY videoInfoChanged)
    Q_PROPERTY(double videoBitrate READ videoBitrate WRITE setVideoBitrate NOTIFY videoInfoChanged)
    Q_PROPERTY(QString hwdec READ hwdec WRITE setHwdec NOTIFY videoInfoChanged)
    Q_PROPERTY(QString hdrMode READ hdrMode WRITE setHdrMode NOTIFY videoInfoChanged)

public:
    explicit ShotPlayer(QObject *parent = nullptr) : QObject(parent) {}

    double position() const { return m_position; }
    void setPosition(double v) { if (m_position != v) { m_position = v; emit positionChanged(); } }
    double duration() const { return m_duration; }
    void setDuration(double v) { if (m_duration != v) { m_duration = v; emit durationChanged(); } }
    bool paused() const { return m_paused; }
    void setPaused(bool v) { if (m_paused != v) { m_paused = v; emit pausedChanged(); } }
    double volume() const { return m_volume; }
    void setVolume(double v) { if (m_volume != v) { m_volume = v; emit volumeChanged(); } }
    bool muted() const { return m_muted; }
    void setMuted(bool v) { if (m_muted != v) { m_muted = v; emit mutedChanged(); } }
    QString title() const { return m_title; }
    void setTitle(const QString &v) { if (m_title != v) { m_title = v; emit titleChanged(); } }
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &v) { if (m_filePath != v) { m_filePath = v; emit filePathChanged(); } }
    double speed() const { return m_speed; }
    void setSpeed(double v) { if (m_speed != v) { m_speed = v; emit speedChanged(); } }
    QVariantList audioTracks() const { return m_audioTracks; }
    void setAudioTracks(const QVariantList &v) { m_audioTracks = v; emit tracksChanged(); }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    void setSubtitleTracks(const QVariantList &v) { m_subtitleTracks = v; emit tracksChanged(); }
    QString errorMessage() const { return m_errorMessage; }
    void setErrorMessage(const QString &v) { if (m_errorMessage != v) { m_errorMessage = v; emit errorMessageChanged(); } }
    QString videoCodec() const { return m_videoCodec; }
    void setVideoCodec(const QString &v) { if (m_videoCodec != v) { m_videoCodec = v; emit videoInfoChanged(); } }
    double videoFps() const { return m_videoFps; }
    void setVideoFps(double v) { if (m_videoFps != v) { m_videoFps = v; emit videoInfoChanged(); } }
    double videoBitrate() const { return m_videoBitrate; }
    void setVideoBitrate(double v) { if (m_videoBitrate != v) { m_videoBitrate = v; emit videoInfoChanged(); } }
    QString hwdec() const { return m_hwdec; }
    void setHwdec(const QString &v) { if (m_hwdec != v) { m_hwdec = v; emit videoInfoChanged(); } }
    QString hdrMode() const { return m_hdrMode; }
    void setHdrMode(const QString &v) { if (m_hdrMode != v) { m_hdrMode = v; emit videoInfoChanged(); } }

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
    void errorMessageChanged();
    void videoInfoChanged();

private:
    double m_position = 0;
    double m_duration = 0;
    bool m_paused = false;
    double m_volume = 100;
    bool m_muted = false;
    QString m_title;
    QString m_filePath;
    double m_speed = 1.0;
    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    QString m_errorMessage;
    QString m_videoCodec;
    QString m_hwdec;
    QString m_hdrMode;
    double m_videoFps = 0.0;
    double m_videoBitrate = 0.0;
};

// 窗口上下文桩：动作只记账，截图不触发真实窗口操作。
class ShotWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(QVariantList recentFiles READ recentFiles WRITE setRecentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString configDir READ configDir CONSTANT)
    Q_PROPERTY(QString crashDir READ crashDir CONSTANT)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)

public:
    explicit ShotWindow(QObject *parent = nullptr) : QObject(parent) {}

    bool fullscreen() const { return m_fullscreen; }
    void setFullscreen(bool v) { if (m_fullscreen != v) { m_fullscreen = v; emit fullscreenChanged(); } }
    QVariantList recentFiles() const { return m_recentFiles; }
    void setRecentFiles(const QVariantList &v) { m_recentFiles = v; emit recentFilesChanged(); }
    QString version() const { return QStringLiteral(PALUNE_VERSION); }
    QString configDir() const { return QCoreApplication::translate("Palune", "(截图模式)"); }
    QString crashDir() const { return QCoreApplication::translate("Palune", "(截图模式)"); }
    QString logFile() const { return QCoreApplication::translate("Palune", "(截图模式)"); }
    QStringList actionNames() const { return m_actions; }

    Q_INVOKABLE void action(const QString &name) { m_actions << name; }
    Q_INVOKABLE void action(const QString &name, const QVariant &) { m_actions << name; }
    Q_INVOKABLE void moveBy(double, double) {}
    Q_INVOKABLE void openFile(const QString &) {}
    Q_INVOKABLE void openFileDialog() {}
    Q_INVOKABLE void openRecentFile(const QString &) {}
    // 布局 B 会给检查器让位，截图模式只记账不下发原生几何。
    Q_INVOKABLE void setVideoInsets(double, double, double, double) {}

signals:
    void fullscreenChanged();
    void recentFilesChanged();

private:
    bool m_fullscreen = false;
    QVariantList m_recentFiles;
    QStringList m_actions;
};

} // namespace palune
