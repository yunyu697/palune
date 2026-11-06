#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace palune {

class Settings;
class MediaLibraryService;
class MpvClient;
class UpdateService;
class WindowController;

class AppController : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString openPath;
        bool selftest = false;
        QString videoPath;
        QString reportPath;
        QString shotDir;
        QString updateUrl;
        QString downloadDir;
        QString e2eMode;          // downloadonly | full
        bool updateCheckOnly = false;
        bool restarted = false;
        QString restartMarker;
        bool hiddenDesktop = false;

        // M1 证据采集
        bool m1Smoke = false;        // --m1-smoke
        bool m1Perf = false;         // --m1-perf
        bool m1ThemeSwitch = false;  // --m1-theme-switch
        QString mediaPath;           // --media：目录或单文件
        int switchCount = 10;
    };

    AppController(const Options &options, Settings *settings, QObject *parent = nullptr);

    void start();

private:
    void startNormal();
    void runShots();
    void finishWithCode(int code);
    void runUpdateCheckOnly();
    void runUpdateE2E();
    void writeRestartMarker();
    void writeReport(const QJsonObject &report);
    void writeJsonReport(const QJsonObject &report, const QString &path);
    static QString hashFile(const QString &path);

    void runM1Smoke();
    void runM1Perf();
    void runM1ThemeSwitch();
    QStringList collectMedia() const;
    bool waitFor(const std::function<bool()> &predicate, int timeoutMs);
    void sleepMs(int ms);

    Options m_options;
    Settings *m_settings = nullptr;
    MediaLibraryService *m_library = nullptr;
    MpvClient *m_player = nullptr;
    UpdateService *m_updater = nullptr;
    WindowController *m_windows = nullptr;
    QJsonArray m_transitions;
    QString m_reportedState;
    bool m_reportWritten = false;
    QString m_lastPlaybackError;
    double m_windowReadyMs = -1.0;
};

} // namespace palune
