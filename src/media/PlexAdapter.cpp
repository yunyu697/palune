#include "media/PlexAdapter.h"

#include "media/MediaServerHttp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace palune {
namespace {

// PIN 与资源发现固定访问 plex.tv。自动化测试通过 PALUNE_PLEX_TV_BASE 指向本机
// 回环 mock；生产环境不设置该变量，行为与直连 plex.tv 完全一致。
QString plexTvBase()
{
    const QString injected = qEnvironmentVariable("PALUNE_PLEX_TV_BASE");
    return injected.isEmpty() ? QStringLiteral("https://plex.tv") : injected;
}

http::Headers plexHeaders()
{
    return {
        {"Accept", "application/json"},
        {"X-Plex-Product", "Palune"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", "palune-desktop-1"},
        {"X-Plex-Platform", "Windows"},
        {"X-Plex-Device", "Desktop"},
    };
}

QString joinUrl(const QUrl &base, const QString &path)
{
    return base.toString() + path;
}

// Plex 的时间字段是毫秒。
double msToSeconds(const QString &value)
{
    return value.toDouble() / 1000.0;
}

} // namespace

PlexAdapter::PlexAdapter() = default;

MediaServerAdapter::Kind PlexAdapter::kind() const
{
    return Kind::Plex;
}

QString PlexAdapter::clientId() const
{
    return QStringLiteral("palune-desktop-1");
}

QString PlexAdapter::tokenParam() const
{
    return QStringLiteral("X-Plex-Token=%1").arg(m_token);
}

void PlexAdapter::restoreSession(const QUrl &baseUrl, const QString &token)
{
    m_base = baseUrl;
    m_token = token;
    m_caps.auth = !token.isEmpty();
    m_caps.catalog = m_caps.auth;
    m_caps.playback = m_caps.auth;
    m_caps.progress = m_caps.auth;
    m_caps.images = m_caps.auth;
    m_caps.supportsHttps = baseUrl.scheme() == QLatin1String("https");
}

PlexAdapter::Pin PlexAdapter::createPin() const
{
    Pin pin;
    const http::Headers headers = plexHeaders();
    const auto resp = http::request(
        "POST", QUrl(QStringLiteral("%1/api/v2/pins?strong=true").arg(plexTvBase())), headers);
    if (!resp.ok()) {
        pin.error = resp.error.isEmpty()
                        ? QStringLiteral("创建 PIN 失败（HTTP %1）").arg(resp.status)
                        : resp.error;
        return pin;
    }
    const QJsonObject root = QJsonDocument::fromJson(resp.body).object();
    pin.id = static_cast<qint64>(root.value(QLatin1String("id")).toDouble());
    pin.code = root.value(QLatin1String("code")).toString();
    if (pin.id == 0 || pin.code.isEmpty()) {
        pin.error = QStringLiteral("PIN 响应缺少 id/code");
        return pin;
    }
    pin.authUrl =
        QStringLiteral("https://app.plex.tv/auth#?clientID=%1&code=%2&context%%5Bdevice%%5D%%5Bproduct%%5D=Palune")
            .arg(clientId(), pin.code);
    pin.ok = true;
    return pin;
}

QString PlexAdapter::pollPin(qint64 pinId, const QString &code, QString *error) const
{
    const QUrl url(QStringLiteral("%1/api/v2/pins/%2?X-Plex-Token=%3")
                       .arg(plexTvBase(), QString::number(pinId), code));
    const auto resp = http::request("GET", url, plexHeaders());
    if (!resp.ok()) {
        if (error)
            *error = resp.error.isEmpty()
                         ? QStringLiteral("查询 PIN 失败（HTTP %1）").arg(resp.status)
                         : resp.error;
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(resp.body).object();
    const QString token = root.value(QLatin1String("authToken")).toString();
    if (token.isEmpty() && error)
        *error = QStringLiteral("尚未在浏览器完成授权");
    return token;
}

QList<PlexAdapter::ServerResource> PlexAdapter::listServers(const QString &accountToken,
                                                            QString *error) const
{
    QList<ServerResource> out;
    const QUrl url(QStringLiteral("%1/api/v2/resources?includeHttps=1&includeRelay=1")
                       .arg(plexTvBase()));
    http::Headers headers = plexHeaders();
    headers.append({"X-Plex-Token", accountToken.toUtf8()});
    const auto resp = http::request("GET", url, headers);
    if (!resp.ok()) {
        if (error)
            *error = resp.error.isEmpty()
                         ? QStringLiteral("发现服务器失败（HTTP %1）").arg(resp.status)
                         : resp.error;
        return out;
    }

    for (const auto &value : QJsonDocument::fromJson(resp.body).array()) {
        const QJsonObject obj = value.toObject();
        if (!obj.value(QLatin1String("provides")).toString().contains(QLatin1String("server")))
            continue;
        ServerResource res;
        res.name = obj.value(QLatin1String("name")).toString();
        res.clientIdentifier = obj.value(QLatin1String("clientIdentifier")).toString();
        res.accessToken = obj.value(QLatin1String("accessToken")).toString();
        if (res.accessToken.isEmpty())
            res.accessToken = accountToken;
        QStringList localUris, remoteUris;
        for (const auto &connValue : obj.value(QLatin1String("connections")).toArray()) {
            const QJsonObject conn = connValue.toObject();
            const QString uri = conn.value(QLatin1String("uri")).toString();
            if (uri.isEmpty())
                continue;
            if (conn.value(QLatin1String("local")).toBool())
                localUris.append(uri);
            else
                remoteUris.append(uri);
        }
        res.uris = localUris + remoteUris;
        res.remoteOnly = localUris.isEmpty();
        if (!res.uris.isEmpty())
            out.append(res);
    }
    return out;
}

MediaServerAdapter::AuthResult PlexAdapter::authenticate(const QUrl &baseUrl,
                                                         const QString &user,
                                                         const QString &secret)
{
    Q_UNUSED(user)
    AuthResult out;
    QString urlError;
    const QUrl base = http::normalizeBase(baseUrl.toString(), &urlError);
    if (base.isEmpty()) {
        out.error = urlError;
        return out;
    }
    if (secret.trimmed().isEmpty()) {
        out.error = QStringLiteral("Plex 需要 X-Plex-Token（可先用 PIN 登录换取）");
        return out;
    }

    // 用 /identity 验证 token 与地址是否可用（返回机器标识与版本）。
    const QUrl url(joinUrl(base, QStringLiteral("/identity")));
    http::Headers headers = plexHeaders();
    headers.append({"X-Plex-Token", secret.toUtf8()});
    const auto resp = http::request("GET", url, headers);
    if (!resp.ok()) {
        out.error = resp.error.isEmpty()
                        ? QStringLiteral("连接 Plex 服务器失败（HTTP %1）").arg(resp.status)
                        : resp.error;
        return out;
    }

    m_base = base;
    m_token = secret;
    m_caps.auth = true;
    m_caps.catalog = true;
    m_caps.playback = true;
    m_caps.progress = true;
    m_caps.images = true;
    m_caps.supportsHttps = base.scheme() == QLatin1String("https");

    QXmlStreamReader xml(resp.body);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement
            && xml.name() == QLatin1String("MediaContainer")) {
            m_caps.serverVersion = xml.attributes()
                                       .value(QLatin1String("version")).toString();
        }
    }

    out.ok = true;
    out.token = secret;
    out.userId = base.host();
    return out;
}

MediaServerAdapter::CatalogPage PlexAdapter::fetchItems(const QString &libraryId,
                                                        int offset, int limit)
{
    CatalogPage page;
    if (m_token.isEmpty()) {
        page.error = QStringLiteral("尚未登录");
        return page;
    }

    http::Headers headers = plexHeaders();
    headers.append({"X-Plex-Token", m_token.toUtf8()});

    // 先取分区（没有缓存过时），再按分区拉全部条目。
    if (m_sectionKey.isEmpty() || !libraryId.isEmpty()) {
        const auto sections = http::request(
            "GET", QUrl(joinUrl(m_base, QStringLiteral("/library/sections"))), headers);
        if (!sections.ok()) {
            page.error = sections.error.isEmpty()
                             ? QStringLiteral("拉取分区失败（HTTP %1）").arg(sections.status)
                             : sections.error;
            return page;
        }
        QString preferred = libraryId;
        QXmlStreamReader xml(sections.body);
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement
                && xml.name() == QLatin1String("Directory")) {
                const auto attrs = xml.attributes();
                const QString key = attrs.value(QLatin1String("key")).toString();
                const QString type = attrs.value(QLatin1String("type")).toString();
                if (preferred.isEmpty()
                    && (type == QLatin1String("movie") || type == QLatin1String("show")))
                    preferred = key;
            }
        }
        if (preferred.isEmpty()) {
            page.error = QStringLiteral("该 Plex 服务器没有电影/剧集分区");
            return page;
        }
        m_sectionKey = preferred;
    }

    const auto all = http::request(
        "GET", QUrl(joinUrl(m_base, QStringLiteral("/library/sections/%1/all").arg(m_sectionKey))),
        headers);
    if (!all.ok()) {
        page.error = all.error.isEmpty()
                         ? QStringLiteral("拉取媒体库失败（HTTP %1）").arg(all.status)
                         : all.error;
        return page;
    }

    QList<MediaItem> parsed;
    QXmlStreamReader xml(all.body);
    MediaItem current;
    bool inItem = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("Video")) {
                const auto attrs = xml.attributes();
                current = MediaItem{};
                current.id = attrs.value(QLatin1String("ratingKey")).toString();
                current.libraryId = m_sectionKey;
                current.name = attrs.value(QLatin1String("title")).toString();
                current.seriesName =
                    attrs.value(QLatin1String("grandparentTitle")).toString();
                current.type = attrs.value(QLatin1String("type")).toString()
                                       == QLatin1String("episode")
                                   ? QStringLiteral("episode")
                                   : QStringLiteral("movie");
                current.season = attrs.value(QLatin1String("parentIndex")).toInt();
                current.episode = attrs.value(QLatin1String("index")).toInt();
                current.durationSeconds =
                    msToSeconds(attrs.value(QLatin1String("duration")).toString());
                const QString thumb = attrs.value(QLatin1String("thumb")).toString();
                if (!thumb.isEmpty())
                    current.posterUrl = QUrl(joinUrl(
                        m_base, QStringLiteral("%1?%2").arg(thumb, tokenParam())));
                inItem = true;
            } else if (inItem && xml.name() == QLatin1String("Part")) {
                const auto attrs = xml.attributes();
                const QString key = attrs.value(QLatin1String("key")).toString();
                if (!key.isEmpty())
                    current.streamUrl = QUrl(joinUrl(
                        m_base, QStringLiteral("%1?download=1&%2").arg(key, tokenParam())));
                current.container = attrs.value(QLatin1String("container")).toString();
                current.directPlayable = true;
            } else if (inItem && xml.name() == QLatin1String("Stream")) {
                const auto attrs = xml.attributes();
                const QString type = attrs.value(QLatin1String("streamType")).toString();
                const QString codec = attrs.value(QLatin1String("codec")).toString();
                if (type == QLatin1String("1") && current.videoCodec.isEmpty())
                    current.videoCodec = codec;
                else if (type == QLatin1String("2") && current.audioCodec.isEmpty())
                    current.audioCodec = codec;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("Video") && inItem) {
                parsed.append(current);
                inItem = false;
            }
        }
    }

    const int from = qMax(0, offset);
    const int count = qMax(1, limit);
    for (int i = from; i < parsed.size() && i < from + count; ++i)
        page.items.append(parsed.at(i));
    page.hasMore = from + page.items.size() < parsed.size();
    return page;
}

QUrl PlexAdapter::playbackUrl(const MediaItem &item, PlaybackMode *mode) const
{
    if (mode)
        *mode = PlaybackMode::DirectPlay;
    if (!item.streamUrl.isEmpty())
        return item.streamUrl;
    return {};
}

MediaServerAdapter::ProgressResult PlexAdapter::reportProgress(
    const QString &sessionId, const MediaItem &item, const UserState &state)
{
    Q_UNUSED(sessionId)
    ProgressResult out;
    if (m_token.isEmpty()) {
        out.error = QStringLiteral("尚未登录");
        return out;
    }

    http::Headers headers = plexHeaders();
    headers.append({"X-Plex-Token", m_token.toUtf8()});

    if (state.played) {
        const QUrl scrobble(joinUrl(
            m_base,
            QStringLiteral("/:/scrobble?key=%1&identifier=com.plexapp.plugins.library&%2")
                .arg(item.id, tokenParam())));
        const auto resp = http::request("GET", scrobble, headers);
        out.ok = resp.ok();
        if (!out.ok)
            out.error = resp.error.isEmpty()
                            ? QStringLiteral("标记已看失败（HTTP %1）").arg(resp.status)
                            : resp.error;
        return out;
    }

    const QString ms = QString::number(
        static_cast<qint64>(qMax(0.0, state.positionSeconds) * 1000.0));
    const QString duration = QString::number(
        static_cast<qint64>(qMax(0.0, item.durationSeconds) * 1000.0));
    const QUrl timeline(joinUrl(
        m_base,
        QStringLiteral("/:/timeline?ratingKey=%1&key=/library/metadata/%1&state=playing"
                       "&time=%2&duration=%3&%4")
            .arg(item.id, ms, duration, tokenParam())));
    const auto resp = http::request("GET", timeline, headers);
    out.ok = resp.ok();
    if (!out.ok)
        out.error = resp.error.isEmpty()
                        ? QStringLiteral("时间线上报失败（HTTP %1）").arg(resp.status)
                        : resp.error;
    return out;
}

} // namespace palune
