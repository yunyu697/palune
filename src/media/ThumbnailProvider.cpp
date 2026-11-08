#include "media/ThumbnailProvider.h"

#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#endif

namespace palune {

QImage ThumbnailProvider::load(const QString &path, const QSize &size)
{
#if defined(Q_OS_WIN)
    if (!QFileInfo::exists(path))
        return {};

    const bool initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    IShellItemImageFactory *factory = nullptr;
    QImage image;
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HRESULT hr = SHCreateItemFromParsingName(native.c_str(), nullptr,
                                             IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr) && factory) {
        HBITMAP bitmap = nullptr;
        SIZE requested = {size.width(), size.height()};
        hr = factory->GetImage(requested, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK,
                               &bitmap);
        if (SUCCEEDED(hr) && bitmap) {
            image = QImage::fromHBITMAP(bitmap);
            DeleteObject(bitmap);
        }
        factory->Release();
    }
    if (initialized)
        CoUninitialize();
    return image;
#else
    Q_UNUSED(path)
    Q_UNUSED(size)
    return {};
#endif
}

bool ThumbnailProvider::save(const QString &sourcePath, const QString &targetFile, const QSize &size)
{
    QImage image = load(sourcePath, size);
    if (image.isNull())
        return false;
    // 等比铺满目标框后居中裁切，保证海报墙网格整齐。
    const QImage scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2,
                     size.width(), size.height());
    QDir().mkpath(QFileInfo(targetFile).absolutePath());
    return scaled.copy(crop).save(targetFile, "JPG", 86);
}

} // namespace palune
