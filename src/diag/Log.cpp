#include "diag/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>

namespace palune {

QMutex Log::s_mutex;
QFile Log::s_file;
QString Log::s_path;
Log::Level Log::s_minimum = Log::Info;

void Log::init(const QString &filePath)
{
    QMutexLocker lock(&s_mutex);
    s_path = filePath;
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    s_file.setFileName(filePath);
    s_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Log::setMinimumLevel(Level level)
{
    QMutexLocker lock(&s_mutex);
    s_minimum = level;
}

Log::Level Log::minimumLevel()
{
    QMutexLocker lock(&s_mutex);
    return s_minimum;
}

bool Log::setMinimumLevelName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("debug"))
        setMinimumLevel(Debug);
    else if (normalized == QLatin1String("info"))
        setMinimumLevel(Info);
    else if (normalized == QLatin1String("warn") || normalized == QLatin1String("warning"))
        setMinimumLevel(Warn);
    else if (normalized == QLatin1String("error"))
        setMinimumLevel(Error);
    else
        return false;
    return true;
}

QString Log::minimumLevelName()
{
    switch (minimumLevel()) {
    case Debug: return QStringLiteral("debug");
    case Info: return QStringLiteral("info");
    case Warn: return QStringLiteral("warn");
    case Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

void Log::write(Level level, const QString &message)
{
    QMutexLocker lock(&s_mutex);
    if (level < s_minimum)
        return;
    const char *tag = "DEBUG";
    switch (level) {
    case Info: tag = "INFO"; break;
    case Warn: tag = "WARN"; break;
    case Error: tag = "ERROR"; break;
    default: break;
    }
    const QString line = QStringLiteral("%1 [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), tag, message);
    if (s_file.isOpen()) {
        QTextStream ts(&s_file);
        ts << line << '\n';
        ts.flush();
    }
#ifdef QT_DEBUG
    qDebug().noquote() << line;
#endif
}

QString Log::filePath()
{
    QMutexLocker lock(&s_mutex);
    return s_path;
}

} // namespace palune
