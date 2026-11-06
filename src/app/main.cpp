#include "app/AppController.h"
#include "diag/Log.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

using namespace palune;

namespace {

bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0)
            return true;
    }
    return false;
}

QString valueArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc - 1; ++i) {
        if (qstrcmp(argv[i], name) == 0)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "basic");
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QCoreApplication::setApplicationName(QStringLiteral("Palune"));
    QCoreApplication::setOrganizationName(QStringLiteral("Palune"));

#if defined(Q_OS_WIN)
    // 隐藏桌面必须由启动器用 STARTUPINFO.lpDesktop 指定：mpv 的 VO 线程不会继承
    // SetThreadDesktop 的结果，线程级切换会让 wid 子窗创建失败。这里只记录当前桌面。
    if (hasArg(argc, argv, "--hidden-desktop")) {
        HDESK desk = GetThreadDesktop(GetCurrentThreadId());
        wchar_t deskName[256] = {};
        DWORD needed = 0;
        if (desk && GetUserObjectInformationW(desk, UOI_NAME, deskName, sizeof(deskName), &needed))
            qputenv("PALUNE_M0_DESKTOP", QString::fromWCharArray(deskName).toUtf8());
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(PALUNE_VERSION));
    app.setApplicationDisplayName(QStringLiteral("Palune"));

    const QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/logs/palune.log");
    Log::init(logPath);
    Log::info(QStringLiteral("Palune %1 启动").arg(QStringLiteral(PALUNE_VERSION)));
    Log::info(QStringLiteral("日志：%1").arg(logPath));

    AppController::Options options;
    options.selftest = hasArg(argc, argv, "--m0-selftest");
    options.videoPath = valueArg(argc, argv, "--video");
    options.reportPath = valueArg(argc, argv, "--report");
    options.shotDir = valueArg(argc, argv, "--ui-shot");
    options.updateUrl = valueArg(argc, argv, "--update-url");
    options.downloadDir = valueArg(argc, argv, "--download-dir");
    options.e2eMode = valueArg(argc, argv, "--e2e-mode");
    options.updateCheckOnly = hasArg(argc, argv, "--update-check");
    options.restarted = hasArg(argc, argv, "--e2e-restarted");
    options.restartMarker = valueArg(argc, argv, "--e2e-marker");
    options.hiddenDesktop = hasArg(argc, argv, "--hidden-desktop");
    options.openPath = valueArg(argc, argv, "--open");

    // 位置参数作为待打开文件（跳过已知的取值参数，避免把 flag 的值误当文件）
    const char *valueFlags[] = {"--video", "--report", "--ui-shot", "--update-url",
                                "--download-dir", "--e2e-mode", "--e2e-marker", "--open"};
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        bool isValueFlag = false;
        for (const char *flag : valueFlags) {
            if (arg == QLatin1String(flag)) {
                isValueFlag = true;
                ++i; // 跳过该 flag 的值
                break;
            }
        }
        if (isValueFlag || arg.startsWith(QLatin1Char('-')))
            continue;
        options.openPath = arg;
    }
    if (options.reportPath.isEmpty() && options.selftest)
        options.reportPath = QDir::currentPath() + QStringLiteral("/m0-report.json");

    AppController controller(options);
    controller.start();
    return app.exec();
}
