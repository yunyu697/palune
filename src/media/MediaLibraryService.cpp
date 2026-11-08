#include "media/MediaLibraryService.h"

#include "core/AppPaths.h"
#include "diag/Log.h"
#include "media/ThumbnailProvider.h"
#include "settings/Settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRunnable>
#include <QSaveFile>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <functional>

namespace palune {

namespace {

// 扫描上限：索引不至于无限增长，海报墙一次也不会取上万张贴图。
constexpr int kMaxItems = 2000;
// 每轮扫描最多补多少张缩略图，避免首次扫描长时间占满工作线程。
constexpr int kMaxThumbsPerScan = 160;

const QStringList kMediaSuffixes = {
    QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("webm"),
    QStringLiteral("avi"), QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("flv"),
    QStringLiteral("wmv"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ogv"),
    QStringLiteral("m4a"), QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
    QStringLiteral("aac"), QStringLiteral("opus")
};

QStringList mediaPatterns()
{
    QStringList patterns;
    patterns.reserve(kMediaSuffixes.size());
    for (const QString &suffix : kMediaSuffixes)
        patterns << QStringLiteral("*.") + suffix;
    return patterns;
}

QString itemId(const QString &absolutePath)
{
    const QByteArray hash = QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(hash.toHex().left(16));
}

QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("-");
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return QString::number(mb / 1024.0, 'f', 2) + QStringLiteral(" GB");
    if (mb >= 1.0)
        return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
    return QString::number(static_cast<double>(bytes) / 1024.0, 'f', 0) + QStringLiteral(" KB");
}

class FunctionTask : public QRunnable
{
public:
    explicit FunctionTask(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void run() override { m_fn(); }

private:
    std::function<void()> m_fn;
};

QVariantList readIndexFile(const QString &path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return {};
    const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
    QVariantList out;
    out.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const QString filePath = obj.value(QStringLiteral("path")).toString();
        if (filePath.isEmpty() || !QFileInfo::exists(filePath))
            continue;
        QVariantMap item = obj.toVariantMap();
        const QString poster = obj.value(QStringLiteral("poster")).toString();
        item.insert(QStringLiteral("poster"),
                    !poster.isEmpty() && QFileInfo::exists(poster)
                        ? QUrl::fromLocalFile(poster).toString() : QString());
        out << item;
    }
    return out;
}

} // namespace

MediaLibraryService::MediaLibraryService(QObject *parent)
    : QObject(parent)
{
}

void MediaLibraryService::setSettings(Settings *settings)
{
    m_settings = settings;
}

void MediaLibraryService::load()
{
    if (m_settings) {
        m_roots = m_settings->value(QStringLiteral("library.roots")).toStringList();
        const QString mode = m_settings->value(QStringLiteral("library.viewMode")).toString();
        if (mode == QLatin1String("list") || mode == QLatin1String("poster"))
            m_viewMode = mode;
    }
    for (int i = m_roots.size() - 1; i >= 0; --i) {
        if (m_roots.at(i).trimmed().isEmpty() || !QFileInfo(m_roots.at(i)).isDir())
            m_roots.removeAt(i);
    }
    emit rootsChanged();
    emit viewModeChanged();

    m_items = readIndexFile(indexFile());
    emit itemsChanged();
    emit libraryChanged();

    if (!m_roots.isEmpty()) {
        // 启动后的首轮扫描延后一拍，不抢窗口创建与首帧的 I/O。
        QTimer::singleShot(600, this, [this]() { scanInBackground(); });
    }
}

void MediaLibraryService::registerAdapter(std::shared_ptr<MediaServerAdapter> adapter)
{
    if (!adapter)
        return;
    for (const auto &existing : m_adapters) {
        if (existing->id() == adapter->id()) {
            Log::warn(QStringLiteral("媒体库适配器重复注册：%1").arg(adapter->id()));
            return;
        }
    }
    m_adapters.push_back(std::move(adapter));
    Log::info(QStringLiteral("媒体库适配器已注册：%1").arg(m_adapters.back()->id()));
    emit adaptersChanged();
}

void MediaLibraryService::unregisterAdapter(const QString &id)
{
    for (int i = 0; i < m_adapters.size(); ++i) {
        if (m_adapters.at(i)->id() == id) {
            m_adapters.removeAt(i);
            emit adaptersChanged();
            return;
        }
    }
}

void MediaLibraryService::refresh()
{
    if (m_roots.isEmpty()) {
        emit libraryChanged();
        return;
    }
    scanInBackground();
}

void MediaLibraryService::addRoot()
{
    const QString start = m_roots.isEmpty() ? QDir::homePath() : m_roots.last();
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, QCoreApplication::translate("Palune", "选择视频目录"), start);
    if (dir.isEmpty())
        return;
    const QString normalized = QDir::fromNativeSeparators(QFileInfo(dir).absoluteFilePath());
    if (m_roots.contains(normalized))
        return;
    m_roots << normalized;
    emit rootsChanged();
    if (m_settings) {
        m_settings->setValue(QStringLiteral("library.roots"), m_roots);
        m_settings->save();
    }
    scanInBackground();
}

void MediaLibraryService::removeRoot(const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path);
    const int index = m_roots.indexOf(normalized);
    if (index < 0)
        return;
    m_roots.removeAt(index);
    emit rootsChanged();
    if (m_settings) {
        m_settings->setValue(QStringLiteral("library.roots"), m_roots);
        m_settings->save();
    }
    scanInBackground();
}

void MediaLibraryService::rescan()
{
    scanInBackground();
}

void MediaLibraryService::setViewMode(const QString &mode)
{
    if (mode != QLatin1String("poster") && mode != QLatin1String("list"))
        return;
    if (m_viewMode == mode)
        return;
    m_viewMode = mode;
    emit viewModeChanged();
    if (m_settings) {
        m_settings->setValue(QStringLiteral("library.viewMode"), mode);
        m_settings->save();
    }
}

QString MediaLibraryService::indexFile() const
{
    return AppPaths::configDir() + QStringLiteral("/library/index.json");
}

QString MediaLibraryService::thumbDir() const
{
    return AppPaths::configDir() + QStringLiteral("/library/thumbs");
}

void MediaLibraryService::scanInBackground()
{
    if (m_scanning || m_roots.isEmpty())
        return;

    setScanning(true);
    setScanStatus(QCoreApplication::translate("Palune", "正在扫描…"));
    const QStringList roots = m_roots;
    const QString thumbs = thumbDir();

    auto task = new FunctionTask([this, roots, thumbs]() {
        QVariantList scanned;
        int skipped = 0;
        for (const QString &root : roots) {
            if (!QFileInfo(root).isDir())
                continue;
            QDirIterator it(root, mediaPatterns(), QDir::Files | QDir::Readable | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                if (scanned.size() >= kMaxItems) {
                    ++skipped;
                    continue;
                }
                const QFileInfo info = it.fileInfo();
                const QString absolute = QDir::fromNativeSeparators(info.absoluteFilePath());
                const QString id = itemId(absolute);
                const QString suffix = info.suffix().toLower();
                const QString modified = info.lastModified().toString(Qt::ISODate);
                const QString thumbPath = thumbs + QLatin1Char('/') + id + QLatin1Char('-')
                                          + QString::number(info.lastModified().toMSecsSinceEpoch())
                                          + QStringLiteral(".jpg");

                QVariantMap item;
                item.insert(QStringLiteral("id"), id);
                item.insert(QStringLiteral("title"), info.completeBaseName());
                item.insert(QStringLiteral("fileName"), info.fileName());
                item.insert(QStringLiteral("path"), absolute);
                item.insert(QStringLiteral("url"), QUrl::fromLocalFile(absolute).toString());
                item.insert(QStringLiteral("folder"), QDir::fromNativeSeparators(info.absolutePath()));
                item.insert(QStringLiteral("suffix"), suffix);
                item.insert(QStringLiteral("size"), static_cast<qint64>(info.size()));
                item.insert(QStringLiteral("sizeText"), humanSize(info.size()));
                item.insert(QStringLiteral("modified"), modified);
                item.insert(QStringLiteral("modifiedText"),
                            info.lastModified().toString(QStringLiteral("yyyy-MM-dd")));
                item.insert(QStringLiteral("thumb"), thumbPath);
                item.insert(QStringLiteral("poster"),
                            QFileInfo::exists(thumbPath)
                                ? QUrl::fromLocalFile(thumbPath).toString() : QString());
                scanned << item;
            }
        }

        std::sort(scanned.begin(), scanned.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("title")).toString().toLower()
                 < b.toMap().value(QStringLiteral("title")).toString().toLower();
        });

        // 缩略图在后台补齐；资源管理器无缩略图处理器时失败，QML 显示占位。
        int generated = 0;
        for (QVariant &entry : scanned) {
            QVariantMap item = entry.toMap();
            const QString thumb = item.value(QStringLiteral("thumb")).toString();
            if (item.value(QStringLiteral("poster")).toString().isEmpty()
                    && !QFileInfo::exists(thumb)) {
                if (generated >= kMaxThumbsPerScan)
                    continue;
                if (ThumbnailProvider::save(item.value(QStringLiteral("path")).toString(), thumb,
                                            QSize(320, 180))) {
                    item.insert(QStringLiteral("poster"),
                                QUrl::fromLocalFile(thumb).toString());
                    ++generated;
                }
            }
            entry = item;
        }

        const QString status = skipped > 0
            ? QCoreApplication::translate("Palune", "已索引 %1 项（上限 %2），缩略图新增 %3")
                  .arg(scanned.size()).arg(kMaxItems).arg(generated)
            : QCoreApplication::translate("Palune", "已索引 %1 项，缩略图新增 %2")
                  .arg(scanned.size()).arg(generated);

        QMetaObject::invokeMethod(
            this,
            [this, scanned, status]() {
                m_items = scanned;
                persistIndex(scanned);
                emit itemsChanged();
                emit libraryChanged();
                setScanStatus(status);
                setScanning(false);
                Log::info(QStringLiteral("媒体库扫描完成：%1 项").arg(scanned.size()));
            },
            Qt::QueuedConnection);
    });

    QThreadPool::globalInstance()->start(task);
}

void MediaLibraryService::persistIndex(const QVariantList &items) const
{
    const QString path = indexFile();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray array;
    for (const QVariant &entry : items) {
        const QVariantMap map = entry.toMap();
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), map.value(QStringLiteral("id")).toString());
        obj.insert(QStringLiteral("title"), map.value(QStringLiteral("title")).toString());
        obj.insert(QStringLiteral("fileName"), map.value(QStringLiteral("fileName")).toString());
        obj.insert(QStringLiteral("path"), map.value(QStringLiteral("path")).toString());
        obj.insert(QStringLiteral("folder"), map.value(QStringLiteral("folder")).toString());
        obj.insert(QStringLiteral("suffix"), map.value(QStringLiteral("suffix")).toString());
        obj.insert(QStringLiteral("size"), map.value(QStringLiteral("size")).toDouble());
        obj.insert(QStringLiteral("sizeText"), map.value(QStringLiteral("sizeText")).toString());
        obj.insert(QStringLiteral("modified"), map.value(QStringLiteral("modified")).toString());
        obj.insert(QStringLiteral("modifiedText"), map.value(QStringLiteral("modifiedText")).toString());
        obj.insert(QStringLiteral("poster"), map.value(QStringLiteral("thumb")).toString());
        array << obj;
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("roots"), QJsonArray::fromStringList(m_roots));
    root.insert(QStringLiteral("items"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void MediaLibraryService::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

void MediaLibraryService::setScanStatus(const QString &status)
{
    if (m_scanStatus == status)
        return;
    m_scanStatus = status;
    emit scanStatusChanged();
}

} // namespace palune
