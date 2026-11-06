#include "core/StartupClock.h"

namespace palune {

namespace {
QElapsedTimer &timer()
{
    static QElapsedTimer t;
    return t;
}
} // namespace

void StartupClock::mark()
{
    timer().start();
}

qint64 StartupClock::elapsedMs()
{
    return timer().isValid() ? timer().elapsed() : 0;
}

} // namespace palune
