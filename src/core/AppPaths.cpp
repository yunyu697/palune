#include "core/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace palune {

namespace {
QString s_override;

QString homeDir()
{
    return QDir::homePath();
}
} // namespace

void AppPaths::setConfigDirOverride(const QString &dir)
{
    s_override = dir;
}

QString AppPaths::configDir()
{
    if (!s_override.isEmpty())
        return s_override;
#if defined(Q_OS_WIN)
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        return QDir::toNativeSeparators(appData + QStringLiteral("/Palune"));
    // 极少数环境没有 APPDATA（服务账号），退回 Qt 的 AppDataLocation。
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#elif defined(Q_OS_MACOS)
    return homeDir() + QStringLiteral("/Library/Application Support/Palune");
#else
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty())
        return xdg + QStringLiteral("/palune");
    return homeDir() + QStringLiteral("/.config/palune");
#endif
}

QString AppPaths::settingsFile()
{
    return configDir() + QStringLiteral("/settings.toml");
}

QString AppPaths::logDir()
{
    return configDir() + QStringLiteral("/logs");
}

QString AppPaths::logFile()
{
    return logDir() + QStringLiteral("/palune.log");
}

QString AppPaths::crashDir()
{
    return configDir() + QStringLiteral("/crashes");
}

QString AppPaths::stateFile()
{
    return configDir() + QStringLiteral("/state.db");
}

QString AppPaths::updateStagingDir(const QString &version)
{
#if defined(Q_OS_WIN)
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString base = local.isEmpty() ? configDir() : QDir::toNativeSeparators(local + QStringLiteral("/Palune"));
#else
    const QString base = configDir();
#endif
    return base + QStringLiteral("/updates/") + version;
}

} // namespace palune
