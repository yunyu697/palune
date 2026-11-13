#include "media/MediaLibraryService.h"

#include "core/AppPaths.h"
#include "core/CredentialStore.h"
#include "diag/Log.h"
#include "media/JellyfinAdapter.h"
#include "media/ThumbnailProvider.h"
#include "settings/Settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
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

// 系统凭据存储里的固定条目：同一安装只保存一台媒体服务器的会话。
const QString kCredentialService = QStringLiteral("Palune");
const QString kCredentialAccount = QStringLiteral("media-server");

MediaServerAdapter::Kind kindFromId(const QString &kind)
{
    if (kind == QLatin1String("emby"))
        return MediaServerAdapter::Kind::Emby;
    if (kind == QLatin1String("zhispace"))
        return MediaServerAdapter::Kind::ZhiSpace;
    if (kind == QLatin1String("feiniu"))
        return MediaServerAdapter::Kind::FeiNiu;
    if (kind == QLatin1String("jellyfin-compat"))
        return MediaServerAdapter::Kind::OtherJellyfin;
    return MediaServerAdapter::Kind::Jellyfin;
}

bool kindSupported(const QString &kind)
{
    return kind == QLatin1String("jellyfin") || kind == QLatin1String("emby")
        || kind == QLatin1String("jellyfin-compat") || kind == QLatin1String("zhispace")
        || kind == QLatin1String("feiniu");
}

// 设置页状态口径：认证失败与不可达分开显示，避免「密码错」被说成「连不上」。
QString failureText(const QString &error)
{
    if (error.startsWith(QStringLiteral("认证失败")))
        return error;
    if (error.contains(QStringLiteral("HTTP 401")) || error.contains(QStringLiteral("HTTP 403"))
        || error.contains(QStringLiteral("API Key")))
        return QStringLiteral("认证失败：%1").arg(error);
    return QStringLiteral("无法连接（不可达）：%1").arg(error);
}

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

qint64 dirTotalBytes(const QString &path)
{
    qint64 total = 0;
    QDir dir(path);
    if (!dir.exists())
        return 0;
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &info : entries) {
        if (info.isDir())
            total += dirTotalBytes(info.absoluteFilePath());
        else
            total += info.size();
    }
    return total;
}

// 按修改时间从旧到新删除，直到目录总量回到上限以下；返回删除个数。
int pruneThumbDir(const QString &path, qint64 limitBytes)
{
    if (limitBytes <= 0)
        return 0;
    QDir dir(path);
    if (!dir.exists())
        return 0;
    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo &info : files)
        total += info.size();
    int removed = 0;
    for (const QFileInfo &info : files) {
        if (total <= limitBytes)
            break;
        const qint64 size = info.size();
        if (QFile::remove(info.absoluteFilePath())) {
            total -= size;
            ++removed;
        }
    }
    return removed;
}

void clearDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return;
    dir.removeRecursively();
    dir.mkpath(QStringLiteral("."));
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

    m_localItems = readIndexFile(indexFile());
    rebuildItems();
    emit libraryChanged();

    // 已保存的服务器会话：启动后延后一拍拉取，避免拖慢首帧。
    if (m_settings) {
        m_serverKind = m_settings->value(QStringLiteral("media.server.kind")).toString();
        m_serverUrl = m_settings->value(QStringLiteral("media.server.url")).toString();
        m_serverUser = m_settings->value(QStringLiteral("media.server.userId")).toString();
        m_serverAccount = m_settings->value(QStringLiteral("media.server.user")).toString();
        m_serverAuthMode = m_settings->value(QStringLiteral("media.server.authMode")).toString();
        if (m_serverAuthMode.isEmpty())
            m_serverAuthMode = QStringLiteral("password");

        // token 只从系统凭据存储读取；旧版本的明文键迁移后立即从 settings.toml 删除。
        QString token = credential::read(kCredentialService, kCredentialAccount);
        const QString legacyToken = m_settings->value(QStringLiteral("media.server.token")).toString();
        if (!legacyToken.isEmpty()) {
            QString credentialError;
            if (credential::write(kCredentialService, kCredentialAccount, legacyToken,
                                  &credentialError)) {
                m_settings->removeValue(QStringLiteral("media.server.token"));
                m_settings->save();
                Log::info(QStringLiteral("媒体服务器 token 已迁移至系统凭据存储，"
                                         "并从 settings.toml 删除"));
            } else {
                Log::warn(QStringLiteral("媒体服务器 token 迁移失败（%1），"
                                         "本次仍使用旧值，明文键暂不删除")
                              .arg(credentialError));
            }
            token = legacyToken;
        }

        if (!m_serverKind.isEmpty() && !m_serverUrl.isEmpty() && !token.isEmpty()) {
            auto adapter = std::make_shared<JellyfinAdapter>(kindFromId(m_serverKind));
            bool ready = true;
            if (m_serverUser.isEmpty()) {
                // 旧配置缺 userId（例如只保存了 API Key）：用 /Users/Me 补查。
                const auto auth = adapter->authenticateWithApiKey(QUrl(m_serverUrl), token);
                if (!auth.ok) {
                    setServerFailure(auth.error);
                    ready = false;
                } else {
                    m_serverUser = auth.userId;
                    m_settings->setValue(QStringLiteral("media.server.userId"), m_serverUser);
                    m_settings->save();
                }
            } else {
                adapter->restoreSession(QUrl(m_serverUrl), m_serverUser, token);
            }
            if (ready) {
                m_server = adapter;
                m_serverFailed = false;
                setServerStatus(QStringLiteral("已连接 %1").arg(adapter->displayName()));
                QTimer::singleShot(800, this, [this]() { refreshServer(); });
            }
            emit serverStatusChanged();
        } else {
            m_serverFailed = false;
            emit serverStatusChanged();
        }
    }

    if (!m_roots.isEmpty()) {
        // 启动后的首轮扫描延后一拍，不抢窗口创建与首帧的 I/O。
        QTimer::singleShot(600, this, [this]() { scanInBackground(); });
    }
}

void MediaLibraryService::rebuildItems()
{
    QVariantList merged = m_localItems;
    merged.append(m_remoteItems);
    m_items = merged;
    emit itemsChanged();
}

void MediaLibraryService::setServerStatus(const QString &status)
{
    if (m_serverStatus == status)
        return;
    m_serverStatus = status;
    emit serverStatusChanged();
}

void MediaLibraryService::setServerFailure(const QString &error)
{
    m_serverFailed = true;
    setServerStatus(failureText(error));
}

bool MediaLibraryService::connectServer(const QUrl &baseUrl, const QString &kind,
                                        const QString &user, const QString &secret,
                                        const QString &authMode, QString *error)
{
    if (!kindSupported(kind)) {
        const QString message = QStringLiteral("不支持的服务器类型：%1").arg(kind);
        m_serverFailed = true;
        setServerStatus(message);
        if (error)
            *error = message;
        return false;
    }
    auto adapter = std::make_shared<JellyfinAdapter>(kindFromId(kind));
    const bool apiKey = authMode == QLatin1String("apikey");
    const auto auth = apiKey ? adapter->authenticateWithApiKey(baseUrl, secret)
                             : adapter->authenticate(baseUrl, user, secret);
    if (!auth.ok) {
        setServerFailure(auth.error);
        if (error)
            *error = auth.error;
        return false;
    }

    // 先落系统凭据存储；写入失败就不改配置，避免出现「配置有、token 丢」的半截状态。
    QString credentialError;
    if (!credential::write(kCredentialService, kCredentialAccount, auth.token, &credentialError)) {
        const QString message =
            QStringLiteral("保存失败：系统凭据存储不可用（%1）").arg(credentialError);
        m_serverFailed = true;
        setServerStatus(message);
        if (error)
            *error = message;
        return false;
    }

    m_server = adapter;
    m_serverKind = kind;
    m_serverUrl = baseUrl.toString();
    m_serverUser = auth.userId;
    m_serverAccount = apiKey ? QString() : user;
    m_serverAuthMode = apiKey ? QStringLiteral("apikey") : QStringLiteral("password");
    if (m_settings) {
        m_settings->setValue(QStringLiteral("media.server.kind"), m_serverKind);
        m_settings->setValue(QStringLiteral("media.server.url"), m_serverUrl);
        m_settings->setValue(QStringLiteral("media.server.userId"), m_serverUser);
        m_settings->setValue(QStringLiteral("media.server.authMode"), m_serverAuthMode);
        if (m_serverAccount.isEmpty())
            m_settings->removeValue(QStringLiteral("media.server.user"));
        else
            m_settings->setValue(QStringLiteral("media.server.user"), m_serverAccount);
        // 明文键必须消失：token 只在系统凭据存储里。
        m_settings->removeValue(QStringLiteral("media.server.token"));
        m_settings->save();
    }
    setServerStatus(QStringLiteral("已连接 %1 %2")
                        .arg(adapter->displayName(), adapter->capabilities().serverVersion));
    m_serverFailed = false;
    emit serverStatusChanged();
    refreshServer();
    return true;
}

bool MediaLibraryService::testServerConnection(const QString &kind, const QString &url,
                                               const QString &user, const QString &secret,
                                               const QString &authMode)
{
    if (!kindSupported(kind)) {
        m_serverFailed = true;
        setServerStatus(QStringLiteral("不支持的服务器类型：%1").arg(kind));
        return false;
    }
    auto adapter = std::make_shared<JellyfinAdapter>(kindFromId(kind));
    const auto auth = authMode == QLatin1String("apikey")
                          ? adapter->authenticateWithApiKey(QUrl(url), secret)
                          : adapter->authenticate(QUrl(url), user, secret);
    if (!auth.ok) {
        setServerFailure(auth.error);
        return false;
    }
    m_serverFailed = false;
    setServerStatus(QStringLiteral("连接成功：%1 %2（未保存）")
                        .arg(adapter->displayName(), adapter->capabilities().serverVersion));
    return true;
}

bool MediaLibraryService::saveServerConnection(const QString &kind, const QString &url,
                                               const QString &user, const QString &secret,
                                               const QString &authMode)
{
    QString error;
    return connectServer(QUrl(url), kind, user, secret, authMode, &error);
}

void MediaLibraryService::removeServerConnection()
{
    disconnectServer();
}

void MediaLibraryService::disconnectServer()
{
    m_server.reset();
    m_serverItems.clear();
    m_remoteItems.clear();
    m_playSessions.clear();
    QString credentialError;
    if (!credential::remove(kCredentialService, kCredentialAccount, &credentialError))
        Log::warn(QStringLiteral("删除系统凭据失败：%1").arg(credentialError));
    m_serverKind.clear();
    m_serverUrl.clear();
    m_serverUser.clear();
    m_serverAccount.clear();
    m_serverAuthMode = QStringLiteral("password");
    if (m_settings) {
        m_settings->removeValue(QStringLiteral("media.server.kind"));
        m_settings->removeValue(QStringLiteral("media.server.url"));
        m_settings->removeValue(QStringLiteral("media.server.userId"));
        m_settings->removeValue(QStringLiteral("media.server.user"));
        m_settings->removeValue(QStringLiteral("media.server.authMode"));
        m_settings->removeValue(QStringLiteral("media.server.token"));
        m_settings->save();
    }
    setServerStatus(QString());
    m_serverFailed = false;
    emit serverStatusChanged();
    rebuildItems();
    emit libraryChanged();
}

void MediaLibraryService::refreshServer()
{
    if (!m_server) {
        m_serverFailed = false;
        setServerStatus(QStringLiteral("未连接媒体服务器"));
        return;
    }
    QList<MediaItem> all;
    int offset = 0;
    bool hasMore = true;
    QString failure;
    while (hasMore && all.size() < kMaxItems) {
        const auto page = m_server->fetchItems(QString(), offset, 200);
        if (!page.error.isEmpty()) {
            failure = page.error;
            break;
        }
        all.append(page.items);
        offset += page.items.size();
        hasMore = page.hasMore && !page.items.isEmpty();
    }
    if (!failure.isEmpty()) {
        m_serverFailed = true;
        setServerStatus(QStringLiteral("拉取媒体库失败：%1").arg(failure));
        Log::warn(QStringLiteral("媒体服务器拉取失败：%1").arg(failure));
        return;
    }

    m_serverItems = all;
    m_remoteItems.clear();
    for (const MediaItem &item : all)
        m_remoteItems << mapRemoteItem(item);
    rebuildItems();
    emit libraryChanged();
    m_serverFailed = false;
    setServerStatus(QStringLiteral("已连接 %1 · %2 项")
                        .arg(m_server->displayName())
                        .arg(all.size()));
    Log::info(QStringLiteral("媒体服务器媒体库：%1 项（%2）")
                  .arg(all.size()).arg(m_server->id()));
}

QVariantMap MediaLibraryService::mapRemoteItem(const MediaItem &item) const
{
    const QString stream = item.streamUrl.toString();
    QString title = item.name;
    if (item.type == QLatin1String("episode") && !item.seriesName.isEmpty()) {
        title = QStringLiteral("%1 S%2E%3 — %4")
                    .arg(item.seriesName)
                    .arg(item.season, 2, 10, QLatin1Char('0'))
                    .arg(item.episode, 2, 10, QLatin1Char('0'))
                    .arg(item.name);
    }
    const int totalSeconds = static_cast<int>(item.durationSeconds);
    QVariantMap map;
    map.insert(QStringLiteral("id"), item.id);
    map.insert(QStringLiteral("title"), title);
    map.insert(QStringLiteral("fileName"), item.name);
    map.insert(QStringLiteral("path"), stream);
    map.insert(QStringLiteral("url"), item.posterUrl.toString());
    map.insert(QStringLiteral("folder"), m_server ? m_server->displayName() : QString());
    map.insert(QStringLiteral("suffix"),
               item.container.isEmpty() ? QStringLiteral("stream") : item.container);
    map.insert(QStringLiteral("size"), qint64{0});
    map.insert(QStringLiteral("sizeText"),
               QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10,
                                                                 QLatin1Char('0')));
    map.insert(QStringLiteral("modified"), QString());
    map.insert(QStringLiteral("modifiedText"), QString());
    map.insert(QStringLiteral("thumb"), QString());
    map.insert(QStringLiteral("poster"), item.posterUrl.toString());
    map.insert(QStringLiteral("remote"), true);
    map.insert(QStringLiteral("durationSeconds"), item.durationSeconds);
    map.insert(QStringLiteral("videoCodec"), item.videoCodec);
    map.insert(QStringLiteral("audioCodec"), item.audioCodec);
    return map;
}

bool MediaLibraryService::reportRemoteProgress(const QString &path, double positionSeconds,
                                               bool stopped)
{
    if (!m_server || path.isEmpty() || positionSeconds <= 0.0)
        return false;
    for (const MediaItem &item : m_serverItems) {
        if (item.streamUrl.toString() != path)
            continue;
        QString &sessionId = m_playSessions[path];
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("palune-%1-%2")
                            .arg(item.id)
                            .arg(QDateTime::currentMSecsSinceEpoch());
        }
        UserState state;
        state.itemId = item.id;
        state.positionSeconds = positionSeconds;
        state.played = stopped;
        const auto result = m_server->reportProgress(sessionId, item, state);
        if (stopped)
            m_playSessions.remove(path);
        if (result.ok) {
            Log::info(QStringLiteral("播放进度回写：%1s（%2）")
                          .arg(positionSeconds, 0, 'f', 1)
                          .arg(stopped ? QStringLiteral("stop") : QStringLiteral("progress")));
        } else {
            Log::warn(QStringLiteral("播放进度回写失败：%1").arg(result.error));
        }
        return result.ok;
    }
    if (m_serverItems.isEmpty())
        return false; // 还在拉库或已在退出流程中拆除
    Log::warn(QStringLiteral("播放进度回写：未匹配到服务器条目 %1").arg(path));
    return false;
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
    if (m_server)
        refreshServer();
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

void MediaLibraryService::clearThumbCache()
{
    clearDirectory(thumbDir());
    // 索引里的 poster 指向已删除的文件，先清空，避免 QML 继续显示失效图片。
    for (QVariant &entry : m_items) {
        QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("remote")).toBool())
            continue;
        item.insert(QStringLiteral("poster"), QString());
        entry = item;
    }
    emit itemsChanged();
    setScanStatus(QStringLiteral("已清理缩略图缓存（可重建）"));
    Log::info(QStringLiteral("已清理缩略图缓存：%1").arg(thumbDir()));
    if (!m_roots.isEmpty())
        scanInBackground();
}

void MediaLibraryService::rebuildLibraryIndex()
{
    QFile::remove(indexFile());
    clearDirectory(thumbDir());
    m_localItems.clear();
    rebuildItems();
    emit libraryChanged();
    setScanStatus(QStringLiteral("索引已删除，正在重新扫描…"));
    Log::info(QStringLiteral("媒体库索引已重建：%1").arg(indexFile()));
    if (!m_roots.isEmpty())
        scanInBackground();
}

void MediaLibraryService::trimThumbCache()
{
    const int limitMb = m_settings
        ? qBound(100, m_settings->value(QStringLiteral("storage.thumbCacheLimitMb"), 2000).toInt(), 20000)
        : 2000;
    const int pruned = pruneThumbDir(thumbDir(), static_cast<qint64>(limitMb) * 1024 * 1024);
    setScanStatus(pruned > 0
        ? QStringLiteral("已按上限 %1 MB 清理 %2 个最旧缩略图").arg(limitMb).arg(pruned)
        : QStringLiteral("缩略图缓存未超过上限 %1 MB").arg(limitMb));
}

void MediaLibraryService::openLibraryDir()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::configDir() + QStringLiteral("/library")));
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
    // 缩略图缓存上限（MB）在扫描结束时按「最旧优先」淘汰；默认 2000 MB。
    const int limitMb = m_settings
        ? qBound(100, m_settings->value(QStringLiteral("storage.thumbCacheLimitMb"), 2000).toInt(), 20000)
        : 2000;
    const qint64 limitBytes = static_cast<qint64>(limitMb) * 1024 * 1024;

    auto task = new FunctionTask([this, roots, thumbs, limitBytes]() {
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

        const int pruned = pruneThumbDir(thumbs, limitBytes);
        if (pruned > 0)
            Log::info(QStringLiteral("缩略图缓存超过上限（%1 MB），已清理 %2 个最旧文件")
                          .arg(static_cast<qint64>(limitBytes) / (1024 * 1024)).arg(pruned));

        QMetaObject::invokeMethod(
            this,
            [this, scanned, status]() {
                m_localItems = scanned;
                persistIndex(scanned);
                rebuildItems();
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
