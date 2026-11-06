#pragma once

#include <QElapsedTimer>

namespace palune {

// 进程启动计时：main() 在构造 QApplication 前 mark()，性能判据从进程开始算冷启动。
class StartupClock
{
public:
    static void mark();
    static qint64 elapsedMs();
};

} // namespace palune
