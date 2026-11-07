#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include "update/UpdateManifest.h"

class QFile;
class QNetworkReply;
class QTimer;

namespace palune {

// 更新模块状态机：
// unchecked -> checking -> (uptodate | available)
// available -> downloading -> (downloaded | installing | error)
// error -> retry / 换镜像 -> downloading | checking
class UpdateService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY manifestChanged)
    Q_PROPERTY(QString publishedAt READ publishedAt NOTIFY manifestChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY manifestChanged)
    Q_PROPERTY(qint64 sizeBytes READ sizeBytes NOTIFY manifestChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY manifestChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString speedText READ speedText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QString lastCheckedText READ lastCheckedText NOTIFY lastCheckedTextChanged)
    Q_PROPERTY(QVariantList mirrors READ mirrors NOTIFY mirrorsChanged)
    Q_PROPERTY(int mirrorIndex READ mirrorIndex NOTIFY mirrorChanged)
    Q_PROPERTY(QString mirrorName READ mirrorName NOTIFY mirrorChanged)
    Q_PROPERTY(QString downloadedPath READ downloadedPath NOTIFY downloadedPathChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY manifestChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString installActionText READ installActionText NOTIFY stateChanged)

public:
    explicit UpdateService(QObject *parent = nullptr);
    UpdateService(const QUrl &feedUrl, const QString &downloadDirOverride, QObject *parent);

    QString state() const;
    QString currentVersion() const;
    QString availableVersion() const { return m_manifest.version; }
    QString publishedAt() const { return m_manifest.publishedAt; }
    QString notes() const { return m_manifest.notes; }
    qint64 sizeBytes() const { return m_manifest.size; }
    QString sizeText() const;
    int progress() const { return m_progress; }
    QString speedText() const;
    QString etaText() const;
    QString errorCode() const { return m_errorCode; }
    QString errorMessage() const { return m_errorMessage; }
    QString lastCheckedText() const;
    QVariantList mirrors() const { return m_mirrors; }
    int mirrorIndex() const { return m_mirrorIndex; }
    QString mirrorName() const;
    QString downloadedPath() const { return m_downloadedPath; }
    QString expectedSha256() const { return m_manifest.sha256; }
    bool updateAvailable() const { return m_hasUpdate; }
    bool busy() const;
    QString statusText() const;
    QString installActionText() const;

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void selectMirror(int index);
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void startDownloadOnly();
    Q_INVOKABLE void installDownloaded();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void openHomePage();

    // 截图/演示用：在 --ui-shot 模式下直接设定状态，不触发网络。
    Q_INVOKABLE void setDemoState(const QString &stateName);

    void setFeedUrl(const QUrl &url) { m_feedUrl = url; }
    void setDownloadDirOverride(const QString &dir) { m_downloadDirOverride = dir; }
    bool isDownloading() const;

signals:
    void stateChanged();
    void progressChanged();
    void errorChanged();
    void manifestChanged();
    void mirrorsChanged();
    void mirrorChanged();
    void downloadedPathChanged();
    void lastCheckedTextChanged();
    void finished(const QString &stateName);

private:
    enum class State { Unchecked, Checking, UpToDate, Available, Downloading, Downloaded, Installing, Error };
    enum class Action { None, UpdateAndInstall, DownloadOnly };

    void setState(State s);
    static QString stateName(State s);
    void fail(const QString &code, const QString &detail);
    void fetchManifest();
    void handleManifestReply(QNetworkReply *reply);
    void startDownload(Action action);
    void handleDownloadProgress();
    void finishDownload();
    bool verifyPackage(const QString &path, QString *error);
    bool verifyManifestSignature(QString *error);
    void launchInstaller(const QString &path);
    void rebuildMirrors();
    bool tryNextChannel(const QString &reason);
    QUrl selectedArtifactUrl() const;
    QString targetDirectory() const;
    QString packageFileName() const;
    QByteArray publicKey() const;
    void recordLastCheck();
    void loadLastCheck();
    static QString readableError(const QString &code);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_progressTimer = nullptr;

    QUrl m_feedUrl;
    QString m_downloadDirOverride;
    UpdateManifest m_manifest;
    QByteArray m_manifestBytes;

    State m_state = State::Unchecked;
    Action m_action = Action::None;
    bool m_hasUpdate = false;
    bool m_restartingDownload = false;

    QVariantList m_mirrors;
    int m_mirrorIndex = 0;

    QFile *m_file = nullptr;
    QString m_currentPath;
    qint64 m_received = 0;
    qint64 m_lastProgressBytes = 0;
    QElapsedTimer m_downloadClock;
    QElapsedTimer m_speedClock;
    double m_speedBytesPerSecond = 0;
    int m_progress = 0;

    QString m_errorCode;
    QString m_errorMessage;
    QString m_downloadedPath;
    QString m_lastCheckedIso;
    qint64 m_installerPid = 0;
};

} // namespace palune
