#include "app/AppController.h"

#include "core/AppPaths.h"
#include "core/StartupClock.h"
#include "diag/CrashBreadcrumb.h"
#include "diag/Log.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include <QUrlQuery>

#include <clocale>

#include <cstdio>

#if defined(Q_OS_WIN)
#include <shobjidl.h>
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

#if defined(Q_OS_WIN)
void registerUrlScheme()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\palune", 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    const wchar_t description[] = L"URL:Palune Protocol";
    RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(description),
                   sizeof(description));
    const wchar_t protocol[] = L"";
    RegSetValueExW(key, L"URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE *>(protocol),
                   sizeof(protocol));
    RegCloseKey(key);

    HKEY commandKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\palune\\shell\\open\\command",
                        0, nullptr, 0, KEY_WRITE, nullptr, &commandKey, nullptr) != ERROR_SUCCESS) {
        return;
    }
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1"));
    RegSetValueExW(commandKey, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(command.utf16()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(commandKey);
}
#endif

// palune://play?url=... —— 只接受一个 url 参数，交给正常播放入口。
QString resolveProtocolUrl(const QString &path)
{
    if (!path.startsWith(QLatin1String("palune://")))
        return path;
    const QUrl url(path);
    const QUrlQuery query(url);
    const QString target = query.queryItemValue(QStringLiteral("url"), QUrl::FullyDecoded);
    return target.isEmpty() ? QString() : target;
}

} // namespace

int main(int argc, char *argv[])
{
    StartupClock::mark();
    if (hasArg(argc, argv, "--version")) {
        const QString version = QStringLiteral(PALUNE_VERSION);
        std::printf("Palune %s\n", version.toUtf8().constData());
        return 0;
    }

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

    // libmpv 要求 LC_NUMERIC 为 C；macOS CI 等环境可能给出非 C 的 numeric locale，
    // 会导致 mpv_create 直接失败。Qt 的格式化走 QLocale，不受这里影响。
    std::setlocale(LC_NUMERIC, "C");

    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(PALUNE_VERSION));
    app.setApplicationDisplayName(QStringLiteral("Palune"));

    // 测试与自动化可以用 PALUNE_CONFIG_DIR 把配置、日志、崩溃目录重定向到临时目录。
    const QString configOverride = qEnvironmentVariable("PALUNE_CONFIG_DIR");
    if (!configOverride.isEmpty())
        AppPaths::setConfigDirOverride(configOverride);

    Settings settings;
    QString settingsError;
    settings.load(&settingsError);

    Log::init(AppPaths::logFile());
    if (!settingsError.isEmpty())
        Log::warn(settingsError);
    if (!Log::setMinimumLevelName(settings.rawLogLevel()))
        Log::warn(QStringLiteral("settings.toml 中的 logLevel 非法，沿用 info"));
    Log::info(QStringLiteral("Palune %1 启动").arg(QStringLiteral(PALUNE_VERSION)));
    Log::info(QStringLiteral("配置目录：%1").arg(AppPaths::configDir()));
    Log::info(QStringLiteral("日志级别：%1，日志：%2").arg(Log::minimumLevelName(), Log::filePath()));
    CrashBreadcrumb::install(QStringLiteral(PALUNE_VERSION));
    CrashBreadcrumb::record(QStringLiteral("进程启动"));

#if defined(Q_OS_WIN)
    SetCurrentProcessExplicitAppUserModelID(L"app.palune.player");
#endif

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
    options.m1Smoke = hasArg(argc, argv, "--m1-smoke");
    options.m1Perf = hasArg(argc, argv, "--m1-perf");
    options.m1ThemeSwitch = hasArg(argc, argv, "--m1-theme-switch");
    options.m1Soak = hasArg(argc, argv, "--m1-soak");
    options.mediaPath = valueArg(argc, argv, "--media");
    if (hasArg(argc, argv, "--switch-count"))
        options.switchCount = valueArg(argc, argv, "--switch-count").toInt();
    if (hasArg(argc, argv, "--soak-minute"))
        options.soakMinutes = valueArg(argc, argv, "--soak-minute").toInt();

    options.m2StateSwitch = hasArg(argc, argv, "--m2-stateswitch");
    options.m2Compact = hasArg(argc, argv, "--m2-compact");
    options.m2ConfigCheck = hasArg(argc, argv, "--m2-config-check");
    options.m2Actions = hasArg(argc, argv, "--m2-actions");
    options.m2UiPerf = hasArg(argc, argv, "--m2-ui-perf");
    options.m2Window = hasArg(argc, argv, "--m2-window");
    options.resumeProbe = hasArg(argc, argv, "--resume-probe");
    options.shotMatrix = hasArg(argc, argv, "--shot-matrix");
    options.backdropPath = valueArg(argc, argv, "--shot-backdrop");
    const QString shotSize = valueArg(argc, argv, "--shot-size");
    if (!shotSize.isEmpty()) {
        const QStringList parts = shotSize.split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0)
            options.shotSize = QSize(parts.at(0).toInt(), parts.at(1).toInt());
    }
    if (hasArg(argc, argv, "--state-switch-count"))
        options.stateSwitchCount = valueArg(argc, argv, "--state-switch-count").toInt();

    const bool automation = options.selftest || options.updateCheckOnly || !options.e2eMode.isEmpty()
        || options.m1Smoke || options.m1Perf || options.m1ThemeSwitch || options.m1Soak
        || options.m2StateSwitch || options.m2Compact || options.m2ConfigCheck
        || options.m2Actions
        || options.m2UiPerf
        || options.m2Window
        || !options.shotDir.isEmpty();

    // 位置参数作为待打开文件（跳过已知的取值参数，避免把 flag 的值误当文件）
    const char *valueFlags[] = {"--video", "--report", "--ui-shot", "--update-url",
                                "--download-dir", "--e2e-mode", "--e2e-marker", "--open",
                                "--media", "--switch-count", "--soak-minute",
                                "--state-switch-count", "--shot-backdrop", "--shot-size",
                                "--lang"};
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
    options.openPath = resolveProtocolUrl(options.openPath);

    if (options.reportPath.isEmpty() && options.selftest)
        options.reportPath = QDir::currentPath() + QStringLiteral("/m0-report.json");

#if defined(Q_OS_WIN)
    if (!automation) {
        registerUrlScheme();
        CrashBreadcrumb::record(QStringLiteral("已确认 palune:// 协议注册"));
    }
#endif

    // 界面语言：默认跟随系统；非中文环境加载英文翻译（QML 文案用 qsTr 标记）。
    QTranslator translator;
    QString language = valueArg(argc, argv, "--lang");
    if (language.isEmpty()) {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        language = uiLanguages.isEmpty() ? QString() : uiLanguages.value(0);
    }
    if (!language.startsWith(QLatin1String("zh")) && translator.load(
            QStringLiteral(":/i18n/palune_en_US.qm"))) {
        app.installTranslator(&translator);
    }
    Log::info(QStringLiteral("界面语言：%1").arg(language));

    AppController controller(options, &settings);
    controller.start();
    return app.exec();
}
