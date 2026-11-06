#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace palune {

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
    };

    explicit AppController(const Options &options, QObject *parent = nullptr);

    void start();

private:
    void startNormal();
    void runShots();
    void finishWithCode(int code);
    void runUpdateCheckOnly();
    void runUpdateE2E();
    void writeRestartMarker();
    void writeReport(const QJsonObject &report);
    static QString hashFile(const QString &path);

    Options m_options;
    MpvClient *m_player = nullptr;
    UpdateService *m_updater = nullptr;
    WindowController *m_windows = nullptr;
    QJsonArray m_transitions;
    QString m_reportedState;
    bool m_reportWritten = false;
};

} // namespace palune
