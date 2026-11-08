#include <QtTest>

#include "core/AppPaths.h"
#include "media/MediaLibraryService.h"
#include "settings/Settings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

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
        QVERIFY(service.emptyHint().contains(QStringLiteral("目录")));
        QCOMPARE(service.viewMode(), QStringLiteral("poster"));
        QVERIFY(!service.scanning());
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

    // M4：真实目录扫描 + JSON 索引 + 失效条目清理。
    void localScanIndexesAndPrunes()
    {
        QTemporaryDir config;
        QTemporaryDir media;
        QVERIFY(config.isValid() && media.isValid());
        AppPaths::setConfigDirOverride(config.path());

        auto touch = [](const QString &path) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        };
        touch(media.path() + "/a.mp4");
        touch(media.path() + "/b.mkv");
        touch(media.path() + "/ignore.txt");

        Settings settings(config.path() + "/settings.toml");
        settings.setValue(QStringLiteral("library.roots"), QStringList{media.path()});
        QVERIFY(settings.save());
        MediaLibraryService service;
        service.setSettings(&settings);
        service.load();

        QTRY_VERIFY_WITH_TIMEOUT(service.itemCount() == 2 && !service.scanning(), 30000);
        QCOMPARE(service.roots().size(), 1);
        QVERIFY(QFile::exists(service.indexFile()));

        QFile index(service.indexFile());
        QVERIFY(index.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(index.readAll());
        QCOMPARE(doc.object().value(QStringLiteral("items")).toArray().size(), 2);

        // 源文件消失后重新扫描，索引里不再保留该条目。
        QVERIFY(QFile::remove(media.path() + "/a.mp4"));
        service.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(service.itemCount() == 1 && !service.scanning(), 30000);

        // 失效根目录在 load() 时被剔除，不参与扫描。
        settings.setValue(QStringLiteral("library.roots"),
                          QStringList{media.path(), media.path() + "/missing"});
        QVERIFY(settings.save());
        MediaLibraryService reloaded;
        reloaded.setSettings(&settings);
        reloaded.load();
        QCOMPARE(reloaded.roots().size(), 1);

        AppPaths::setConfigDirOverride(QString());
    }

    // 损坏索引：不清空用户库，靠一次重扫重建索引文件。
    void corruptedIndexRebuilds()
    {
        QTemporaryDir config;
        QTemporaryDir media;
        QVERIFY(config.isValid() && media.isValid());
        AppPaths::setConfigDirOverride(config.path());
        QFile seed(media.path() + "/clip.mp4");
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("x");
        seed.close();

        Settings settings(config.path() + "/settings.toml");
        settings.setValue(QStringLiteral("library.roots"), QStringList{media.path()});
        QVERIFY(settings.save());

        QDir().mkpath(config.path() + "/library");
        QFile broken(config.path() + "/library/index.json");
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ this is not json ");
        broken.close();

        MediaLibraryService service;
        service.setSettings(&settings);
        service.load();
        QTRY_VERIFY_WITH_TIMEOUT(service.itemCount() == 1 && !service.scanning(), 30000);

        QFile rebuilt(service.indexFile());
        QVERIFY(rebuilt.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(rebuilt.readAll()).isObject());

        AppPaths::setConfigDirOverride(QString());
    }

    // 大目录：200 个文件必须在上限内完成索引（不阻塞主线程，扫描线程可测）。
    void largeDirectoryScanStaysBounded()
    {
        QTemporaryDir config;
        QTemporaryDir media;
        QVERIFY(config.isValid() && media.isValid());
        AppPaths::setConfigDirOverride(config.path());
        for (int i = 0; i < 200; ++i) {
            QFile file(media.path() + QStringLiteral("/clip-%1.mp4").arg(i, 3, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }

        Settings settings(config.path() + "/settings.toml");
        settings.setValue(QStringLiteral("library.roots"), QStringList{media.path()});
        QVERIFY(settings.save());

        QElapsedTimer timer;
        timer.start();
        MediaLibraryService service;
        service.setSettings(&settings);
        service.load();
        QTRY_VERIFY_WITH_TIMEOUT(service.itemCount() == 200 && !service.scanning(), 30000);
        qInfo() << "200 文件扫描耗时(含缩略图尝试):" << timer.elapsed() << "ms";
        QVERIFY2(timer.elapsed() < 30000, "目录扫描超时");

        AppPaths::setConfigDirOverride(QString());
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
