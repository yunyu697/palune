#include <QtTest>

#include "media/MediaLibraryService.h"

using namespace palune;

namespace {

class FakeAdapter : public MediaServerAdapter
{
public:
    explicit FakeAdapter(const QString &id) : m_id(id) {}

    Kind kind() const override { return Kind::Jellyfin; }
    QString id() const override { return m_id; }
    QString displayName() const override { return QStringLiteral("测试内容源"); }

    AuthResult authenticate(const QUrl &, const QString &, const QString &) override
    {
        AuthResult result;
        result.ok = true;
        result.token = QStringLiteral("token");
        return result;
    }
    CatalogPage fetchItems(const QString &libraryId, int offset, int limit) override
    {
        CatalogPage page;
        if (offset == 0 && limit > 0) {
            MediaItem item;
            item.id = QStringLiteral("item-1");
            item.libraryId = libraryId;
            item.name = QStringLiteral("测试影片");
            page.items << item;
        }
        return page;
    }
    QUrl playbackUrl(const MediaItem &item, PlaybackMode *mode) const override
    {
        if (mode) *mode = PlaybackMode::DirectPlay;
        return QUrl(QStringLiteral("https://example.com/") + item.id);
    }
    ProgressResult reportProgress(const QString &, const MediaItem &, const UserState &) override
    {
        return ProgressResult{true, {}};
    }
    MediaCapability capabilities() const override
    {
        MediaCapability c;
        c.auth = c.catalog = c.playback = c.progress = true;
        return c;
    }

private:
    QString m_id;
};

} // namespace

class TestMedia : public QObject
{
    Q_OBJECT

private slots:
    void emptyLibraryIsUnavailableButUsable()
    {
        MediaLibraryService service;
        QVERIFY(!service.available());
        QVERIFY(!service.paired());
        QVERIFY(service.libraries().isEmpty());
        QVERIFY(service.emptyTitle().contains(QStringLiteral("媒体库")));
        QVERIFY(service.emptyHint().contains(QStringLiteral("M1")));
        service.refresh(); // 不应崩溃、不联网
    }

    void adapterRegistryRejectsDuplicates()
    {
        MediaLibraryService service;
        auto first = std::make_shared<FakeAdapter>(QStringLiteral("jellyfin"));
        service.registerAdapter(first);
        service.registerAdapter(std::make_shared<FakeAdapter>(QStringLiteral("jellyfin")));
        QCOMPARE(service.adapterCount(), 1);
        service.registerAdapter(std::make_shared<FakeAdapter>(QStringLiteral("emby")));
        QCOMPARE(service.adapterCount(), 2);

        service.unregisterAdapter(QStringLiteral("emby"));
        QCOMPARE(service.adapterCount(), 1);
        service.unregisterAdapter(QStringLiteral("missing"));
        QCOMPARE(service.adapterCount(), 1);
    }

    void adapterContractIsComplete()
    {
        FakeAdapter adapter(QStringLiteral("fake"));
        const auto auth = adapter.authenticate(QUrl(QStringLiteral("https://srv.example")),
                                               QStringLiteral("u"), QStringLiteral("p"));
        QVERIFY(auth.ok);
        const auto page = adapter.fetchItems(QStringLiteral("lib"), 0, 20);
        QCOMPARE(page.items.size(), 1);
        QCOMPARE(page.items.first().name, QStringLiteral("测试影片"));
        PlaybackMode mode = PlaybackMode::Transcode;
        const QUrl url = adapter.playbackUrl(page.items.first(), &mode);
        QCOMPARE(mode, PlaybackMode::DirectPlay);
        QVERIFY(url.toString().endsWith(QStringLiteral("item-1")));
        QCOMPARE(adapter.capabilities().progress, true);
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"
