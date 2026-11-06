#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>

namespace palune {

// 极简分级日志：只写本地文件，默认不上传。
class Log
{
public:
    enum Level { Debug, Info, Warn, Error };

    static void init(const QString &filePath);
    static void write(Level level, const QString &message);
    static QString filePath();

    static void debug(const QString &m) { write(Debug, m); }
    static void info(const QString &m) { write(Info, m); }
    static void warn(const QString &m) { write(Warn, m); }
    static void error(const QString &m) { write(Error, m); }

private:
    static QMutex s_mutex;
    static QFile s_file;
    static QString s_path;
};

} // namespace palune
