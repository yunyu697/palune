#pragma once

#include <QString>
#include <QStringList>

namespace palune {

// 崩溃面包屑：环形缓冲 + 每次动作落盘，进程异常终止后可从
// <配置目录>/crashes/breadcrumbs.txt 还原最近 50 个动作与版本信息。
// 同时安装致命信号处理器，尽力写出一份 crash-<时间>.json（骨架实现，M5 换正式转储）。
class CrashBreadcrumb
{
public:
    static void install(const QString &version);
    static void record(const QString &action);
    static QStringList recent();
    static QString breadcrumbFile();
    static QString crashDirectory();
};

} // namespace palune
