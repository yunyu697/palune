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

void Log::init(const QString &filePath)
{
    QMutexLocker lock(&s_mutex);
    s_path = filePath;
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    s_file.setFileName(filePath);
    s_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Log::write(Level level, const QString &message)
{
    QMutexLocker lock(&s_mutex);
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
    return s_path;
}

} // namespace palune
