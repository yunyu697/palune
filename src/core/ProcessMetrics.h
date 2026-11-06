#pragma once

#include <QString>

namespace palune {

// 进程内存占用（常驻集），用于 M1 性能判据留证。取不到时返回 -1。
double currentRssMb();

} // namespace palune
