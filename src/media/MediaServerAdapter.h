#pragma once

#include "media/Types.h"

#include <QUrl>

namespace palune {

// 内容源适配器契约（M1 冻结接口，M6 才写具体协议）。
// 五组能力：auth / catalog / playback / progress / capability。
// 硬约束（静态检查）：本目录不得包含 mpv 调用，也不得依赖 QML。
class MediaServerAdapter
{
public:
    enum class Kind {
        Jellyfin,
        Emby,
        Plex,
        ZhiSpace,
        FeiNiu,
    };

    struct AuthResult {
        bool ok = false;
        QString userId;
        QString token;
        QString error;
    };

    struct CatalogPage {
        QList<MediaItem> items;
        bool hasMore = false;
        QString error;
    };

    struct ProgressResult {
        bool ok = false;
        QString error;
    };

    virtual ~MediaServerAdapter() = default;

    virtual Kind kind() const = 0;
    // 稳定标识（写进配置与日志，不允许随展示名变化）。
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    // 1) 认证
    virtual AuthResult authenticate(const QUrl &baseUrl, const QString &user, const QString &secret) = 0;
    // 2) 目录（分页拉取）
    virtual CatalogPage fetchItems(const QString &libraryId, int offset, int limit) = 0;
    // 3) 播放（返回直链；mode 说明客户端应走的播放路径）
    virtual QUrl playbackUrl(const MediaItem &item, PlaybackMode *mode) const = 0;
    // 4) 进度（幂等上报，sessionId 由调用方生成）
    virtual ProgressResult reportProgress(const QString &sessionId,
                                          const MediaItem &item,
                                          const UserState &state) = 0;
    // 5) 能力探测（配对期完成，UI 据此隐藏不可用入口）
    virtual MediaCapability capabilities() const = 0;
};

} // namespace palune
