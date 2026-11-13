#pragma once

#include "media/MediaServerAdapter.h"

#include <QStringList>

namespace palune {

// Plex 适配器：PIN 登录 + 服务器资源发现 + 媒体库 + 直连播放 + 时间线/已看上报。
//
// Plex Pass 限制（真实存在，不能靠代码绕过）：
// - 通过 Plex 中继（relay）或异地访问时的远程播放，需要账号具备 Plex Pass；
// - 本机/同网段直连（connections 里 local=true）不受此限制。
// 因此配对阶段会优先选直连地址，并把是否只能走中继写进能力字段，由 UI 提示。
class PlexAdapter : public MediaServerAdapter
{
public:
    struct Pin {
        bool ok = false;
        qint64 id = 0;
        QString code;
        QString authUrl;
        QString error;
    };

    struct ServerResource {
        QString name;
        QString clientIdentifier;
        QString accessToken;
        QStringList uris;      // 直连优先，中继在后
        bool remoteOnly = false; // 只能走中继/远程
    };

    PlexAdapter();

    Kind kind() const override;
    QString id() const override { return QStringLiteral("plex"); }
    QString displayName() const override { return QStringLiteral("Plex"); }

    // secret 为 X-Plex-Token；baseUrl 为选中服务器地址。
    AuthResult authenticate(const QUrl &baseUrl, const QString &user,
                            const QString &secret) override;
    CatalogPage fetchItems(const QString &libraryId, int offset, int limit) override;
    QUrl playbackUrl(const MediaItem &item, PlaybackMode *mode) const override;
    ProgressResult reportProgress(const QString &sessionId, const MediaItem &item,
                                  const UserState &state) override;
    MediaCapability capabilities() const override { return m_caps; }

    // 1) 创建 PIN（用户用浏览器完成授权）
    Pin createPin() const;
    // 2) 轮询 PIN 拿到 token
    QString pollPin(qint64 pinId, const QString &code, QString *error) const;
    // 3) 用账号 token 发现可用服务器
    QList<ServerResource> listServers(const QString &accountToken, QString *error) const;

    void restoreSession(const QUrl &baseUrl, const QString &token);

private:
    QString clientId() const;
    QString tokenParam() const;

    QUrl m_base;
    QString m_token;
    QString m_sectionKey;
    MediaCapability m_caps;
};

} // namespace palune
