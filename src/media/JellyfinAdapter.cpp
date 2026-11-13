#include "media/JellyfinAdapter.h"

#include "media/MediaServerHttp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace palune {
namespace {

QString kindId(MediaServerAdapter::Kind kind)
{
    switch (kind) {
    case MediaServerAdapter::Kind::Jellyfin:
        return QStringLiteral("jellyfin");
    case MediaServerAdapter::Kind::Emby:
        return QStringLiteral("emby");
    case MediaServerAdapter::Kind::ZhiSpace:
        return QStringLiteral("zhispace");
    case MediaServerAdapter::Kind::FeiNiu:
        return QStringLiteral("feiniu");
    case MediaServerAdapter::Kind::OtherJellyfin:
        return QStringLiteral("jellyfin-compat");
    case MediaServerAdapter::Kind::Plex:
        return QStringLiteral("plex");
    }
    return QStringLiteral("jellyfin");
}

QString joinUrl(const QUrl &base, const QString &path)
{
    return base.toString() + path;
}

double ticksToSeconds(qint64 ticks)
{
    return static_cast<double>(ticks) / 10000000.0;
}

QString firstMediaSourceId(const QJsonObject &item)
{
    const QJsonArray sources = item.value(QLatin1String("MediaSources")).toArray();
    if (sources.isEmpty())
        return {};
    return sources.first().toObject().value(QLatin1String("Id")).toString();
}

} // namespace

JellyfinAdapter::JellyfinAdapter(Kind kind, QUrl baseUrl)
    : m_kind(kind)
    , m_base(std::move(baseUrl))
{
}

MediaServerAdapter::Kind JellyfinAdapter::kind() const
{
    return m_kind;
}

QString JellyfinAdapter::id() const
{
    return kindId(m_kind);
}

QString JellyfinAdapter::displayName() const
{
    switch (m_kind) {
    case Kind::Emby:
        return QStringLiteral("Emby");
    case Kind::ZhiSpace:
        return QStringLiteral("极空间（Jellyfin 协议）");
    case Kind::FeiNiu:
        return QStringLiteral("飞牛（Jellyfin 协议）");
    case Kind::OtherJellyfin:
        return QStringLiteral("其他 Jellyfin（极空间/飞牛）");
    case Kind::Jellyfin:
    case Kind::Plex:
        break;
    }
    return QStringLiteral("Jellyfin");
}

QString JellyfinAdapter::deviceId() const
{
    // 稳定设备标识：同一安装固定，服务端据此区分会话。
    return QStringLiteral("palune-desktop-1");
}

QByteArray JellyfinAdapter::authHeader() const
{
    const QString value = QStringLiteral(
                              "MediaBrowser Client=\"Palune\", Device=\"Windows\", "
                              "DeviceId=\"%1\", Version=\"1.0.0\"")
                              .arg(deviceId());
    return value.toUtf8();
}

void JellyfinAdapter::restoreSession(const QUrl &baseUrl, const QString &userId,
                                     const QString &token)
{
    m_base = baseUrl;
    m_userId = userId;
    m_token = token;
    m_caps.auth = !token.isEmpty();
    m_caps.catalog = m_caps.auth;
    m_caps.playback = m_caps.auth;
    m_caps.progress = m_caps.auth;
    m_caps.images = m_caps.auth;
    m_caps.supportsHttps = baseUrl.scheme() == QLatin1String("https");
}

MediaServerAdapter::AuthResult JellyfinAdapter::authenticate(const QUrl &baseUrl,
                                                             const QString &user,
                                                             const QString &secret)
{
    AuthResult out;
    QString urlError;
    const QUrl base = http::normalizeBase(baseUrl.toString(), &urlError);
    if (base.isEmpty()) {
        out.error = urlError;
        return out;
    }

    const QJsonObject payload{{QStringLiteral("Username"), user},
                              {QStringLiteral("Pw"), secret}};
    const http::Headers headers{
        {"Content-Type", "application/json"},
        {"X-Emby-Authorization", authHeader()},
    };
    const auto resp = http::request("POST", QUrl(joinUrl(base, QStringLiteral("/Users/AuthenticateByName"))),
                                    headers, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (!resp.ok()) {
        // 有 HTTP 状态码就是服务器答复了（401/403 → 认证失败）；状态为 0 表示传输层
        // 根本没连上，交给设置页显示「不可达」。
        if (resp.status > 0) {
            out.error = QStringLiteral("认证失败（HTTP %1）").arg(resp.status);
            if (!resp.error.isEmpty())
                out.error += QStringLiteral("：%1").arg(resp.error);
        } else {
            out.error = resp.error.isEmpty() ? QStringLiteral("无法连接服务器") : resp.error;
        }
        return out;
    }

    const QJsonObject root = QJsonDocument::fromJson(resp.body).object();
    const QString token = root.value(QLatin1String("AccessToken")).toString();
    const QString userId = root.value(QLatin1String("User")).toObject()
                               .value(QLatin1String("Id")).toString();
    if (token.isEmpty() || userId.isEmpty()) {
        out.error = QStringLiteral("认证响应缺少 AccessToken/UserId");
        return out;
    }

    m_base = base;
    m_userId = userId;
    m_token = token;

    probeCapabilities(base, token);

    out.ok = true;
    out.userId = userId;
    out.token = token;
    return out;
}

void JellyfinAdapter::probeCapabilities(const QUrl &base, const QString &token)
{
    // 能力探测：/System/Info 需要鉴权，失败再退回 /System/Info/Public；失败不影响登录。
    QString version;
    const http::Headers authed{
        {"X-Emby-Authorization", authHeader()},
        {"X-Emby-Token", token.toUtf8()},
    };
    const auto info = http::request("GET", QUrl(joinUrl(base, QStringLiteral("/System/Info"))),
                                    authed);
    if (info.ok())
        version = QJsonDocument::fromJson(info.body).object()
                      .value(QLatin1String("Version")).toString();
    else {
        const auto pub = http::request(
            "GET", QUrl(joinUrl(base, QStringLiteral("/System/Info/Public"))));
        if (pub.ok())
            version = QJsonDocument::fromJson(pub.body).object()
                          .value(QLatin1String("Version")).toString();
    }

    m_caps.auth = true;
    m_caps.catalog = true;
    m_caps.playback = true;
    m_caps.progress = true;
    m_caps.images = true;
    m_caps.supportsHttps = base.scheme() == QLatin1String("https");
    m_caps.serverVersion = version;
    m_caps.audioCodecs = {QStringLiteral("aac"), QStringLiteral("ac3"),
                          QStringLiteral("eac3"), QStringLiteral("flac"),
                          QStringLiteral("dts")};
    m_caps.subtitleFormats = {QStringLiteral("srt"), QStringLiteral("ass"),
                              QStringLiteral("pgs")};
}

MediaServerAdapter::AuthResult JellyfinAdapter::authenticateWithApiKey(const QUrl &baseUrl,
                                                                       const QString &apiKey)
{
    AuthResult out;
    QString urlError;
    const QUrl base = http::normalizeBase(baseUrl.toString(), &urlError);
    if (base.isEmpty()) {
        out.error = urlError;
        return out;
    }
    const QString key = apiKey.trimmed();
    if (key.isEmpty()) {
        out.error = QStringLiteral("API Key 为空");
        return out;
    }

    const http::Headers headers{
        {"X-Emby-Authorization", authHeader()},
        {"X-Emby-Token", key.toUtf8()},
    };
    const auto me = http::request("GET", QUrl(joinUrl(base, QStringLiteral("/Users/Me"))),
                                  headers);
    if (!me.ok()) {
        if (me.status > 0) {
            out.error = QStringLiteral("认证失败（HTTP %1）").arg(me.status);
            if (!me.error.isEmpty())
                out.error += QStringLiteral("：%1").arg(me.error);
        } else {
            out.error = me.error.isEmpty() ? QStringLiteral("无法连接服务器") : me.error;
        }
        return out;
    }
    const QString userId = QJsonDocument::fromJson(me.body).object()
                               .value(QLatin1String("Id")).toString();
    if (userId.isEmpty()) {
        out.error = QStringLiteral("API Key 响应缺少 UserId");
        return out;
    }

    m_base = base;
    m_userId = userId;
    m_token = key;
    probeCapabilities(base, key);

    out.ok = true;
    out.userId = userId;
    out.token = key;
    return out;
}

MediaServerAdapter::CatalogPage JellyfinAdapter::fetchItems(const QString &libraryId,
                                                            int offset, int limit)
{
    CatalogPage page;
    if (m_token.isEmpty() || m_userId.isEmpty()) {
        page.error = QStringLiteral("尚未登录");
        return page;
    }

    QUrl url(joinUrl(m_base, QStringLiteral("/Users/%1/Items").arg(m_userId)));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("IncludeItemTypes"),
                   QStringLiteral("Movie,Episode"));
    q.addQueryItem(QStringLiteral("Fields"),
                   QStringLiteral("PrimaryImageAspectRatio,MediaSources,RunTimeTicks,"
                                  "SeriesName,IndexNumber,ParentIndexNumber"));
    q.addQueryItem(QStringLiteral("ImageTypeLimit"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("EnableImageTypes"), QStringLiteral("Primary"));
    q.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    q.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
    q.addQueryItem(QStringLiteral("Limit"), QString::number(qMax(1, limit)));
    q.addQueryItem(QStringLiteral("StartIndex"), QString::number(qMax(0, offset)));
    if (!libraryId.isEmpty())
        q.addQueryItem(QStringLiteral("ParentId"), libraryId);
    url.setQuery(q);

    const http::Headers headers{{"X-Emby-Token", m_token.toUtf8()}};
    const auto resp = http::request("GET", url, headers);
    if (!resp.ok()) {
        page.error = resp.error.isEmpty()
                         ? QStringLiteral("拉取媒体库失败（HTTP %1）").arg(resp.status)
                         : resp.error;
        return page;
    }

    const QJsonObject root = QJsonDocument::fromJson(resp.body).object();
    const QJsonArray items = root.value(QLatin1String("Items")).toArray();
    for (const auto &value : items) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value(QLatin1String("Type")).toString();
        if (type != QLatin1String("Movie") && type != QLatin1String("Episode"))
            continue;

        MediaItem item;
        item.id = obj.value(QLatin1String("Id")).toString();
        item.libraryId = libraryId;
        item.name = obj.value(QLatin1String("Name")).toString();
        item.seriesName = obj.value(QLatin1String("SeriesName")).toString();
        item.type = type == QLatin1String("Movie") ? QStringLiteral("movie")
                                                   : QStringLiteral("episode");
        item.season = obj.value(QLatin1String("ParentIndexNumber")).toInt();
        item.episode = obj.value(QLatin1String("IndexNumber")).toInt();
        item.durationSeconds =
            ticksToSeconds(static_cast<qint64>(obj.value(QLatin1String("RunTimeTicks"))
                                                   .toDouble()));
        const QJsonObject source =
            obj.value(QLatin1String("MediaSources")).toArray().first().toObject();
        item.container = source.value(QLatin1String("Container")).toString();
        for (const auto &stream : source.value(QLatin1String("MediaStreams")).toArray()) {
            const QJsonObject s = stream.toObject();
            const QString streamType = s.value(QLatin1String("Type")).toString();
            if (streamType == QLatin1String("Video") && item.videoCodec.isEmpty())
                item.videoCodec = s.value(QLatin1String("Codec")).toString();
            else if (streamType == QLatin1String("Audio") && item.audioCodec.isEmpty())
                item.audioCodec = s.value(QLatin1String("Codec")).toString();
        }
        item.directPlayable = true;
        item.posterUrl = QUrl(joinUrl(
            m_base, QStringLiteral("/Items/%1/Images/Primary?maxHeight=480&quality=90")
                        .arg(item.id)));
        item.streamUrl = QUrl(joinUrl(
            m_base, QStringLiteral("/Videos/%1/stream?static=true&api_key=%2&MediaSourceId=%3")
                        .arg(item.id, m_token, firstMediaSourceId(obj))));
        page.items.append(item);
    }

    const int total = root.value(QLatin1String("TotalRecordCount")).toInt();
    page.hasMore = total > 0 && (offset + page.items.size()) < total;
    return page;
}

QUrl JellyfinAdapter::playbackUrl(const MediaItem &item, PlaybackMode *mode) const
{
    if (mode)
        *mode = PlaybackMode::DirectPlay;
    if (!item.streamUrl.isEmpty())
        return item.streamUrl;
    return QUrl(joinUrl(
        m_base, QStringLiteral("/Videos/%1/stream?static=true&api_key=%2")
                    .arg(item.id, m_token)));
}

MediaServerAdapter::ProgressResult JellyfinAdapter::reportProgress(
    const QString &sessionId, const MediaItem &item, const UserState &state)
{
    ProgressResult out;
    if (m_token.isEmpty()) {
        out.error = QStringLiteral("尚未登录");
        return out;
    }

    const qint64 ticks =
        static_cast<qint64>(qMax(0.0, state.positionSeconds) * 10000000.0);
    QJsonObject payload{
        {QStringLiteral("ItemId"), item.id},
        {QStringLiteral("PlaySessionId"), sessionId},
        {QStringLiteral("PositionTicks"), ticks},
        {QStringLiteral("IsPaused"), false},
        {QStringLiteral("IsMuted"), false},
        {QStringLiteral("CanSeek"), true},
        {QStringLiteral("PlayMethod"), QStringLiteral("DirectPlay")},
        {QStringLiteral("EventName"), QStringLiteral("timeupdate")},
    };
    QString path = QStringLiteral("/Sessions/Playing/Progress");
    if (state.played) {
        path = QStringLiteral("/Sessions/Playing/Stopped");
        payload.insert(QStringLiteral("EventName"), QStringLiteral("stop"));
    }

    const http::Headers headers{
        {"Content-Type", "application/json"},
        {"X-Emby-Token", m_token.toUtf8()},
        {"X-Emby-Authorization", authHeader()},
    };
    // Jellyfin 只对已开始的播放会话更新 UserData：先补一条 start，
    // 否则 /Sessions/Playing/Progress 只进内存 playstate，不落库。
    if (!state.played && !m_startedSessions.contains(sessionId)) {
        QJsonObject start = payload;
        start.insert(QStringLiteral("EventName"), QStringLiteral("playbackstart"));
        const auto began = http::request("POST",
                                         QUrl(joinUrl(m_base, QStringLiteral("/Sessions/Playing"))),
                                         headers,
                                         QJsonDocument(start).toJson(QJsonDocument::Compact));
        if (!began.ok()) {
            out.error = began.error.isEmpty()
                            ? QStringLiteral("开始播放上报失败（HTTP %1）").arg(began.status)
                            : began.error;
            return out;
        }
        m_startedSessions.insert(sessionId);
    }
    if (state.played)
        m_startedSessions.remove(sessionId);

    const auto resp = http::request("POST", QUrl(joinUrl(m_base, path)), headers,
                                    QJsonDocument(payload).toJson(QJsonDocument::Compact));
    out.ok = resp.ok();
    if (!out.ok) {
        out.error = resp.error.isEmpty()
                        ? QStringLiteral("进度上报失败（HTTP %1）").arg(resp.status)
                        : resp.error;
    }
    return out;
}

} // namespace palune
