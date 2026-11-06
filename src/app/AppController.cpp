#include "app/AppController.h"

#include "diag/Log.h"
#include "m0/SelfTest.h"
#include "mpv/MpvClient.h"
#include "update/UpdateService.h"
#include "window/WindowController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTimer>

namespace palune {

namespace {

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

AppController::AppController(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
{
}

void AppController::start()
{
    if (m_options.restarted) {
        writeRestartMarker();
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    if (!m_options.shotDir.isEmpty()) {
        runShots();
        return;
    }
    if (m_options.updateCheckOnly) {
        runUpdateCheckOnly();
        return;
    }
    if (!m_options.e2eMode.isEmpty()) {
        runUpdateE2E();
        return;
    }
    startNormal();
}

void AppController::startNormal()
{
    m_player = new MpvClient(this);
    if (!m_player->initialize())
        Log::error(QStringLiteral("libmpv 初始化失败：%1").arg(m_player->loadError()));
    connect(m_player, &MpvClient::errorOccurred, this, [](const QString &message) {
        Log::error(QStringLiteral("播放器：%1").arg(message));
    });
    connect(m_player, &MpvClient::fileLoaded, this, []() {
        Log::info(QStringLiteral("文件已加载"));
    });

    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);

    m_windows = new WindowController(m_player, m_updater, this);
    if (!m_windows->start(1180, 700)) {
        Log::error(QStringLiteral("窗口初始化失败"));
        QCoreApplication::exit(4);
        return;
    }

    QString path = m_options.openPath;
    if (!path.isEmpty())
        m_windows->openFile(QUrl::fromLocalFile(path));

    if (m_options.selftest) {
        QTimer::singleShot(700, this, [this]() {
            if (!m_options.videoPath.isEmpty())
                m_windows->openFile(QUrl::fromLocalFile(m_options.videoPath));
            else
                m_player->openTestPattern();
            waitMs(900);
            QJsonObject report = SelfTest::run(m_windows, m_player, m_options.reportPath);
            const QString overall = report.value(QStringLiteral("verdicts")).toObject()
                                        .value(QStringLiteral("overall")).toString();
            QCoreApplication::exit(overall == QLatin1String("PASS") ? 0 : 2);
        });
    }
}

void AppController::runShots()
{
    QDir().mkpath(m_options.shotDir);
    UpdateService demo;
    QQuickView view;
    // 不用 offscreen 平台截图：offscreen + software 后端的字体引擎拿不到系统字体，中文会变方块。
    // 这里用不抢焦点的工具窗 + 全透明不透明度：窗口在屏上被正常合成（字体与渲染路径真实），
    // 但用户看不到、不进任务栏；grabWindow() 走 FBO 离屏渲染，取到完整一帧。
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
                  | Qt::WindowDoesNotAcceptFocus);
    view.setColor(QColor(QStringLiteral("#0b0c10")));
    view.setOpacity(0.0);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.engine()->rootContext()->setContextProperty(QStringLiteral("updater"), &demo);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/SettingsPanel.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            Log::error(QStringLiteral("QML: %1").arg(error.toString()));
        finishWithCode(5);
        return;
    }
    view.resize(1440, 900);
    view.setPosition(0, 0);
    view.show();
    waitMs(200);
    if (view.rootObject())
        view.rootObject()->setProperty("shotMode", true);

    const QStringList states = {QStringLiteral("unchecked"), QStringLiteral("uptodate"),
                                QStringLiteral("available"), QStringLiteral("downloading"),
                                QStringLiteral("downloaded"), QStringLiteral("error")};
    int saved = 0;
    for (const QString &state : states) {
        demo.setDemoState(state);
        waitMs(220);
        const QImage image = view.grabWindow();
        const QString path = m_options.shotDir + QStringLiteral("/update-settings-%1.png").arg(state);
        if (!image.isNull() && image.save(path)) {
            ++saved;
            Log::info(QStringLiteral("已保存截图 %1").arg(path));
        } else {
            Log::error(QStringLiteral("截图失败：%1").arg(path));
        }
    }
    finishWithCode(saved == states.size() ? 0 : 6);
}

void AppController::finishWithCode(int code)
{
    // start() 在 app.exec() 之前被调用，此时直接 QCoreApplication::exit() 不会生效，
    // 事件循环起来后会一直挂着；用排队回调把退出码带到事件循环里再退出。
    QTimer::singleShot(0, qApp, [code]() { QCoreApplication::exit(code); });
}

void AppController::runUpdateCheckOnly()
{
    Log::info(QStringLiteral("update-check-only: 开始"));
    UpdateService *service = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    m_updater = service; // writeReport 依赖 m_updater
    connect(service, &UpdateService::finished, this, [this, service](const QString &state) {
        if (state == QLatin1String("checking"))
            return;
        QJsonObject report;
        report.insert(QStringLiteral("mode"), QStringLiteral("update-check-only"));
        report.insert(QStringLiteral("feed"), m_options.updateUrl);
        report.insert(QStringLiteral("state"), state);
        report.insert(QStringLiteral("errorCode"), service->errorCode());
        report.insert(QStringLiteral("errorMessage"), service->errorMessage());
        report.insert(QStringLiteral("currentVersion"), service->currentVersion());
        report.insert(QStringLiteral("availableVersion"), service->availableVersion());
        report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
        writeReport(report);
        QCoreApplication::exit(0);
    });
    QTimer::singleShot(30000, this, []() { QCoreApplication::exit(7); });
    service->checkForUpdates();
}

void AppController::runUpdateE2E()
{
    if (!m_options.restartMarker.isEmpty())
        qputenv("PALUNE_E2E_MARKER", m_options.restartMarker.toUtf8());
    m_updater = new UpdateService(QUrl(m_options.updateUrl), m_options.downloadDir, this);
    connect(m_updater, &UpdateService::finished, this, [this](const QString &state) {
        QJsonObject transition;
        transition.insert(QStringLiteral("state"), state);
        transition.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        transition.insert(QStringLiteral("mirror"), m_updater->mirrorName());
        transition.insert(QStringLiteral("progress"), m_updater->progress());
        m_transitions.append(transition);

        if (state == QLatin1String("available")) {
            m_updater->selectMirror(1); // 使用第一个加速镜像，验证通道切换
            QTimer::singleShot(60, this, [this]() {
                if (m_options.e2eMode == QLatin1String("full"))
                    m_updater->startUpdate();
                else
                    m_updater->startDownloadOnly();
            });
        } else if (state == QLatin1String("downloaded")) {
            writeReport(QJsonObject());
            m_updater->installDownloaded();
        } else if (state == QLatin1String("installing")) {
            writeReport(QJsonObject());
        } else if (state == QLatin1String("error")) {
            writeReport(QJsonObject());
            QCoreApplication::exit(2);
        }
    });
    QTimer::singleShot(120000, this, []() { QCoreApplication::exit(3); });
    m_updater->checkForUpdates();
}

void AppController::writeReport(const QJsonObject &extra)
{
    QJsonObject report = extra;
    if (!report.contains(QStringLiteral("mode")))
        report.insert(QStringLiteral("mode"), QStringLiteral("update-e2e:%1").arg(m_options.e2eMode));
    report.insert(QStringLiteral("feed"), m_options.updateUrl);
    report.insert(QStringLiteral("downloadDir"), m_options.downloadDir);
    report.insert(QStringLiteral("currentVersion"), QStringLiteral(PALUNE_VERSION));
    report.insert(QStringLiteral("availableVersion"), m_updater->availableVersion());
    report.insert(QStringLiteral("state"), m_updater->state());
    report.insert(QStringLiteral("mirror"), m_updater->mirrorName());
    report.insert(QStringLiteral("mirrorIndex"), m_updater->mirrorIndex());
    report.insert(QStringLiteral("downloadedPath"), m_updater->downloadedPath());
    report.insert(QStringLiteral("expectedSha256"), m_updater->expectedSha256());
    report.insert(QStringLiteral("actualSha256"), hashFile(m_updater->downloadedPath()));
    report.insert(QStringLiteral("errorCode"), m_updater->errorCode());
    report.insert(QStringLiteral("errorMessage"), m_updater->errorMessage());
    report.insert(QStringLiteral("transitions"), m_transitions);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    report.insert(QStringLiteral("logPath"), Log::filePath());
    report.insert(QStringLiteral("restartMarker"), m_options.restartMarker);
    QDir().mkpath(QFileInfo(m_options.reportPath).absolutePath());
    QFile f(m_options.reportPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    else
        Log::error(QStringLiteral("无法写入 e2e 报告：%1").arg(m_options.reportPath));
}

QString AppController::hashFile(const QString &path)
{
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return QString::fromLatin1(hash.result().toHex());
}

void AppController::writeRestartMarker()
{
    if (m_options.restartMarker.isEmpty())
        return;
    QJsonObject marker;
    marker.insert(QStringLiteral("restartedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    marker.insert(QStringLiteral("version"), QStringLiteral(PALUNE_VERSION));
    marker.insert(QStringLiteral("pid"), double(QCoreApplication::applicationPid()));
    marker.insert(QStringLiteral("applicationPath"), QCoreApplication::applicationFilePath());
    marker.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(QCoreApplication::arguments().mid(1)));
    QDir().mkpath(QFileInfo(m_options.restartMarker).absolutePath());
    QFile f(m_options.restartMarker);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(marker).toJson(QJsonDocument::Indented));
    Log::info(QStringLiteral("已写入重启标记：%1").arg(m_options.restartMarker));
}

} // namespace palune
