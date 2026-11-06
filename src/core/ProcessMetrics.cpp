#include "core/ProcessMetrics.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#else
#include <QFile>
#include <unistd.h>
#endif

namespace palune {

double currentRssMb()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return -1.0;
    return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
#elif defined(Q_OS_MACOS)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
#else
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly))
        return -1.0;
    const QString text = QString::fromLatin1(statm.readAll()).trimmed();
    const QStringList parts = text.split(QLatin1Char(' '));
    if (parts.size() < 2)
        return -1.0;
    const double pages = parts.at(1).toDouble();
    return pages * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
}

} // namespace palune
