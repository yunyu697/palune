#include "media/MediaLibraryService.h"

#include "diag/Log.h"

namespace palune {

MediaLibraryService::MediaLibraryService(QObject *parent)
    : QObject(parent)
{
}

void MediaLibraryService::registerAdapter(std::shared_ptr<MediaServerAdapter> adapter)
{
    if (!adapter)
        return;
    for (const auto &existing : m_adapters) {
        if (existing->id() == adapter->id()) {
            Log::warn(QStringLiteral("媒体库适配器重复注册：%1").arg(adapter->id()));
            return;
        }
    }
    m_adapters.push_back(std::move(adapter));
    Log::info(QStringLiteral("媒体库适配器已注册：%1").arg(m_adapters.back()->id()));
    emit adaptersChanged();
}

void MediaLibraryService::unregisterAdapter(const QString &id)
{
    for (int i = 0; i < m_adapters.size(); ++i) {
        if (m_adapters.at(i)->id() == id) {
            m_adapters.removeAt(i);
            emit adaptersChanged();
            return;
        }
    }
}

void MediaLibraryService::refresh()
{
    // M1：不发网络请求。空库状态下刷新是无操作，只记录一次。
    Log::debug(QStringLiteral("媒体库刷新（M1 空实现，适配器 %1 个）").arg(adapterCount()));
    emit libraryChanged();
}

} // namespace palune
