// 媒体服务器适配器真机联调测试。
// 默认跳过；设置以下环境变量后才连接真实服务器：
//   PALUNE_LIVE_JELLYFIN_URL   例如 http://127.0.0.1:8096
//   PALUNE_LIVE_JELLYFIN_USER  用户名
//   PALUNE_LIVE_JELLYFIN_PASS  密码
// 断言链：认证 → 媒体库列表 → 海报可下载 → 直链可播 → 进度回写服务端。
#include <QtTest>

#include "media/JellyfinAdapter.h"
#include "media/MediaServerHttp.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace palune;

namespace {

QString liveUrl() { return qEnvironmentVariable("PALUNE_LIVE_JELLYFIN_URL"); }
QString liveUser() { return qEnvironmentVariable("PALUNE_LIVE_JELLYFIN_USER"); }
QString livePass() { return qEnvironmentVariable("PALUNE_LIVE_JELLYFIN_PASS"); }

bool liveConfigured()
{
    return !liveUrl().isEmpty() && !liveUser().isEmpty() && !livePass().isEmpty();
}

QJsonObject fetchItemUserData(const QUrl &base, const QString &token, const QString &userId,
                              const QString &itemId)
{
    const QUrl url(base.toString()
                   + QStringLiteral("/Users/%1/Items/%2?Fields=UserData").arg(userId, itemId));
    const auto resp = http::request("GET", url, {{"X-Emby-Token", token.toUtf8()}});
    if (!resp.ok())
        return {};
    return QJsonDocument::fromJson(resp.body).object()
        .value(QLatin1String("UserData")).toObject();
}

} // namespace

class TstAdapterLive : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinEndToEnd();
    void otherJellyfinKinds();
};

// 主链路：连接、认证、媒体库、海报、直链、进度回写。
void TstAdapterLive::jellyfinEndToEnd()
{
    if (!liveConfigured())
        QSKIP("未配置 PALUNE_LIVE_JELLYFIN_* 环境变量，跳过真机联调");

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    QCOMPARE(adapter.id(), QStringLiteral("jellyfin"));

    const auto auth = adapter.authenticate(QUrl(liveUrl()), liveUser(), livePass());
    QVERIFY2(auth.ok, qPrintable(auth.error));
    QVERIFY(!auth.token.isEmpty());
    QVERIFY(!auth.userId.isEmpty());

    const auto caps = adapter.capabilities();
    QVERIFY(caps.auth);
    QVERIFY(caps.catalog);
    QVERIFY(caps.playback);
    QVERIFY(caps.progress);
    QVERIFY(caps.images);
    QVERIFY2(!caps.serverVersion.isEmpty(), "认证后应探测到服务端版本");

    // 1) 媒体库列表
    const auto page = adapter.fetchItems(QString(), 0, 50);
    QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
    QVERIFY2(!page.items.isEmpty(), "服务器上应至少有一个媒体条目");

    // 选时长最长的一条：Jellyfin 默认 MinResumeDurationSeconds=300，
    // 短于 5 分钟的条目不会保存续播位置，只记为已看。
    MediaItem item = page.items.first();
    for (const MediaItem &candidate : page.items) {
        if (candidate.durationSeconds > item.durationSeconds)
            item = candidate;
    }
    QVERIFY(!item.id.isEmpty());
    QVERIFY(!item.name.isEmpty());
    QVERIFY(item.durationSeconds > 0.0);
    QVERIFY2(item.durationSeconds >= 300.0,
             "真机样例片需 ≥ 300 秒，否则 Jellyfin 不写续播位置（MinResumeDurationSeconds）");

    // 2) 海报可下载
    QVERIFY(!item.posterUrl.isEmpty());
    const auto poster = http::request("GET", item.posterUrl, {});
    QVERIFY2(poster.ok(), qPrintable(QStringLiteral("海报下载失败：HTTP %1").arg(poster.status)));
    QVERIFY2(poster.body.size() > 1024, "海报响应过小，疑似占位图");
    const bool jpeg = poster.body.startsWith("\xFF\xD8");
    const bool png = poster.body.startsWith("\x89PNG");
    QVERIFY2(jpeg || png, "海报不是 JPEG/PNG 数据");

    // 3) 直链可播：HEAD 或带 Range 的 GET 必须拿得到媒体字节
    PlaybackMode mode = PlaybackMode::Transcode;
    const QUrl stream = adapter.playbackUrl(item, &mode);
    QCOMPARE(mode, PlaybackMode::DirectPlay);
    QVERIFY(stream.toString().contains(QStringLiteral("/Videos/")));
    const auto head = http::request("GET", stream, {{"Range", "bytes=0-1023"}});
    QVERIFY2(head.ok(), qPrintable(QStringLiteral("直链请求失败：HTTP %1 %2")
                                      .arg(head.status).arg(head.error)));
    QVERIFY2(head.body.size() > 100, "直链返回字节数不足");

    // 4) 进度回写：先上报播放中，再上报停止，服务端 UserData 必须记录位置
    constexpr double kPositionSeconds = 60.0;
    const QString sessionId = QStringLiteral("palune-live-%1")
                                  .arg(QDateTime::currentMSecsSinceEpoch());
    {
        UserState state;
        state.itemId = item.id;
        state.positionSeconds = kPositionSeconds * 0.5;
        const auto progress = adapter.reportProgress(sessionId, item, state);
        QVERIFY2(progress.ok, qPrintable(progress.error));
    }
    {
        UserState state;
        state.itemId = item.id;
        state.positionSeconds = kPositionSeconds;
        state.played = true;
        const auto stopped = adapter.reportProgress(sessionId, item, state);
        QVERIFY2(stopped.ok, qPrintable(stopped.error));
    }

    const QUrl base = QUrl(liveUrl());
    const QJsonObject userData = fetchItemUserData(base, auth.token, auth.userId, item.id);
    const double ticks = userData.value(QLatin1String("PlaybackPositionTicks")).toDouble();
    QVERIFY2(ticks > 0.0, "服务端未记录播放位置");
    QVERIFY2(qAbs(ticks / 10000000.0 - kPositionSeconds) < 2.0,
             qPrintable(QStringLiteral("回写位置偏差过大：%1 秒").arg(ticks / 10000000.0)));
    QVERIFY(!userData.value(QLatin1String("LastPlayedDate")).toString().isEmpty());
}

// 极空间 / 飞牛按「其他 Jellyfin」复用同一条协议路径：
// 同一台真实 Jellyfin 服务器上换 Kind，认证与媒体库必须照样打通。
void TstAdapterLive::otherJellyfinKinds()
{
    if (!liveConfigured())
        QSKIP("未配置 PALUNE_LIVE_JELLYFIN_* 环境变量，跳过真机联调");

    const QList<QPair<MediaServerAdapter::Kind, QString>> kinds{
        {MediaServerAdapter::Kind::Emby, QStringLiteral("emby")},
        {MediaServerAdapter::Kind::ZhiSpace, QStringLiteral("zhispace")},
        {MediaServerAdapter::Kind::FeiNiu, QStringLiteral("feiniu")},
    };
    for (const auto &entry : kinds) {
        JellyfinAdapter adapter(entry.first);
        QCOMPARE(adapter.id(), entry.second);
        QVERIFY(!adapter.displayName().isEmpty());

        const auto auth = adapter.authenticate(QUrl(liveUrl()), liveUser(), livePass());
        QVERIFY2(auth.ok, qPrintable(entry.second + QStringLiteral("：") + auth.error));

        const auto page = adapter.fetchItems(QString(), 0, 10);
        QVERIFY2(page.error.isEmpty(),
                 qPrintable(entry.second + QStringLiteral("：") + page.error));
        QVERIFY2(!page.items.isEmpty(),
                 qPrintable(entry.second + QStringLiteral("：媒体库为空")));
    }
}

QTEST_GUILESS_MAIN(TstAdapterLive)
#include "tst_adapter_live.moc"
