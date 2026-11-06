#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>

namespace palune {

// 分级日志：只写本地文件，默认不上传；级别由 settings.toml 的 [diag] logLevel 控制。
class Log
{
public:
    enum Level { Debug, Info, Warn, Error };

    static void init(const QString &filePath);
    static void write(Level level, const QString &message);
    static QString filePath();

    static void setMinimumLevel(Level level);
    static Level minimumLevel();
    // 接受 debug / info / warn / error（大小写不敏感），非法值返回 false。
    static bool setMinimumLevelName(const QString &name);
    static QString minimumLevelName();

    static void debug(const QString &m) { write(Debug, m); }
    static void info(const QString &m) { write(Info, m); }
    static void warn(const QString &m) { write(Warn, m); }
    static void error(const QString &m) { write(Error, m); }

private:
    static QMutex s_mutex;
    static QFile s_file;
    static QString s_path;
    static Level s_minimum;
};

} // namespace palune
