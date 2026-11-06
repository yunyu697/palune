#include "diag/CrashBreadcrumb.h"

#include "core/AppPaths.h"
#include "diag/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <csignal>
#include <cstdio>

namespace palune {

namespace {
QMutex s_mutex;
QStringList s_items;
QByteArray s_snapshot;
QString s_version;
QString s_breadcrumbPath;

void writeSnapshot()
{
    QFile file(s_breadcrumbPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    file.write(s_snapshot);
}

// 只在致命信号里使用：库函数调用有限，写到固定路径后恢复默认处理器。
void fatalHandler(int signal)
{
    const QString path = AppPaths::crashDir()
        + QStringLiteral("/crash-%1.json")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    if (FILE *out = std::fopen(path.toUtf8().constData(), "w")) {
        std::fwrite(s_snapshot.constData(), 1, static_cast<size_t>(s_snapshot.size()), out);
        std::fclose(out);
    }
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

QStringList snapshotLines()
{
    QMutexLocker lock(&s_mutex);
    return s_items;
}
} // namespace

QString CrashBreadcrumb::breadcrumbFile()
{
    return AppPaths::crashDir() + QStringLiteral("/breadcrumbs.txt");
}

QString CrashBreadcrumb::crashDirectory()
{
    return AppPaths::crashDir();
}

void CrashBreadcrumb::install(const QString &version)
{
    {
        QMutexLocker lock(&s_mutex);
        s_version = version;
        s_breadcrumbPath = breadcrumbFile();
        QDir().mkpath(crashDirectory());
        s_snapshot = QByteArrayLiteral("{\"version\":\"") + version.toUtf8()
                     + QByteArrayLiteral("\",\"note\":\"启动崩溃面包屑（仅保留最近 50 个动作）\",\"actions\":[]}\n");
    }
    writeSnapshot();
    std::signal(SIGSEGV, fatalHandler);
    std::signal(SIGABRT, fatalHandler);
    std::signal(SIGFPE, fatalHandler);
    std::signal(SIGILL, fatalHandler);
}

void CrashBreadcrumb::record(const QString &action)
{
    QStringList lines;
    {
        QMutexLocker lock(&s_mutex);
        s_items << QStringLiteral("%1 %2")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), action);
        while (s_items.size() > 50)
            s_items.removeFirst();
        lines = s_items;

        QByteArray text = QStringLiteral("{\"version\":\"%1\",\"installedAt\":\"%2\",\"actions\":[\n")
                              .arg(s_version, QDateTime::currentDateTime().toString(Qt::ISODate))
                              .toUtf8();
        for (int i = 0; i < lines.size(); ++i) {
            QString escaped = lines.at(i);
            escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            text += QByteArrayLiteral("  \"") + escaped.toUtf8() + QByteArrayLiteral("\"");
            text += (i + 1 < lines.size()) ? QByteArrayLiteral(",\n") : QByteArrayLiteral("\n");
        }
        text += QByteArrayLiteral("]}\n");
        s_snapshot = text;
    }
    writeSnapshot();
}

QStringList CrashBreadcrumb::recent()
{
    return snapshotLines();
}

} // namespace palune
