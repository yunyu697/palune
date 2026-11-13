#pragma once

#include "media/MediaServerAdapter.h"

#include <QSet>

namespace palune {

// Jellyfin 协议族适配器。Emby、极空间、飞牛的媒体服务接口与 Jellyfin 同源
// （/Users/AuthenticateByName、/Users/{id}/Items、/Videos/{id}/stream、
//  /Sessions/Playing*），因此共用同一套实现，只改展示名与稳定 id。
class JellyfinAdapter : public MediaServerAdapter
{
public:
    explicit JellyfinAdapter(Kind kind, QUrl baseUrl = {});

    Kind kind() const override;
    QString id() const override;
    QString displayName() const override;

    AuthResult authenticate(const QUrl &baseUrl, const QString &user,
                            const QString &secret) override;
    // API Key 登录：Jellyfin 控制台生成的密钥直接作为 X-Emby-Token，
    // 用 /Users/Me 反查 userId，供后续 /Users/{id}/Items 使用。
    AuthResult authenticateWithApiKey(const QUrl &baseUrl, const QString &apiKey);
    CatalogPage fetchItems(const QString &libraryId, int offset, int limit) override;
    QUrl playbackUrl(const MediaItem &item, PlaybackMode *mode) const override;
    ProgressResult reportProgress(const QString &sessionId, const MediaItem &item,
                                  const UserState &state) override;
    MediaCapability capabilities() const override { return m_caps; }

    // 恢复已保存的会话（启动时免密登录）。
    void restoreSession(const QUrl &baseUrl, const QString &userId, const QString &token);

    QString userId() const { return m_userId; }
    QString token() const { return m_token; }
    QUrl baseUrl() const { return m_base; }

private:
    void probeCapabilities(const QUrl &base, const QString &token);
    QByteArray authHeader() const;
    QByteArray tokenHeader() const { return "X-Emby-Token"; }
    QString deviceId() const;

    Kind m_kind;
    QUrl m_base;
    QString m_userId;
    QString m_token;
    QSet<QString> m_startedSessions;
    MediaCapability m_caps;
};

} // namespace palune
