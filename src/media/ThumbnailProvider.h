#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace palune {

// 缩略图：Windows 走资源管理器缩略图提供程序（IShellItemImageFactory），
// 无缩略图处理器（如部分 MKV/HEVC）时返回空图，由调用方回退到占位海报。
// 其他平台返回空图。
class ThumbnailProvider
{
public:
    static QImage load(const QString &path, const QSize &size);
    static bool save(const QString &sourcePath, const QString &targetFile, const QSize &size);
};

} // namespace palune
