// PlayQueue 行为测试：追加去重、切换当前项、排序保位、保存/载入往返、自动落盘。
// 这些是 P0「真正的播放队列」的判定依据，不依赖 libmpv 与窗口。

#include "ui/PlayQueue.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using palune::PlayQueue;

namespace {

QStringList paths(const PlayQueue &queue)
{
    QStringList out;
    for (const QVariant &item : queue.items())
        out << item.toMap().value(QStringLiteral("path")).toString();
    return out;
}

} // namespace

class TstPlayQueue : public QObject
{
    Q_OBJECT

private slots:
    void appendDeduplicates();
    void setUrlsClampsStartIndex();
    void removeAtKeepsCurrentValid();
    void moveSwapsAndTracksCurrent();
    void sortByNameIsNaturalAndKeepsCurrent();
    void saveLoadRoundTrip();
    void autoSaveWritesPersistPath();
    void networkUrlKeptVerbatim();
};

void TstPlayQueue::appendDeduplicates()
{
    PlayQueue queue;
    QSignalSpy spy(&queue, &PlayQueue::changed);
    const int first = queue.append({QStringLiteral("a.mkv"), QStringLiteral("b.mkv"),
                                    QStringLiteral("a.mkv")});
    QCOMPARE(first, 0);
    QCOMPARE(queue.count(), 2);
    QCOMPARE(paths(queue), QStringList({QStringLiteral("a.mkv"), QStringLiteral("b.mkv")}));
    QCOMPARE(spy.count(), 1);

    // 全重复时不产生变更信号，返回 -1。
    QCOMPARE(queue.append({QStringLiteral("b.mkv")}), -1);
    QCOMPARE(spy.count(), 1);
}

void TstPlayQueue::setUrlsClampsStartIndex()
{
    PlayQueue queue;
    queue.setUrls({QStringLiteral("a.mkv"), QStringLiteral("b.mkv")}, 9);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.count(), 2);

    queue.setUrls({QStringLiteral("x.mkv")}, -5);
    QCOMPARE(queue.currentIndex(), 0);

    queue.setUrls({}, 0);
    QCOMPARE(queue.empty(), true);
    QCOMPARE(queue.currentIndex(), -1);
}

void TstPlayQueue::removeAtKeepsCurrentValid()
{
    PlayQueue queue;
    queue.setUrls({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 2);
    queue.removeAt(0); // 删除当前项之前的条目，当前索引左移
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(paths(queue), QStringList({QStringLiteral("b"), QStringLiteral("c")}));

    queue.removeAt(1); // 删除当前项，索引收敛到末尾
    QCOMPARE(queue.currentIndex(), 0);

    queue.removeAt(0);
    QCOMPARE(queue.empty(), true);
    QCOMPARE(queue.currentIndex(), -1);

    queue.removeAt(5); // 越界不做任何事
    QCOMPARE(queue.count(), 0);
}

void TstPlayQueue::moveSwapsAndTracksCurrent()
{
    PlayQueue queue;
    queue.setUrls({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 1);
    queue.moveUp(1);
    QCOMPARE(paths(queue), QStringList({QStringLiteral("b"), QStringLiteral("a"),
                                        QStringLiteral("c")}));
    QCOMPARE(queue.currentIndex(), 0);

    queue.moveDown(0);
    QCOMPARE(paths(queue), QStringList({QStringLiteral("a"), QStringLiteral("b"),
                                        QStringLiteral("c")}));
    QCOMPARE(queue.currentIndex(), 1);

    queue.moveUp(0); // 越界不动
    QCOMPARE(queue.currentIndex(), 1);
}

void TstPlayQueue::sortByNameIsNaturalAndKeepsCurrent()
{
    PlayQueue queue;
    queue.setUrls({QStringLiteral("E10.mkv"), QStringLiteral("E2.mkv"),
                   QStringLiteral("E1.mkv")}, 1);
    const QString currentPath = paths(queue).value(1);
    queue.sortByName();
    QCOMPARE(paths(queue), QStringList({QStringLiteral("E1.mkv"), QStringLiteral("E2.mkv"),
                                        QStringLiteral("E10.mkv")}));
    // 排序后当前项仍指向同一文件，而不是同一个下标。
    QCOMPARE(paths(queue).value(queue.currentIndex()), currentPath);
}

void TstPlayQueue::saveLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("queue.json"));

    PlayQueue queue;
    queue.setUrls({QStringLiteral("/media/a.mkv"), QStringLiteral("https://example.com/b.mkv")}, 1);
    QVERIFY(queue.save(file));

    QJsonObject root = QJsonDocument::fromJson([&file] {
        QFile f(file);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }()).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("current")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("items")).toArray().size(), 2);

    PlayQueue restored;
    QVERIFY(restored.load(file));
    QCOMPARE(paths(restored), paths(queue));
    QCOMPARE(restored.currentIndex(), 1);
    // 名称由路径推导，网络地址取 URL 末段。
    QCOMPARE(restored.items().value(1).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("b.mkv"));

    // 文件不存在与格式非法都返回 false，且不改动现有队列。
    QVERIFY(!restored.load(dir.filePath(QStringLiteral("missing.json"))));
    const QString broken = dir.filePath(QStringLiteral("broken.json"));
    {
        QFile f(broken);
        f.open(QIODevice::WriteOnly);
        f.write("not json");
    }
    QVERIFY(!restored.load(broken));
    QCOMPARE(restored.count(), 2);
}

void TstPlayQueue::autoSaveWritesPersistPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString persist = dir.filePath(QStringLiteral("nested/queue.json"));

    PlayQueue queue;
    queue.setPersistPath(persist);
    queue.append({QStringLiteral("a.mkv")});
    QVERIFY(QFile::exists(persist));

    PlayQueue reloaded;
    QVERIFY(reloaded.load(persist));
    QCOMPARE(paths(reloaded), QStringList({QStringLiteral("a.mkv")}));
}

void TstPlayQueue::networkUrlKeptVerbatim()
{
    PlayQueue queue;
    queue.setUrls({QStringLiteral("https://example.com/live/index.m3u8")}, 0);
    QCOMPARE(paths(queue), QStringList({QStringLiteral("https://example.com/live/index.m3u8")}));
    QCOMPARE(queue.items().value(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("index.m3u8"));
}

QTEST_MAIN(TstPlayQueue)
#include "tst_playqueue.moc"
