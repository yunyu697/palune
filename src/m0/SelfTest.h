#pragma once

#include <QJsonObject>
#include <QString>

namespace palune {

class MpvClient;
class WindowController;

// M0 自测：全屏切换 20 次、DPI 缩放、Alt-Tab、Snap 贴边。
// 全部通过程序化驱动与几何/时差采样完成，不依赖人工点击；
// 在独立桌面运行，不占前台、不弹窗。
class SelfTest
{
public:
    static QJsonObject run(WindowController *windows, MpvClient *player, const QString &reportPath);
};

} // namespace palune
