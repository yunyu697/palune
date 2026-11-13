// 媒体服务器适配器 / 媒体库服务单测。
// 全部走 127.0.0.1 回环 mock 服务：不访问外网、不开 GUI、不弹窗、不播放音频。
// 断言依据是 JellyfinAdapter.cpp / PlexAdapter.cpp / MediaServerHttp.cpp 的实际请求。
#include <QtTest>

#include "core/AppPaths.h"
#include "core/CredentialStore.h"
#include "media/JellyfinAdapter.h"
#include "media/MediaLibraryService.h"
#include "media/MediaServerHttp.h"
#include "media/PlexAdapter.h"
#include "settings/Settings.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <functional>
#include <memory>

using namespace palune;

namespace {

// 回环 mock：按请求返回预置报文，并记录收到的每个请求（方法/路径/头/体）。
class MockServer : public QTcpServer
{
public:
    struct Record {
        QByteArray verb;
        QByteArray path; // path?query
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/json";
        bool drop = false; // 收了请求但不回复，用于触发调用方超时
    };

    using Handler = std::function<Response(const Record &)>;

    explicit MockServer(QObject *parent = nullptr) : QTcpServer(parent) {}

    bool start() { return listen(QHostAddress::LocalHost, 0); }

    QUrl base() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
    }

    void setHandler(Handler handler) { m_handler = std::move(handler); }

    // 当前 handler 收到的请求（按到达顺序）。
    QList<Record> requests;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readFrom(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QObject::destroyed, this, [this, socket]() { m_pending.remove(socket); });
    }

private:
    static QByteArray statusText(int status)
    {
        switch (status) {
        case 200:
            return "OK";
        case 204:
            return "No Content";
        case 401:
            return "Unauthorized";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        default:
            return "Status";
        }
    }

    static bool takeRequest(QByteArray &buffer, Record *record)
    {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return false;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        if (requestLine.size() < 2)
            return false;
        record->verb = requestLine.at(0);
        record->path = requestLine.at(1);
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            record->headers.insert(line.left(colon).toLower().trimmed(),
                                   line.mid(colon + 1).trimmed());
        }
        const int length = record->headers.value("content-length").toInt();
        if (buffer.size() < headerEnd + 4 + length)
            return false;
        record->body = buffer.mid(headerEnd + 4, length);
        buffer.remove(0, headerEnd + 4 + length);
        return true;
    }

    void readFrom(QTcpSocket *socket)
    {
        QByteArray &buffer = m_pending[socket];
        buffer += socket->readAll();
        if (!m_handler)
            return;
        Record record;
        if (!takeRequest(buffer, &record))
            return;
        requests << record;
        const Response response = m_handler(record);
        if (response.drop)
            return;
        QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + ' '
                         + statusText(response.status) + "\r\n";
        out += "Content-Type: " + response.contentType + "\r\n";
        out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
        out += "Connection: close\r\n\r\n";
        out += response.body;
        socket->write(out);
        socket->disconnectFromHost();
    }

    Handler m_handler;
    QHash<QTcpSocket *, QByteArray> m_pending;
};

MockServer::Response okJson(const QJsonObject &object)
{
    MockServer::Response response;
    response.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return response;
}

MockServer::Response okJson(const QJsonArray &array)
{
    MockServer::Response response;
    response.body = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return response;
}

MockServer::Response okXml(const QByteArray &xml)
{
    MockServer::Response response;
    response.body = xml;
    response.contentType = "application/xml";
    return response;
}

MockServer::Response failure(int status)
{
    MockServer::Response response;
    response.status = status;
    response.body = "{}";
    return response;
}

QString pathOf(const MockServer::Record &record)
{
    return QString::fromLatin1(record.path).section(QLatin1Char('?'), 0, 0);
}

QString queryOf(const MockServer::Record &record)
{
    const QString text = QString::fromLatin1(record.path);
    const int index = text.indexOf(QLatin1Char('?'));
    return index < 0 ? QString() : text.mid(index + 1);
}

QString queryValue(const MockServer::Record &record, const QString &key)
{
    return QUrlQuery(queryOf(record)).queryItemValue(key);
}

QJsonObject parseJson(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object();
}

// 只接受连接后立刻断开的本地服务：客户端会立即拿到传输层错误。
// 比「连到没人监听的端口」快得多（后者在 Windows 上会拖数秒），仍然不碰外网。
class RejectingServer : public QTcpServer
{
public:
    RejectingServer() { listen(QHostAddress::LocalHost, 0); }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->abort(); // 不给任何 HTTP 响应
        socket->deleteLater();
    }
};

QUrl deadUrl()
{
    static RejectingServer server;
    return server.url();
}

// Jellyfin 登录成功响应
QJsonObject jellyfinLoginBody()
{
    return QJsonObject{{QStringLiteral("AccessToken"), QStringLiteral("tok-123")},
                       {QStringLiteral("User"), QJsonObject{{QStringLiteral("Id"),
                                                            QStringLiteral("user-1")}}}};
}

// 一条电影（含 MediaSources / MediaStreams）
QJsonObject jellyfinMovieItem()
{
    return QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("movie-1")},
        {QStringLiteral("Name"), QStringLiteral("迷宫")},
        {QStringLiteral("Type"), QStringLiteral("Movie")},
        {QStringLiteral("RunTimeTicks"), 72000000000.0},
        {QStringLiteral("MediaSources"),
         QJsonArray{QJsonObject{
             {QStringLiteral("Id"), QStringLiteral("src-1")},
             {QStringLiteral("Container"), QStringLiteral("mkv")},
             {QStringLiteral("MediaStreams"),
              QJsonArray{QJsonObject{{QStringLiteral("Type"), QStringLiteral("Video")},
                                     {QStringLiteral("Codec"), QStringLiteral("h264")}},
                         QJsonObject{{QStringLiteral("Type"), QStringLiteral("Audio")},
                                     {QStringLiteral("Codec"), QStringLiteral("aac")}}}}}}},
    };
}

// 一集剧（无 MediaSources，走兜底分支）
QJsonObject jellyfinEpisodeItem()
{
    return QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("ep-1")},
        {QStringLiteral("Name"), QStringLiteral("第一集")},
        {QStringLiteral("Type"), QStringLiteral("Episode")},
        {QStringLiteral("SeriesName"), QStringLiteral("长剧")},
        {QStringLiteral("ParentIndexNumber"), 2},
        {QStringLiteral("IndexNumber"), 3},
        {QStringLiteral("RunTimeTicks"), 15000000.0},
    };
}

// 把 PALUNE_PLEX_TV_BASE 指向本地 mock（PlexAdapter 的固定域名注入点），析构时还原。
class PlexTvOverride
{
public:
    explicit PlexTvOverride(const QUrl &base)
        : m_previous(qgetenv("PALUNE_PLEX_TV_BASE"))
        , m_hadPrevious(qEnvironmentVariableIsSet("PALUNE_PLEX_TV_BASE"))
    {
        qputenv("PALUNE_PLEX_TV_BASE", base.toString().toUtf8());
    }

    ~PlexTvOverride()
    {
        if (m_hadPrevious)
            qputenv("PALUNE_PLEX_TV_BASE", m_previous);
        else
            qunsetenv("PALUNE_PLEX_TV_BASE");
    }

private:
    QByteArray m_previous;
    bool m_hadPrevious = false;
};

// 媒体库配置目录重定向（service 用 AppPaths::configDir() 定位索引与缩略图）。
class ConfigDirOverride
{
public:
    explicit ConfigDirOverride(const QString &dir) { AppPaths::setConfigDirOverride(dir); }
    ~ConfigDirOverride() { AppPaths::setConfigDirOverride(QString()); }
};

// 凭据测试用独立 target 名，测完删除，避免污染真实的 Palune/media-server 条目。
class CredentialCleanup
{
public:
    CredentialCleanup(QString service, QString account)
        : m_service(std::move(service)), m_account(std::move(account))
    {
    }
    ~CredentialCleanup() { credential::remove(m_service, m_account); }

private:
    QString m_service;
    QString m_account;
};

QByteArray plexIdentityXml(const QString &version)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                          "<MediaContainer size=\"1\" version=\"%1\">"
                          "<MachineIdentifier>machine-1</MachineIdentifier>"
                          "</MediaContainer>")
        .arg(version)
        .toUtf8();
}

QByteArray plexSectionsXml()
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<MediaContainer size=\"3\">"
                      "<Directory key=\"5\" type=\"artist\" title=\"music\"/>"
                      "<Directory key=\"1\" type=\"movie\" title=\"movies\"/>"
                      "<Directory key=\"2\" type=\"show\" title=\"shows\"/>"
                      "</MediaContainer>");
}

QByteArray plexMoviesXml()
{
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<MediaContainer size=\"2\">"
        "<Video ratingKey=\"101\" type=\"movie\" title=\"alien\" duration=\"5400000\""
        " thumb=\"/library/metadata/101/thumb/1\">"
        "<Media><Part key=\"/library/parts/101/file.mkv\" container=\"mkv\">"
        "<Stream streamType=\"1\" codec=\"hevc\"/><Stream streamType=\"2\" codec=\"eac3\"/>"
        "</Part></Media></Video>"
        "<Video ratingKey=\"102\" type=\"episode\" title=\"pilot\" grandparentTitle=\"series\""
        " parentIndex=\"1\" index=\"2\" duration=\"1200000\">"
        "<Media><Part key=\"/library/parts/102/file.mp4\" container=\"mp4\"/>"
        "<Stream streamType=\"1\" codec=\"h264\"/><Stream streamType=\"2\" codec=\"aac\"/>"
        "</Media></Video>"
        "</MediaContainer>");
}

} // namespace

class TestMediaServer : public QObject
{
    Q_OBJECT

private slots:
    // MediaServerHttp
    void httpNormalizeBase();
    void httpRequestVerbsAndHeaders();
    void httpRequestErrors();

    // JellyfinAdapter
    void jellyfinAuthenticateAndProbe();
    void jellyfinProbeFallsBackToPublic();
    void jellyfinAuthenticateFailures();
    void jellyfinApiKeyAuth();
    void jellyfinSessionIdentityAndPlaybackUrl();
    void jellyfinFetchItemsPaging();
    void jellyfinFetchItemsErrors();
    void jellyfinReportProgressLifecycle();
    void jellyfinReportProgressErrors();

    // PlexAdapter
    void plexCreatePin();
    void plexPollPin();
    void plexListServers();
    void plexAuthenticate();
    void plexFetchItems();
    void plexFetchItemsErrors();
    void plexPlaybackUrlAndProgress();

    // MediaLibraryService
    void mediaServiceViewModeAndRoots();
    void mediaServiceIndexAndThumbCache();
    void mediaServiceTrimThumbCache();
    void mediaServiceTestServerConnection();
    void mediaServiceAdapterRegistry();

    // CredentialStore
    void credentialStoreRoundTrip();
    void credentialStoreValidation();
};

// ---------------------------------------------------------------- MediaServerHttp

void TestMediaServer::httpNormalizeBase()
{
    // 成功路径只保证「不写入错误」，不要求清空调用方传入的残留值。
    QString error;
    const QUrl https = http::normalizeBase(QStringLiteral("  https://media.example:8920///  "), &error);
    QVERIFY(error.isEmpty());
    QVERIFY(https.isValid());
    QCOMPARE(https.toString(), QStringLiteral("https://media.example:8920"));

    const QUrl plain = http::normalizeBase(QStringLiteral("127.0.0.1:8096"), &error);
    QCOMPARE(plain.scheme(), QStringLiteral("http"));
    QCOMPARE(plain.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(plain.port(), 8096);

    // 多级路径的尾斜杠也要去掉，避免拼接出双斜杠。
    const QUrl nested = http::normalizeBase(QStringLiteral("http://host.example/jellyfin/"), &error);
    QCOMPARE(nested.toString(), QStringLiteral("http://host.example/jellyfin"));

    const QUrl empty = http::normalizeBase(QString(), &error);
    QVERIFY(empty.isEmpty());
    QCOMPARE(error, QStringLiteral("服务器地址为空"));

    const QUrl blank = http::normalizeBase(QStringLiteral("   "), nullptr);
    QVERIFY(blank.isEmpty());

    // 只给了 scheme 没有主机名：解析失败。
    error.clear();
    const QUrl broken = http::normalizeBase(QStringLiteral("http://"), &error);
    QVERIFY(broken.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("无法解析服务器地址")), qPrintable(error));
    QVERIFY(http::normalizeBase(QStringLiteral("http://"), nullptr).isEmpty());
}

void TestMediaServer::httpRequestVerbsAndHeaders()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (record.verb == "DELETE")
            return okJson(QJsonObject{});
        if (record.verb == "PUT")
            return MockServer::Response{200, "put-ok", "text/plain", false};
        return okJson(QJsonObject{{QStringLiteral("verb"), QString::fromLatin1(record.verb)}});
    });

    const auto get = http::request("GET", server.base());
    QCOMPARE(get.status, 200);
    QVERIFY(get.ok());
    QVERIFY(get.error.isEmpty());
    QCOMPARE(parseJson(get.body).value(QStringLiteral("verb")).toString(),
             QStringLiteral("GET"));

    const http::Headers headers{{"X-Test", "1"}, {"Accept", "application/json"}};
    const auto post = http::request("POST", server.base(), headers, "{\"a\":1}", 5000);
    QVERIFY(post.ok());
    QCOMPARE(server.requests.last().verb, QByteArray("POST"));
    QCOMPARE(server.requests.last().headers.value("x-test"), QByteArray("1"));
    QCOMPARE(server.requests.last().body, QByteArray("{\"a\":1}"));

    const auto put = http::request("PUT", server.base(), {}, "payload");
    QVERIFY(put.ok());
    QCOMPARE(put.body, QByteArray("put-ok"));

    const auto del = http::request("DELETE", server.base());
    QVERIFY(del.ok());
    QCOMPARE(server.requests.last().verb, QByteArray("DELETE"));
}

void TestMediaServer::httpRequestErrors()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/missing")
            return failure(404);
        return MockServer::Response{200, {}, "text/plain", true}; // 不回复，触发超时
    });

    const auto missing = http::request("GET", QUrl(server.base().toString() + "/missing"));
    QCOMPARE(missing.status, 404);
    QVERIFY(!missing.ok());
    QVERIFY(!missing.error.isEmpty());

    QElapsedTimer timer;
    timer.start();
    const auto slow = http::request("GET", QUrl(server.base().toString() + "/slow"), {}, {}, 200);
    QVERIFY2(slow.error.contains(QStringLiteral("超时")), qPrintable(slow.error));
    QCOMPARE(slow.status, 0);
    QVERIFY(timer.elapsed() < 5000);

    const auto unreachable = http::request("GET", deadUrl(), {}, {}, 2000);
    QCOMPARE(unreachable.status, 0);
    QVERIFY(!unreachable.ok());
    QVERIFY(!unreachable.error.isEmpty());
    QVERIFY(unreachable.body.isEmpty());

    const auto invalid = http::request("GET", QUrl(), {}, {}, 500);
    QCOMPARE(invalid.error, QStringLiteral("无效地址"));
}

// ---------------------------------------------------------------- JellyfinAdapter

void TestMediaServer::jellyfinAuthenticateAndProbe()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (record.verb == "POST" && pathOf(record) == "/Users/AuthenticateByName")
            return okJson(jellyfinLoginBody());
        if (record.verb == "GET" && pathOf(record) == "/System/Info")
            return okJson(QJsonObject{{QStringLiteral("Version"), QStringLiteral("10.9.0")}});
        return failure(404);
    });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    const auto auth = adapter.authenticate(server.base(), QStringLiteral("alice"),
                                           QStringLiteral("pw"));
    QVERIFY2(auth.ok, qPrintable(auth.error));
    QCOMPARE(auth.userId, QStringLiteral("user-1"));
    QCOMPARE(auth.token, QStringLiteral("tok-123"));
    QCOMPARE(adapter.userId(), QStringLiteral("user-1"));
    QCOMPARE(adapter.token(), QStringLiteral("tok-123"));
    QCOMPARE(adapter.baseUrl().host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(server.requests.size(), 2);

    const MockServer::Record login = server.requests.at(0);
    QCOMPARE(login.verb, QByteArray("POST"));
    QCOMPARE(pathOf(login), QStringLiteral("/Users/AuthenticateByName"));
    QCOMPARE(login.headers.value("content-type"), QByteArray("application/json"));
    QVERIFY(login.headers.value("x-emby-authorization")
                .contains("DeviceId=\"palune-desktop-1\""));
    const QJsonObject payload = parseJson(login.body);
    QCOMPARE(payload.value(QStringLiteral("Username")).toString(), QStringLiteral("alice"));
    QCOMPARE(payload.value(QStringLiteral("Pw")).toString(), QStringLiteral("pw"));

    const MockServer::Record info = server.requests.at(1);
    QCOMPARE(pathOf(info), QStringLiteral("/System/Info"));
    QCOMPARE(info.headers.value("x-emby-token"), QByteArray("tok-123"));
    QVERIFY(info.headers.value("x-emby-authorization").startsWith("MediaBrowser Client="));

    const auto caps = adapter.capabilities();
    QVERIFY(caps.auth && caps.catalog && caps.playback && caps.progress && caps.images);
    QCOMPARE(caps.serverVersion, QStringLiteral("10.9.0"));
    QCOMPARE(caps.audioCodecs.size(), 5);
    QCOMPARE(caps.subtitleFormats.size(), 3);
    QVERIFY(!caps.supportsHttps); // 回环 http
}

void TestMediaServer::jellyfinProbeFallsBackToPublic()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        const QString path = pathOf(record);
        if (path == "/Users/AuthenticateByName")
            return okJson(jellyfinLoginBody());
        if (path == "/System/Info")
            return failure(401); // 需要更高权限，退回公共信息
        if (path == "/System/Info/Public")
            return okJson(QJsonObject{{QStringLiteral("Version"), QStringLiteral("10.8.1")}});
        return failure(404);
    });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Emby);
    const auto auth = adapter.authenticate(server.base(), QStringLiteral("u"), QStringLiteral("p"));
    QVERIFY2(auth.ok, qPrintable(auth.error));
    QCOMPARE(adapter.capabilities().serverVersion, QStringLiteral("10.8.1"));
    QCOMPARE(server.requests.size(), 3);
    QCOMPARE(pathOf(server.requests.at(2)), QStringLiteral("/System/Info/Public"));
}

void TestMediaServer::jellyfinAuthenticateFailures()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/Users/AuthenticateByName")
            return failure(401);
        return failure(404);
    });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    const auto denied = adapter.authenticate(server.base(), QStringLiteral("u"),
                                             QStringLiteral("bad"));
    QVERIFY(!denied.ok);
    QVERIFY2(denied.error.startsWith(QStringLiteral("认证失败（HTTP 401）")),
             qPrintable(denied.error));
    QVERIFY(!denied.error.isEmpty());

    // 地址解析失败
    const auto broken = adapter.authenticate(QUrl(QStringLiteral("http://")), QStringLiteral("u"),
                                             QStringLiteral("p"));
    QVERIFY(!broken.ok);
    QVERIFY2(broken.error.contains(QStringLiteral("无法解析服务器地址")),
             qPrintable(broken.error));

    // 传输层连不上：不是认证失败
    const auto unreachable = adapter.authenticate(deadUrl(), QStringLiteral("u"),
                                                  QStringLiteral("p"));
    QVERIFY(!unreachable.ok);
    QVERIFY(!unreachable.error.isEmpty());
    QVERIFY(!unreachable.error.startsWith(QStringLiteral("认证失败")));

    // 200 但缺字段
    MockServer empty;
    QVERIFY(empty.start());
    empty.setHandler([](const MockServer::Record &) { return okJson(QJsonObject{}); });
    const auto incomplete = adapter.authenticate(empty.base(), QStringLiteral("u"),
                                                 QStringLiteral("p"));
    QVERIFY(!incomplete.ok);
    QCOMPARE(incomplete.error, QStringLiteral("认证响应缺少 AccessToken/UserId"));
}

void TestMediaServer::jellyfinApiKeyAuth()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        const QString path = pathOf(record);
        if (path == "/Users/Me")
            return okJson(QJsonObject{{QStringLiteral("Id"), QStringLiteral("user-9")}});
        if (path == "/System/Info")
            return okJson(QJsonObject{{QStringLiteral("Version"), QStringLiteral("10.9.2")}});
        return failure(404);
    });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::OtherJellyfin);
    const auto auth = adapter.authenticateWithApiKey(server.base(), QStringLiteral("  key-9  "));
    QVERIFY2(auth.ok, qPrintable(auth.error));
    QCOMPARE(auth.userId, QStringLiteral("user-9"));
    QCOMPARE(auth.token, QStringLiteral("key-9")); // 首尾空白被裁掉
    QCOMPARE(server.requests.at(0).headers.value("x-emby-token"), QByteArray("key-9"));
    QCOMPARE(adapter.capabilities().serverVersion, QStringLiteral("10.9.2"));

    const auto emptyKey = adapter.authenticateWithApiKey(server.base(), QStringLiteral("  "));
    QVERIFY(!emptyKey.ok);
    QCOMPARE(emptyKey.error, QStringLiteral("API Key 为空"));

    const auto brokenUrl = adapter.authenticateWithApiKey(QUrl(QStringLiteral("http://")),
                                                          QStringLiteral("k"));
    QVERIFY(!brokenUrl.ok);
    QVERIFY2(brokenUrl.error.contains(QStringLiteral("无法解析服务器地址")),
             qPrintable(brokenUrl.error));

    server.setHandler([](const MockServer::Record &) { return failure(403); });
    const auto forbidden = adapter.authenticateWithApiKey(server.base(), QStringLiteral("k"));
    QVERIFY(!forbidden.ok);
    QVERIFY2(forbidden.error.startsWith(QStringLiteral("认证失败（HTTP 403）")),
             qPrintable(forbidden.error));

    server.setHandler([](const MockServer::Record &) { return okJson(QJsonObject{}); });
    const auto noUser = adapter.authenticateWithApiKey(server.base(), QStringLiteral("k"));
    QVERIFY(!noUser.ok);
    QCOMPARE(noUser.error, QStringLiteral("API Key 响应缺少 UserId"));

    const auto unreachable = adapter.authenticateWithApiKey(deadUrl(), QStringLiteral("k"));
    QVERIFY(!unreachable.ok);
    QVERIFY(!unreachable.error.isEmpty());
}

void TestMediaServer::jellyfinSessionIdentityAndPlaybackUrl()
{
    const QList<QPair<MediaServerAdapter::Kind, QString>> kinds{
        {MediaServerAdapter::Kind::Jellyfin, QStringLiteral("jellyfin")},
        {MediaServerAdapter::Kind::Emby, QStringLiteral("emby")},
        {MediaServerAdapter::Kind::ZhiSpace, QStringLiteral("zhispace")},
        {MediaServerAdapter::Kind::FeiNiu, QStringLiteral("feiniu")},
        {MediaServerAdapter::Kind::OtherJellyfin, QStringLiteral("jellyfin-compat")},
        {MediaServerAdapter::Kind::Plex, QStringLiteral("plex")},
    };
    for (const auto &entry : kinds) {
        JellyfinAdapter adapter(entry.first);
        QCOMPARE(adapter.id(), entry.second);
        QCOMPARE(adapter.kind(), entry.first);
        QVERIFY(!adapter.displayName().isEmpty());
    }
    QCOMPARE(JellyfinAdapter(MediaServerAdapter::Kind::Emby).displayName(),
             QStringLiteral("Emby"));
    QCOMPARE(JellyfinAdapter(MediaServerAdapter::Kind::ZhiSpace).displayName(),
             QStringLiteral("极空间（Jellyfin 协议）"));
    QCOMPARE(JellyfinAdapter(MediaServerAdapter::Kind::FeiNiu).displayName(),
             QStringLiteral("飞牛（Jellyfin 协议）"));
    QCOMPARE(JellyfinAdapter(MediaServerAdapter::Kind::OtherJellyfin).displayName(),
             QStringLiteral("其他 Jellyfin（极空间/飞牛）"));
    QCOMPARE(JellyfinAdapter(MediaServerAdapter::Kind::Plex).displayName(),
             QStringLiteral("Jellyfin"));

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    adapter.restoreSession(QUrl(QStringLiteral("https://media.example")),
                           QStringLiteral("user-1"), QStringLiteral("tok"));
    const auto caps = adapter.capabilities();
    QVERIFY(caps.auth && caps.catalog && caps.playback && caps.progress && caps.images);
    QVERIFY(caps.supportsHttps);
    QVERIFY(caps.serverVersion.isEmpty());

    // 空 token 恢复：能力全关。
    JellyfinAdapter anonymous(MediaServerAdapter::Kind::Jellyfin);
    anonymous.restoreSession(QUrl(QStringLiteral("http://media.example")), QString(), QString());
    QVERIFY(!anonymous.capabilities().auth);
    QVERIFY(!anonymous.capabilities().supportsHttps);

    MediaItem withStream;
    withStream.id = QStringLiteral("vid-1");
    withStream.streamUrl = QUrl(QStringLiteral("http://cdn.example/vid-1.m3u8"));
    PlaybackMode mode = PlaybackMode::Transcode;
    QCOMPARE(adapter.playbackUrl(withStream, &mode).toString(), withStream.streamUrl.toString());
    QCOMPARE(mode, PlaybackMode::DirectPlay);
    QCOMPARE(adapter.playbackUrl(withStream, nullptr).toString(), withStream.streamUrl.toString());

    MediaItem bare;
    bare.id = QStringLiteral("vid-2");
    const QUrl built = adapter.playbackUrl(bare, nullptr);
    QCOMPARE(built.toString(),
             QStringLiteral("https://media.example/Videos/vid-2/stream?static=true&api_key=tok"));
}

void TestMediaServer::jellyfinFetchItemsPaging()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) != "/Users/user-1/Items")
            return failure(404);
        const int start = queryValue(record, QStringLiteral("StartIndex")).toInt();
        QJsonArray items;
        if (start == 0) {
            items = QJsonArray{jellyfinMovieItem(),
                               QJsonObject{{QStringLiteral("Id"), QStringLiteral("series-1")},
                                           {QStringLiteral("Type"), QStringLiteral("Series")}},
                               jellyfinEpisodeItem()};
        } else {
            items = QJsonArray{jellyfinMovieItem()};
        }
        return okJson(QJsonObject{{QStringLiteral("Items"), items},
                                  {QStringLiteral("TotalRecordCount"), 3}});
    });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    adapter.restoreSession(server.base(), QStringLiteral("user-1"), QStringLiteral("tok-123"));

    const auto first = adapter.fetchItems(QString(), 0, 2);
    QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
    QCOMPARE(first.items.size(), 2); // Series 被过滤掉
    QVERIFY(first.hasMore);

    const MockServer::Record request = server.requests.at(0);
    QCOMPARE(request.verb, QByteArray("GET"));
    QCOMPARE(request.headers.value("x-emby-token"), QByteArray("tok-123"));
    QCOMPARE(queryValue(request, QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(queryValue(request, QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie,Episode"));
    QVERIFY(queryValue(request, QStringLiteral("Fields"))
                .contains(QStringLiteral("MediaSources")));
    QCOMPARE(queryValue(request, QStringLiteral("ImageTypeLimit")), QStringLiteral("1"));
    QCOMPARE(queryValue(request, QStringLiteral("EnableImageTypes")), QStringLiteral("Primary"));
    QCOMPARE(queryValue(request, QStringLiteral("SortBy")), QStringLiteral("SortName"));
    QCOMPARE(queryValue(request, QStringLiteral("SortOrder")), QStringLiteral("Ascending"));
    QCOMPARE(queryValue(request, QStringLiteral("Limit")), QStringLiteral("2"));
    QCOMPARE(queryValue(request, QStringLiteral("StartIndex")), QStringLiteral("0"));
    QVERIFY(!QUrlQuery(queryOf(request)).hasQueryItem(QStringLiteral("ParentId")));

    const MediaItem movie = first.items.at(0);
    QCOMPARE(movie.id, QStringLiteral("movie-1"));
    QCOMPARE(movie.name, QStringLiteral("迷宫"));
    QCOMPARE(movie.type, QStringLiteral("movie"));
    QVERIFY(movie.libraryId.isEmpty());
    QCOMPARE(movie.container, QStringLiteral("mkv"));
    QCOMPARE(movie.videoCodec, QStringLiteral("h264"));
    QCOMPARE(movie.audioCodec, QStringLiteral("aac"));
    QCOMPARE(movie.durationSeconds, 7200.0);
    QCOMPARE(movie.season, 0);
    QVERIFY(movie.directPlayable);
    QVERIFY(movie.posterUrl.toString().contains(
        QStringLiteral("/Items/movie-1/Images/Primary")));
    QVERIFY(movie.posterUrl.toString().contains(QStringLiteral("maxHeight=480")));
    QVERIFY(movie.streamUrl.toString().contains(QStringLiteral("/Videos/movie-1/stream")));
    QVERIFY(movie.streamUrl.toString().contains(QStringLiteral("api_key=tok-123")));
    QVERIFY(movie.streamUrl.toString().contains(QStringLiteral("MediaSourceId=src-1")));

    const MediaItem episode = first.items.at(1);
    QCOMPARE(episode.id, QStringLiteral("ep-1"));
    QCOMPARE(episode.type, QStringLiteral("episode"));
    QCOMPARE(episode.seriesName, QStringLiteral("长剧"));
    QCOMPARE(episode.season, 2);
    QCOMPARE(episode.episode, 3);
    QCOMPARE(episode.durationSeconds, 1.5);
    QVERIFY(episode.container.isEmpty());
    QVERIFY(episode.videoCodec.isEmpty());
    QVERIFY(!episode.streamUrl.toString().contains(QStringLiteral("MediaSourceId=src")));

    const auto second = adapter.fetchItems(QString(), 2, 2);
    QCOMPARE(second.items.size(), 1);
    QVERIFY(!second.hasMore);

    // 指定库：带 ParentId；limit 0 抬到 1，负 offset 归零。
    server.requests.clear();
    const auto library = adapter.fetchItems(QStringLiteral("lib-1"), 0, 5);
    QCOMPARE(library.items.at(0).libraryId, QStringLiteral("lib-1"));
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("ParentId")),
             QStringLiteral("lib-1"));
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("Limit")), QStringLiteral("5"));

    server.requests.clear();
    adapter.fetchItems(QString(), -5, 0);
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("StartIndex")),
             QStringLiteral("0"));
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("Limit")), QStringLiteral("1"));
}

void TestMediaServer::jellyfinFetchItemsErrors()
{
    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    // 未登录
    const auto anonymous = adapter.fetchItems(QString(), 0, 10);
    QVERIFY(anonymous.items.isEmpty());
    QCOMPARE(anonymous.error, QStringLiteral("尚未登录"));

    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &) { return failure(500); });
    adapter.restoreSession(server.base(), QStringLiteral("user-1"), QStringLiteral("tok"));
    const auto failed = adapter.fetchItems(QString(), 0, 10);
    QVERIFY(failed.items.isEmpty());
    QVERIFY2(!failed.error.isEmpty(), "500 应产生错误信息");

    adapter.restoreSession(deadUrl(), QStringLiteral("user-1"), QStringLiteral("tok"));
    const auto unreachable = adapter.fetchItems(QString(), 0, 10);
    QVERIFY(unreachable.items.isEmpty());
    QVERIFY(!unreachable.error.isEmpty());
}

void TestMediaServer::jellyfinReportProgressLifecycle()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &) { return okJson(QJsonObject{}); });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    MediaItem item;
    item.id = QStringLiteral("movie-1");
    UserState state;
    state.itemId = item.id;
    state.positionSeconds = 60.0;

    // 未登录
    QCOMPARE(adapter.reportProgress(QStringLiteral("s1"), item, state).error,
             QStringLiteral("尚未登录"));

    adapter.restoreSession(server.base(), QStringLiteral("user-1"), QStringLiteral("tok-123"));

    // 首次进度：先补 start，再报 progress
    const auto first = adapter.reportProgress(QStringLiteral("sess-1"), item, state);
    QVERIFY2(first.ok, qPrintable(first.error));
    QCOMPARE(server.requests.size(), 2);
    QCOMPARE(pathOf(server.requests.at(0)), QStringLiteral("/Sessions/Playing"));
    QCOMPARE(pathOf(server.requests.at(1)), QStringLiteral("/Sessions/Playing/Progress"));

    const MockServer::Record startRequest = server.requests.at(0);
    QCOMPARE(startRequest.verb, QByteArray("POST"));
    QCOMPARE(startRequest.headers.value("content-type"), QByteArray("application/json"));
    QCOMPARE(startRequest.headers.value("x-emby-token"), QByteArray("tok-123"));
    QVERIFY(!startRequest.headers.value("x-emby-authorization").isEmpty());
    const QJsonObject startPayload = parseJson(startRequest.body);
    QCOMPARE(startPayload.value(QStringLiteral("EventName")).toString(),
             QStringLiteral("playbackstart"));
    QCOMPARE(startPayload.value(QStringLiteral("PlaySessionId")).toString(),
             QStringLiteral("sess-1"));
    QCOMPARE(startPayload.value(QStringLiteral("PositionTicks")).toDouble(), 600000000.0);

    const QJsonObject progressPayload = parseJson(server.requests.at(1).body);
    QCOMPARE(progressPayload.value(QStringLiteral("EventName")).toString(),
             QStringLiteral("timeupdate"));
    QCOMPARE(progressPayload.value(QStringLiteral("ItemId")).toString(),
             QStringLiteral("movie-1"));
    QCOMPARE(progressPayload.value(QStringLiteral("PlayMethod")).toString(),
             QStringLiteral("DirectPlay"));
    QVERIFY(progressPayload.value(QStringLiteral("CanSeek")).toBool());
    QVERIFY(!progressPayload.value(QStringLiteral("IsPaused")).toBool());
    QVERIFY(!progressPayload.value(QStringLiteral("IsMuted")).toBool());

    // 同一会话的后续进度：不再补 start
    server.requests.clear();
    QVERIFY(adapter.reportProgress(QStringLiteral("sess-1"), item, state).ok);
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(pathOf(server.requests.at(0)), QStringLiteral("/Sessions/Playing/Progress"));

    // 停止：走 /Sessions/Playing/Stopped，并从会话集合移除
    server.requests.clear();
    state.played = true;
    QVERIFY(adapter.reportProgress(QStringLiteral("sess-1"), item, state).ok);
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(pathOf(server.requests.at(0)), QStringLiteral("/Sessions/Playing/Stopped"));
    QCOMPARE(parseJson(server.requests.at(0).body).value(QStringLiteral("EventName")).toString(),
             QStringLiteral("stop"));

    // 停止后再次上报：start 需要重新发送
    server.requests.clear();
    state.played = false;
    QVERIFY(adapter.reportProgress(QStringLiteral("sess-1"), item, state).ok);
    QCOMPARE(server.requests.size(), 2);

    // 负数位置按 0 处理
    server.requests.clear();
    UserState negative;
    negative.positionSeconds = -12.5;
    QVERIFY(adapter.reportProgress(QStringLiteral("sess-2"), item, negative).ok);
    QCOMPARE(server.requests.size(), 2);
    QCOMPARE(parseJson(server.requests.at(1).body)
                 .value(QStringLiteral("PositionTicks")).toDouble(),
             0.0);
}

void TestMediaServer::jellyfinReportProgressErrors()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/Sessions/Playing")
            return failure(500);
        return okJson(QJsonObject{});
    });

    JellyfinAdapter adapter(MediaServerAdapter::Kind::Jellyfin);
    adapter.restoreSession(server.base(), QStringLiteral("user-1"), QStringLiteral("tok"));
    MediaItem item;
    item.id = QStringLiteral("movie-1");
    UserState state;
    state.positionSeconds = 10.0;

    const auto startFailed = adapter.reportProgress(QStringLiteral("s1"), item, state);
    QVERIFY(!startFailed.ok);
    QVERIFY(!startFailed.error.isEmpty());
    QCOMPARE(server.requests.size(), 1); // 没有继续发 progress

    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/Sessions/Playing/Progress")
            return failure(500);
        return okJson(QJsonObject{});
    });
    const auto progressFailed = adapter.reportProgress(QStringLiteral("s2"), item, state);
    QVERIFY(!progressFailed.ok);
    QVERIFY(!progressFailed.error.isEmpty());

    // 传输层失败：错误来自网络层
    adapter.restoreSession(deadUrl(), QStringLiteral("user-1"), QStringLiteral("tok"));
    const auto unreachable = adapter.reportProgress(QStringLiteral("s3"), item, state);
    QVERIFY(!unreachable.ok);
    QVERIFY(!unreachable.error.isEmpty());
}

// ---------------------------------------------------------------- PlexAdapter

// 注：PlexAdapter 的 PIN / 资源发现固定访问 plex.tv，测试通过 PALUNE_PLEX_TV_BASE
// 注入本机回环 mock（PlexAdapter.cpp 中的唯一注入点，未设置时行为不变）。
void TestMediaServer::plexCreatePin()
{
    MockServer server;
    QVERIFY(server.start());
    PlexTvOverride env(server.base());
    server.setHandler([](const MockServer::Record &record) {
        if (record.verb == "POST" && pathOf(record) == "/api/v2/pins")
            return okJson(QJsonObject{{QStringLiteral("id"), 77},
                                      {QStringLiteral("code"), QStringLiteral("c0de")}});
        return failure(404);
    });

    PlexAdapter adapter;
    const auto pin = adapter.createPin();
    QVERIFY2(pin.ok, qPrintable(pin.error));
    QCOMPARE(pin.id, qint64{77});
    QCOMPARE(pin.code, QStringLiteral("c0de"));
    QVERIFY(pin.authUrl.startsWith(QStringLiteral("https://app.plex.tv/auth#?")));
    QVERIFY(pin.authUrl.contains(QStringLiteral("clientID=palune-desktop-1")));
    QVERIFY(pin.authUrl.contains(QStringLiteral("code=c0de")));

    const MockServer::Record record = server.requests.at(0);
    QCOMPARE(record.verb, QByteArray("POST"));
    QCOMPARE(queryValue(record, QStringLiteral("strong")), QStringLiteral("true"));
    QCOMPARE(record.headers.value("x-plex-product"), QByteArray("Palune"));
    QCOMPARE(record.headers.value("x-plex-version"), QByteArray("1.0.0"));
    QCOMPARE(record.headers.value("x-plex-client-identifier"), QByteArray("palune-desktop-1"));
    QCOMPARE(record.headers.value("x-plex-platform"), QByteArray("Windows"));
    QCOMPARE(record.headers.value("accept"), QByteArray("application/json"));

    server.setHandler([](const MockServer::Record &) { return failure(500); });
    const auto denied = adapter.createPin();
    QVERIFY(!denied.ok);
    QVERIFY(!denied.error.isEmpty());
    QVERIFY(denied.authUrl.isEmpty());

    server.setHandler([](const MockServer::Record &) { return okJson(QJsonObject{}); });
    const auto incomplete = adapter.createPin();
    QVERIFY(!incomplete.ok);
    QCOMPARE(incomplete.error, QStringLiteral("PIN 响应缺少 id/code"));

    PlexTvOverride dead(deadUrl());
    const auto unreachable = adapter.createPin();
    QVERIFY(!unreachable.ok);
    QVERIFY(!unreachable.error.isEmpty());
    QVERIFY(!unreachable.error.startsWith(QStringLiteral("创建 PIN 失败")));
}

void TestMediaServer::plexPollPin()
{
    MockServer server;
    QVERIFY(server.start());
    PlexTvOverride env(server.base());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/api/v2/pins/77")
            return okJson(QJsonObject{{QStringLiteral("authToken"),
                                       QStringLiteral("plex-token")}});
        return failure(404);
    });

    PlexAdapter adapter;
    QString error;
    const QString token = adapter.pollPin(77, QStringLiteral("c0de"), &error);
    QCOMPARE(token, QStringLiteral("plex-token"));
    QVERIFY(error.isEmpty());
    QCOMPARE(pathOf(server.requests.at(0)), QStringLiteral("/api/v2/pins/77"));
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("X-Plex-Token")),
             QStringLiteral("c0de"));
    QCOMPARE(server.requests.at(0).headers.value("x-plex-product"), QByteArray("Palune"));

    // 还没在浏览器授权
    server.setHandler([](const MockServer::Record &) { return okJson(QJsonObject{}); });
    error.clear();
    QVERIFY(adapter.pollPin(77, QStringLiteral("c0de"), &error).isEmpty());
    QCOMPARE(error, QStringLiteral("尚未在浏览器完成授权"));

    // error 为空指针时不崩溃
    QVERIFY(adapter.pollPin(77, QStringLiteral("c0de"), nullptr).isEmpty());

    server.setHandler([](const MockServer::Record &) { return failure(500); });
    error.clear();
    QVERIFY(adapter.pollPin(77, QStringLiteral("c0de"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    PlexTvOverride dead(deadUrl());
    error.clear();
    QVERIFY(adapter.pollPin(77, QStringLiteral("c0de"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestMediaServer::plexListServers()
{
    MockServer server;
    QVERIFY(server.start());
    PlexTvOverride env(server.base());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) != "/api/v2/resources")
            return failure(404);
        return okJson(QJsonArray{
            QJsonObject{
                {QStringLiteral("provides"), QStringLiteral("server")},
                {QStringLiteral("name"), QStringLiteral("home")},
                {QStringLiteral("clientIdentifier"), QStringLiteral("cid-1")},
                {QStringLiteral("connections"),
                 QJsonArray{
                     QJsonObject{{QStringLiteral("uri"), QStringLiteral("http://192.168.1.5:32400")},
                                 {QStringLiteral("local"), true}},
                     QJsonObject{{QStringLiteral("uri"), QStringLiteral("https://relay:443")},
                                 {QStringLiteral("local"), false}}}}},
            QJsonObject{{QStringLiteral("provides"), QStringLiteral("client")},
                        {QStringLiteral("name"), QStringLiteral("player")}},
            QJsonObject{
                {QStringLiteral("provides"), QStringLiteral("server")},
                {QStringLiteral("name"), QStringLiteral("remote")},
                {QStringLiteral("clientIdentifier"), QStringLiteral("cid-2")},
                {QStringLiteral("accessToken"), QStringLiteral("own-token")},
                {QStringLiteral("connections"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("uri"), QStringLiteral("https://remote:32400")},
                     {QStringLiteral("local"), false}}}}},
            QJsonObject{{QStringLiteral("provides"), QStringLiteral("server")},
                        {QStringLiteral("name"), QStringLiteral("no-connection")}},
            QJsonObject{
                {QStringLiteral("provides"), QStringLiteral("server")},
                {QStringLiteral("connections"),
                 QJsonArray{QJsonObject{{QStringLiteral("uri"), QString()},
                                        {QStringLiteral("local"), true}}}}},
        });
    });

    PlexAdapter adapter;
    QString error;
    const auto resources = adapter.listServers(QStringLiteral("account-token"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(resources.size(), 2);

    // 直连优先，中继在后；没有独立 accessToken 时复用账号 token
    QCOMPARE(resources.at(0).name, QStringLiteral("home"));
    QCOMPARE(resources.at(0).clientIdentifier, QStringLiteral("cid-1"));
    QCOMPARE(resources.at(0).accessToken, QStringLiteral("account-token"));
    QCOMPARE(resources.at(0).uris.size(), 2);
    QCOMPARE(resources.at(0).uris.at(0), QStringLiteral("http://192.168.1.5:32400"));
    QCOMPARE(resources.at(0).uris.at(1), QStringLiteral("https://relay:443"));
    QVERIFY(!resources.at(0).remoteOnly);

    QCOMPARE(resources.at(1).name, QStringLiteral("remote"));
    QCOMPARE(resources.at(1).accessToken, QStringLiteral("own-token"));
    QVERIFY(resources.at(1).remoteOnly);

    const MockServer::Record request = server.requests.at(0);
    QCOMPARE(queryValue(request, QStringLiteral("includeHttps")), QStringLiteral("1"));
    QCOMPARE(queryValue(request, QStringLiteral("includeRelay")), QStringLiteral("1"));
    QCOMPARE(request.headers.value("x-plex-token"), QByteArray("account-token"));

    server.setHandler([](const MockServer::Record &) { return failure(500); });
    error.clear();
    QVERIFY(adapter.listServers(QStringLiteral("t"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    // 不传 error 也要安全
    QVERIFY(adapter.listServers(QStringLiteral("t"), nullptr).isEmpty());

    PlexTvOverride dead(deadUrl());
    error.clear();
    QVERIFY(adapter.listServers(QStringLiteral("t"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestMediaServer::plexAuthenticate()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/identity")
            return okXml(plexIdentityXml(QStringLiteral("1.40.2")));
        return failure(404);
    });

    PlexAdapter adapter;
    const auto auth = adapter.authenticate(server.base(), QStringLiteral("ignored"),
                                           QStringLiteral("tok-plex"));
    QVERIFY2(auth.ok, qPrintable(auth.error));
    QCOMPARE(auth.token, QStringLiteral("tok-plex"));
    QCOMPARE(auth.userId, QStringLiteral("127.0.0.1"));
    QCOMPARE(server.requests.at(0).headers.value("x-plex-token"), QByteArray("tok-plex"));

    const auto caps = adapter.capabilities();
    QVERIFY(caps.auth && caps.catalog && caps.playback && caps.progress && caps.images);
    QCOMPARE(caps.serverVersion, QStringLiteral("1.40.2"));
    QVERIFY(!caps.supportsHttps);

    const auto noToken = adapter.authenticate(server.base(), QString(), QStringLiteral("  "));
    QVERIFY(!noToken.ok);
    QVERIFY2(noToken.error.contains(QStringLiteral("X-Plex-Token")), qPrintable(noToken.error));

    const auto broken = adapter.authenticate(QUrl(QStringLiteral("http://")), QString(),
                                             QStringLiteral("tok"));
    QVERIFY(!broken.ok);
    QVERIFY2(broken.error.contains(QStringLiteral("无法解析服务器地址")),
             qPrintable(broken.error));

    server.setHandler([](const MockServer::Record &) { return failure(401); });
    const auto denied = adapter.authenticate(server.base(), QString(), QStringLiteral("bad"));
    QVERIFY(!denied.ok);
    QVERIFY(!denied.error.isEmpty());

    const auto unreachable = adapter.authenticate(deadUrl(), QString(),
                                                  QStringLiteral("tok"));
    QVERIFY(!unreachable.ok);
    QVERIFY(!unreachable.error.isEmpty());
    QVERIFY(!unreachable.error.startsWith(QStringLiteral("连接 Plex 服务器失败")));
}

void TestMediaServer::plexFetchItems()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        const QString path = pathOf(record);
        if (path == "/library/sections")
            return okXml(plexSectionsXml());
        if (path == "/library/sections/1/all")
            return okXml(plexMoviesXml());
        if (path == "/library/sections/9/all") {
            return okXml(QByteArray("<?xml version=\"1.0\"?>"
                                    "<MediaContainer size=\"1\">"
                                    "<Video ratingKey=\"900\" type=\"movie\" title=\"other\"/>"
                                    "</MediaContainer>"));
        }
        return failure(404);
    });

    PlexAdapter adapter;
    adapter.restoreSession(server.base(), QStringLiteral("tok"));
    const auto page = adapter.fetchItems(QString(), 0, 10);
    QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
    QCOMPARE(page.items.size(), 2);
    QVERIFY(!page.hasMore);
    QCOMPARE(server.requests.size(), 2);
    QCOMPARE(server.requests.at(0).headers.value("x-plex-token"), QByteArray("tok"));

    const MediaItem movie = page.items.at(0);
    QCOMPARE(movie.id, QStringLiteral("101"));
    QCOMPARE(movie.libraryId, QStringLiteral("1")); // 自动选中第一个 movie 分区
    QCOMPARE(movie.name, QStringLiteral("alien"));
    QCOMPARE(movie.type, QStringLiteral("movie"));
    QCOMPARE(movie.season, 0);
    QCOMPARE(movie.episode, 0);
    QCOMPARE(movie.durationSeconds, 5400.0);
    QCOMPARE(movie.container, QStringLiteral("mkv"));
    QCOMPARE(movie.videoCodec, QStringLiteral("hevc"));
    QCOMPARE(movie.audioCodec, QStringLiteral("eac3"));
    QVERIFY(movie.directPlayable);
    QCOMPARE(movie.posterUrl.toString(),
             server.base().toString()
                 + QStringLiteral("/library/metadata/101/thumb/1?X-Plex-Token=tok"));
    QCOMPARE(movie.streamUrl.toString(),
             server.base().toString()
                 + QStringLiteral("/library/parts/101/file.mkv?download=1&X-Plex-Token=tok"));

    const MediaItem episode = page.items.at(1);
    QCOMPARE(episode.id, QStringLiteral("102"));
    QCOMPARE(episode.type, QStringLiteral("episode"));
    QCOMPARE(episode.seriesName, QStringLiteral("series"));
    QCOMPARE(episode.season, 1);
    QCOMPARE(episode.episode, 2);
    QCOMPARE(episode.durationSeconds, 1200.0);
    QCOMPARE(episode.container, QStringLiteral("mp4"));
    QCOMPARE(episode.videoCodec, QStringLiteral("h264"));
    QCOMPARE(episode.audioCodec, QStringLiteral("aac"));

    // 分页：offset/limit 只切已解析的全集
    const auto second = adapter.fetchItems(QString(), 1, 1);
    QCOMPARE(second.items.size(), 1);
    QCOMPARE(second.items.at(0).id, QStringLiteral("102"));
    QVERIFY(!second.hasMore);

    const auto head = adapter.fetchItems(QString(), 0, 1);
    QCOMPARE(head.items.size(), 1);
    QVERIFY(head.hasMore);
    QCOMPARE(head.items.at(0).id, QStringLiteral("101"));

    // 分区已缓存：libraryId 为空时不再重复请求 /library/sections
    int sectionsRequests = 0;
    for (const auto &record : server.requests) {
        if (pathOf(record) == "/library/sections")
            ++sectionsRequests;
    }
    QCOMPARE(sectionsRequests, 1);

    // 指定分区：重新取分区并沿用传入的 key
    const auto other = adapter.fetchItems(QStringLiteral("9"), 0, 10);
    QCOMPARE(other.items.size(), 1);
    QCOMPARE(other.items.at(0).id, QStringLiteral("900"));
    QCOMPARE(pathOf(server.requests.last()), QStringLiteral("/library/sections/9/all"));
}

void TestMediaServer::plexFetchItemsErrors()
{
    PlexAdapter adapter;
    const auto anonymous = adapter.fetchItems(QString(), 0, 10);
    QCOMPARE(anonymous.error, QStringLiteral("尚未登录"));
    QVERIFY(anonymous.items.isEmpty());

    MockServer server;
    QVERIFY(server.start());
    adapter.restoreSession(server.base(), QStringLiteral("tok"));

    server.setHandler([](const MockServer::Record &) { return failure(500); });
    const auto sectionsFailed = adapter.fetchItems(QString(), 0, 10);
    QVERIFY(!sectionsFailed.error.isEmpty());
    QVERIFY(sectionsFailed.items.isEmpty());

    server.setHandler([](const MockServer::Record &) {
        return okXml(QByteArray("<?xml version=\"1.0\"?><MediaContainer size=\"1\">"
                                "<Directory key=\"5\" type=\"artist\" title=\"music\"/>"
                                "</MediaContainer>"));
    });
    const auto noSection = adapter.fetchItems(QString(), 0, 10);
    QCOMPARE(noSection.error, QStringLiteral("该 Plex 服务器没有电影/剧集分区"));

    server.setHandler([](const MockServer::Record &record) {
        if (pathOf(record) == "/library/sections")
            return okXml(plexSectionsXml());
        return failure(500);
    });
    const auto allFailed = adapter.fetchItems(QString(), 0, 10);
    QVERIFY(!allFailed.error.isEmpty());
    QVERIFY(allFailed.items.isEmpty());

    adapter.restoreSession(deadUrl(), QStringLiteral("tok"));
    const auto unreachable = adapter.fetchItems(QString(), 0, 10);
    QVERIFY(!unreachable.error.isEmpty());
    QVERIFY(unreachable.items.isEmpty());
}

void TestMediaServer::plexPlaybackUrlAndProgress()
{
    PlexAdapter adapter;
    MediaItem item;
    item.id = QStringLiteral("101");
    item.durationSeconds = 5400.0;

    PlaybackMode mode = PlaybackMode::Transcode;
    QVERIFY(adapter.playbackUrl(item, &mode).isEmpty());
    QCOMPARE(mode, PlaybackMode::DirectPlay);

    item.streamUrl = QUrl(QStringLiteral("http://plex.example/parts/101.mkv"));
    QCOMPARE(adapter.playbackUrl(item, &mode).toString(), item.streamUrl.toString());

    UserState state;
    state.itemId = item.id;
    state.positionSeconds = 60.0;
    QCOMPARE(adapter.reportProgress(QStringLiteral("s1"), item, state).error,
             QStringLiteral("尚未登录"));

    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &) { return okJson(QJsonObject{}); });
    adapter.restoreSession(server.base(), QStringLiteral("tok"));

    const auto progress = adapter.reportProgress(QStringLiteral("s1"), item, state);
    QVERIFY2(progress.ok, qPrintable(progress.error));
    QCOMPARE(server.requests.size(), 1);
    const MockServer::Record timeline = server.requests.at(0);
    QCOMPARE(timeline.verb, QByteArray("GET"));
    QCOMPARE(pathOf(timeline), QStringLiteral("/:/timeline"));
    QCOMPARE(queryValue(timeline, QStringLiteral("ratingKey")), QStringLiteral("101"));
    QCOMPARE(queryValue(timeline, QStringLiteral("key")), QStringLiteral("/library/metadata/101"));
    QCOMPARE(queryValue(timeline, QStringLiteral("state")), QStringLiteral("playing"));
    QCOMPARE(queryValue(timeline, QStringLiteral("time")), QStringLiteral("60000"));
    QCOMPARE(queryValue(timeline, QStringLiteral("duration")), QStringLiteral("5400000"));
    QCOMPARE(queryValue(timeline, QStringLiteral("X-Plex-Token")), QStringLiteral("tok"));

    // 负数位置按 0 处理
    server.requests.clear();
    state.positionSeconds = -5.0;
    QVERIFY(adapter.reportProgress(QStringLiteral("s1"), item, state).ok);
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("time")), QStringLiteral("0"));

    // 已看完：走 scrobble
    server.requests.clear();
    state.played = true;
    QVERIFY(adapter.reportProgress(QStringLiteral("s1"), item, state).ok);
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(pathOf(server.requests.at(0)), QStringLiteral("/:/scrobble"));
    QCOMPARE(queryValue(server.requests.at(0), QStringLiteral("key")), QStringLiteral("101"));
    QCOMPARE(queryValue(server.requests.at(0),
                        QStringLiteral("identifier")),
             QStringLiteral("com.plexapp.plugins.library"));

    server.setHandler([](const MockServer::Record &) { return failure(500); });
    const auto scrobbleFailed = adapter.reportProgress(QStringLiteral("s1"), item, state);
    QVERIFY(!scrobbleFailed.ok);
    QVERIFY(!scrobbleFailed.error.isEmpty());

    state.played = false;
    const auto timelineFailed = adapter.reportProgress(QStringLiteral("s1"), item, state);
    QVERIFY(!timelineFailed.ok);
    QVERIFY(!timelineFailed.error.isEmpty());

    adapter.restoreSession(deadUrl(), QStringLiteral("tok"));
    const auto unreachable = adapter.reportProgress(QStringLiteral("s1"), item, state);
    QVERIFY(!unreachable.ok);
    QVERIFY(!unreachable.error.isEmpty());
}

// ---------------------------------------------------------------- MediaLibraryService

void TestMediaServer::mediaServiceViewModeAndRoots()
{
    QTemporaryDir config;
    QTemporaryDir mediaA;
    QTemporaryDir mediaB;
    QVERIFY(config.isValid() && mediaA.isValid() && mediaB.isValid());
    ConfigDirOverride guard(config.path());

    QFile clip(mediaA.path() + "/a.mp4");
    QVERIFY(clip.open(QIODevice::WriteOnly));
    clip.write("x");
    clip.close();

    Settings settings(config.path() + "/settings.toml");
    settings.setValue(QStringLiteral("library.roots"),
                      QStringList{mediaA.path(), QStringLiteral("   "),
                                  mediaA.path() + "/missing", mediaB.path()});
    settings.setValue(QStringLiteral("library.viewMode"), QStringLiteral("list"));
    QVERIFY(settings.save());

    MediaLibraryService service;
    service.setSettings(&settings);
    QSignalSpy viewSpy(&service, &MediaLibraryService::viewModeChanged);
    QSignalSpy rootsSpy(&service, &MediaLibraryService::rootsChanged);
    service.load();

    QCOMPARE(service.viewMode(), QStringLiteral("list"));
    QCOMPARE(service.roots().size(), 2); // 空白与不存在的目录被剔除
    QVERIFY(viewSpy.count() >= 1);
    QVERIFY(rootsSpy.count() >= 1);
    QVERIFY(service.available());
    QTRY_VERIFY_WITH_TIMEOUT(!service.scanning(), 60000);

    // 非法视图模式忽略；重复设置同一模式不重复发信号
    service.setViewMode(QStringLiteral("grid"));
    QCOMPARE(service.viewMode(), QStringLiteral("list"));
    viewSpy.clear();
    service.setViewMode(QStringLiteral("list"));
    QCOMPARE(viewSpy.count(), 0);
    service.setViewMode(QStringLiteral("poster"));
    QCOMPARE(service.viewMode(), QStringLiteral("poster"));
    QCOMPARE(viewSpy.count(), 1);
    QCOMPARE(settings.value(QStringLiteral("library.viewMode")).toString(),
             QStringLiteral("poster"));

    // 删除根目录：不存在的路径为 no-op，存在的路径从配置里移除并落盘
    service.removeRoot(mediaA.path() + "/missing");
    QCOMPARE(service.roots().size(), 2);
    service.removeRoot(QDir::fromNativeSeparators(mediaA.path()));
    QCOMPARE(service.roots().size(), 1);
    QCOMPARE(settings.value(QStringLiteral("library.roots")).toStringList().size(), 1);
    service.removeRoot(QDir::fromNativeSeparators(mediaB.path()));
    QCOMPARE(service.roots().size(), 0);
    QCOMPARE(settings.value(QStringLiteral("library.roots")).toStringList().size(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!service.scanning(), 60000);
    QVERIFY(!service.available());

    // 未连接服务器且没有本地根目录：refresh 立即通知界面，进度回写直接失败
    QSignalSpy librarySpy(&service, &MediaLibraryService::libraryChanged);
    service.refresh();
    QCOMPARE(librarySpy.count(), 1);
    QVERIFY(!service.reportRemoteProgress(QStringLiteral("http://example/x"), 10.0));
    QVERIFY(!service.serverConnected());
    QVERIFY(!service.serverFailed());
    QVERIFY(service.serverStatus().isEmpty());
}

void TestMediaServer::mediaServiceIndexAndThumbCache()
{
    QTemporaryDir config;
    QTemporaryDir media;
    QVERIFY(config.isValid() && media.isValid());
    ConfigDirOverride guard(config.path());

    const QString clip = QDir::fromNativeSeparators(media.path() + "/clip.mp4");
    QFile clipFile(clip);
    QVERIFY(clipFile.open(QIODevice::WriteOnly));
    clipFile.write("x");
    clipFile.close();

    const QString thumbs = config.path() + "/library/thumbs";
    QVERIFY(QDir().mkpath(thumbs));
    QFile poster(thumbs + "/poster.jpg");
    QVERIFY(poster.open(QIODevice::WriteOnly));
    poster.write("jpeg");
    poster.close();

    // 索引里：1 条有效（海报存在）、1 条源文件缺失、1 条缺 path、1 条海报缺失。
    const QJsonArray entries{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("1")},
                    {QStringLiteral("title"), QStringLiteral("clip")},
                    {QStringLiteral("path"), clip},
                    {QStringLiteral("poster"), QDir::fromNativeSeparators(thumbs + "/poster.jpg")}},
        QJsonObject{{QStringLiteral("path"), QDir::fromNativeSeparators(media.path() + "/gone.mp4")}},
        QJsonObject{{QStringLiteral("title"), QStringLiteral("no-path")}},
        QJsonObject{{QStringLiteral("path"), clip},
                    {QStringLiteral("poster"), QDir::fromNativeSeparators(config.path() + "/nope.jpg")}},
    };
    QVERIFY(QDir().mkpath(config.path() + "/library"));
    QFile index(config.path() + "/library/index.json");
    QVERIFY(index.open(QIODevice::WriteOnly));
    index.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                          {QStringLiteral("items"), entries}})
                    .toJson());
    index.close();

    Settings settings(config.path() + "/settings.toml");
    QVERIFY(settings.save()); // 没有 roots，load() 不触发扫描

    MediaLibraryService service;
    service.setSettings(&settings);
    service.load();
    QCOMPARE(service.itemCount(), 2);
    QVERIFY(!service.scanning());
    const QVariantMap first = service.items().at(0).toMap();
    QVERIFY(first.value(QStringLiteral("poster")).toString().startsWith(QStringLiteral("file:")));
    const QVariantMap second = service.items().at(1).toMap();
    QVERIFY(second.value(QStringLiteral("poster")).toString().isEmpty());

    // 清空缩略图缓存：目录清空 + 本地条目海报置空
    service.clearThumbCache();
    QVERIFY(QDir(thumbs).entryList(QDir::Files).isEmpty());
    for (const QVariant &entry : service.items())
        QVERIFY(entry.toMap().value(QStringLiteral("poster")).toString().isEmpty());
    QVERIFY(service.scanStatus().contains(QStringLiteral("已清理缩略图缓存")));

    // 重建索引：索引文件删除、本地条目清空
    service.rebuildLibraryIndex();
    QCOMPARE(service.itemCount(), 0);
    QVERIFY(!QFile::exists(service.indexFile()));
    QVERIFY(service.scanStatus().contains(QStringLiteral("索引已删除")));

    // 非法索引（数组而非对象）按空库处理，不崩溃
    QFile broken(config.path() + "/library/index.json");
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("[1,2,3]");
    broken.close();
    MediaLibraryService reloaded;
    reloaded.setSettings(&settings);
    reloaded.load();
    QCOMPARE(reloaded.itemCount(), 0);
}

void TestMediaServer::mediaServiceTrimThumbCache()
{
    QTemporaryDir config;
    QVERIFY(config.isValid());
    ConfigDirOverride guard(config.path());

    const QString thumbs = config.path() + "/library/thumbs";

    // 无 settings：默认 2000 MB，目录不存在时为 no-op
    MediaLibraryService bare;
    bare.trimThumbCache();
    QVERIFY(bare.scanStatus().contains(QStringLiteral("2000 MB")));

    QVERIFY(QDir().mkpath(thumbs));
    auto makeFile = [](const QString &path, qint64 bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write("x");
        const bool resized = file.resize(bytes);
        file.close();
        return resized;
    };
    QVERIFY(makeFile(thumbs + "/old.jpg", 1024));
    QVERIFY(makeFile(thumbs + "/mid.jpg", 1024));
    QVERIFY(makeFile(thumbs + "/big.jpg", 120LL * 1024 * 1024));

    Settings settings(config.path() + "/settings.toml");
    settings.setValue(QStringLiteral("storage.thumbCacheLimitMb"), 5000);
    QVERIFY(settings.save());
    MediaLibraryService service;
    service.setSettings(&settings);
    service.trimThumbCache();
    QVERIFY2(service.scanStatus().contains(QStringLiteral("未超过上限 5000 MB")),
             qPrintable(service.scanStatus()));
    QCOMPARE(QDir(thumbs).entryList(QDir::Files).size(), 3);

    // 低于下限的配置被夹到 100 MB：120 MB 超限，最旧优先全部淘汰
    settings.setValue(QStringLiteral("storage.thumbCacheLimitMb"), 5);
    QVERIFY(settings.save());
    service.trimThumbCache();
    QVERIFY2(service.scanStatus().contains(QStringLiteral("100 MB")),
             qPrintable(service.scanStatus()));
    QVERIFY(service.scanStatus().contains(QStringLiteral("清理")));
    QCOMPARE(QDir(thumbs).entryList(QDir::Files).size(), 0);
}

void TestMediaServer::mediaServiceTestServerConnection()
{
    MockServer server;
    QVERIFY(server.start());
    server.setHandler([](const MockServer::Record &record) {
        const QString path = pathOf(record);
        if (path == "/Users/AuthenticateByName") {
            const QJsonObject payload = parseJson(record.body);
            if (payload.value(QStringLiteral("Pw")).toString() == QLatin1String("bad"))
                return failure(401);
            return okJson(jellyfinLoginBody());
        }
        if (path == "/Users/Me")
            return okJson(QJsonObject{{QStringLiteral("Id"), QStringLiteral("user-9")}});
        if (path == "/System/Info")
            return okJson(QJsonObject{{QStringLiteral("Version"), QStringLiteral("10.9.0")}});
        return failure(404);
    });

    MediaLibraryService service;
    QSignalSpy statusSpy(&service, &MediaLibraryService::serverStatusChanged);

    // 密码登录：只验证不落盘、不建会话
    QVERIFY(service.testServerConnection(QStringLiteral("jellyfin"), server.base().toString(),
                                         QStringLiteral("alice"), QStringLiteral("pw"),
                                         QStringLiteral("password")));
    QVERIFY2(service.serverStatus().contains(QStringLiteral("连接成功：Jellyfin 10.9.0")),
             qPrintable(service.serverStatus()));
    QVERIFY(!service.serverFailed());
    QVERIFY(!service.serverConnected());
    QCOMPARE(statusSpy.count(), 1);

    // API Key 登录
    QVERIFY(service.testServerConnection(QStringLiteral("emby"), server.base().toString(),
                                         QString(), QStringLiteral("key-1"),
                                         QStringLiteral("apikey")));
    QVERIFY2(service.serverStatus().contains(QStringLiteral("连接成功：Emby")),
             qPrintable(service.serverStatus()));

    // 不支持的类型
    QVERIFY(!service.testServerConnection(QStringLiteral("plex"), server.base().toString(),
                                          QStringLiteral("a"), QStringLiteral("b"),
                                          QStringLiteral("password")));
    QVERIFY(service.serverStatus().contains(QStringLiteral("不支持的服务器类型")));
    QVERIFY(service.serverFailed());

    // 认证被拒：状态明确写「认证失败」
    QVERIFY(!service.testServerConnection(QStringLiteral("jellyfin"), server.base().toString(),
                                          QStringLiteral("alice"), QStringLiteral("bad"),
                                          QStringLiteral("password")));
    QVERIFY2(service.serverStatus().startsWith(QStringLiteral("认证失败")),
             qPrintable(service.serverStatus()));
    QVERIFY(service.serverFailed());

    // API Key 为空：failureText 走「含 API Key → 认证失败」分支
    QVERIFY(!service.testServerConnection(QStringLiteral("jellyfin"), server.base().toString(),
                                          QString(), QString(), QStringLiteral("apikey")));
    QVERIFY2(service.serverStatus().startsWith(QStringLiteral("认证失败：")),
             qPrintable(service.serverStatus()));

    // 不可达：与认证失败区分开
    QVERIFY(!service.testServerConnection(QStringLiteral("jellyfin"), deadUrl().toString(),
                                          QStringLiteral("a"), QStringLiteral("b"),
                                          QStringLiteral("password")));
    QVERIFY2(service.serverStatus().startsWith(QStringLiteral("无法连接（不可达）")),
             qPrintable(service.serverStatus()));
    QVERIFY(service.serverFailed());

    // 保存入口对不支持的类型直接拒绝（不会写入系统凭据）
    QVERIFY(!service.saveServerConnection(QStringLiteral("plex"), server.base().toString(),
                                          QStringLiteral("a"), QStringLiteral("b"),
                                          QStringLiteral("password")));
    QVERIFY(!service.serverConnected());
}

void TestMediaServer::mediaServiceAdapterRegistry()
{
    MediaLibraryService service;
    QSignalSpy spy(&service, &MediaLibraryService::adaptersChanged);

    service.registerAdapter(nullptr);
    QCOMPARE(service.adapterCount(), 0);
    QCOMPARE(spy.count(), 0);

    service.registerAdapter(std::make_shared<JellyfinAdapter>(MediaServerAdapter::Kind::Jellyfin));
    QCOMPARE(service.adapterCount(), 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(service.adapters().first()->id(), QStringLiteral("jellyfin"));

    service.registerAdapter(std::make_shared<PlexAdapter>());
    QCOMPARE(service.adapterCount(), 2);
    QCOMPARE(spy.count(), 2);

    service.unregisterAdapter(QStringLiteral("plex"));
    QCOMPARE(service.adapterCount(), 1);
    service.unregisterAdapter(QStringLiteral("plex"));
    QCOMPARE(service.adapterCount(), 1);
    QCOMPARE(spy.count(), 3);
}

// ---------------------------------------------------------------- CredentialStore

void TestMediaServer::credentialStoreRoundTrip()
{
    QVERIFY(credential::supported());

    // 测试专用 target 名：绝不触碰 Palune/media-server 这条真实凭据。
    const QString service = QStringLiteral("PaluneUnitTest-%1")
                                .arg(QCoreApplication::applicationPid());
    const QString account = QStringLiteral("media-server-unit");
    CredentialCleanup cleanup(service, account);

    QString error;
    QVERIFY(credential::read(service, account, &error).isEmpty());
    QVERIFY(error.isEmpty()); // 不存在不算错误

    if (!credential::write(service, account, QStringLiteral("secret-1"), &error))
        QSKIP("本机系统凭据存储不可用，跳过写读验证");
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(credential::read(service, account, &error), QStringLiteral("secret-1"));
    QCOMPARE(credential::read(service, account, nullptr), QStringLiteral("secret-1"));

    // 覆盖写：更长的密钥，验证字节长度处理
    const QString longer = QStringLiteral("secret-") + QString(200, QLatin1Char('x'));
    QVERIFY(credential::write(service, account, longer));
    QCOMPARE(credential::read(service, account), longer);

    QVERIFY(credential::remove(service, account, &error));
    QVERIFY(credential::read(service, account).isEmpty());
    // 重复删除按成功处理（目标状态已达成）
    QVERIFY(credential::remove(service, account));
}

void TestMediaServer::credentialStoreValidation()
{
    QString error;
    QVERIFY(credential::read(QString(), QStringLiteral("account"), &error).isEmpty());
    QCOMPARE(error, QStringLiteral("凭据服务名或账户为空"));

    error.clear();
    QVERIFY(credential::read(QStringLiteral("service"), QString(), &error).isEmpty());
    QCOMPARE(error, QStringLiteral("凭据服务名或账户为空"));

    error.clear();
    QVERIFY(!credential::write(QString(), QStringLiteral("account"), QStringLiteral("s"), &error));
    QCOMPARE(error, QStringLiteral("凭据服务名或账户为空"));

    error.clear();
    QVERIFY(!credential::write(QStringLiteral("service"), QString(), QStringLiteral("s"), &error));
    QCOMPARE(error, QStringLiteral("凭据服务名或账户为空"));

    error.clear();
    QVERIFY(!credential::remove(QString(), QStringLiteral("account"), &error));
    QCOMPARE(error, QStringLiteral("凭据服务名或账户为空"));

    error.clear();
    QVERIFY(!credential::remove(QStringLiteral("service"), QString(), &error));
    QCOMPARE(error, QStringLiteral("凭据服务名或账户为空"));

    // error 为空指针时同样安全
    QVERIFY(credential::read(QString(), QString(), nullptr).isEmpty());
    QVERIFY(!credential::write(QString(), QString(), QString(), nullptr));
    QVERIFY(!credential::remove(QString(), QString(), nullptr));

    // 不存在的条目：空串且不报错
    error.clear();
    const QString missing = QStringLiteral("PaluneUnitTest-missing-%1")
                                .arg(QCoreApplication::applicationPid());
    QVERIFY(credential::read(missing, QStringLiteral("x"), &error).isEmpty());
    QVERIFY(error.isEmpty());
}

QTEST_GUILESS_MAIN(TestMediaServer)
#include "tst_media_server.moc"

