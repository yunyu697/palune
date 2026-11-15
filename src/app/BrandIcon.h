#pragma once

#include <QIcon>
#include <QSize>

#include <initializer_list>

namespace palune {

// 品牌图标入口：所有入口从同一份 PNG 阶梯取图，避免某处只拿到一张 256 图被系统硬缩。
inline QIcon brandIcon(std::initializer_list<int> sizes)
{
    QIcon icon;
    for (int size : sizes) {
        icon.addFile(QStringLiteral(":/resources/brand/png/palune-%1.png").arg(size),
                     QSize(size, size));
    }
    return icon;
}

// 窗口 / 任务栏 / exe 关联图标：多尺寸，Windows 按 DPI 自取，macOS 用 512/1024。
inline QIcon brandAppIcon()
{
    return brandIcon({16, 24, 32, 48, 64, 128, 256, 512, 1024});
}

// 托盘图标：
// - macOS 菜单栏只收单色 template 图，彩色 App 图标会被系统按原色显示，与菜单栏风格冲突；
// - Windows 托盘实际显示 16/20/24px，给 16–64 的小尺寸阶梯，避免从 256 缩糊。
inline QIcon brandTrayIcon()
{
#if defined(Q_OS_MACOS)
    QIcon icon;
    icon.addFile(QStringLiteral(":/resources/brand/png/tray-template-18.png"), QSize(18, 18));
    icon.addFile(QStringLiteral(":/resources/brand/png/tray-template-36.png"), QSize(36, 36));
    icon.setIsMask(true);
    return icon;
#else
    return brandIcon({16, 24, 32, 48, 64});
#endif
}

} // namespace palune
