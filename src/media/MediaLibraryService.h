#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <memory>

#include "media/MediaServerAdapter.h"

namespace palune {

class Settings;

// 本地媒体库（M4）：扫描本地目录、持久化索引、生成缩略图，
// 为 QML 提供海报墙与列表/表格两种形态的数据。
// 扫描与缩略图在工作线程执行，主线程只做结果替换，不阻塞播放。
class MediaLibraryService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY libraryChanged)
    Q_PROPERTY(bool paired READ paired NOTIFY libraryChanged)
    Q_PROPERTY(QString emptyTitle READ emptyTitle CONSTANT)
    Q_PROPERTY(QString emptyHint READ emptyHint CONSTANT)
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY libraryChanged)
    Q_PROPERTY(int adapterCount READ adapterCount NOTIFY adaptersChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemsChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QStringList roots READ roots NOTIFY rootsChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString indexFile READ indexFile CONSTANT)
    Q_PROPERTY(QString scanStatus READ scanStatus NOTIFY scanStatusChanged)
    Q_PROPERTY(QString serverStatus READ serverStatus NOTIFY serverStatusChanged)
    Q_PROPERTY(QString serverKind READ serverKind NOTIFY serverStatusChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverStatusChanged)
    Q_PROPERTY(QString serverUser READ serverUser NOTIFY serverStatusChanged)
    Q_PROPERTY(QString serverAccount READ serverAccount NOTIFY serverStatusChanged)
    Q_PROPERTY(QString serverAuthMode READ serverAuthMode NOTIFY serverStatusChanged)
    Q_PROPERTY(bool serverConnected READ serverConnected NOTIFY serverStatusChanged)
    Q_PROPERTY(bool serverFailed READ serverFailed NOTIFY serverStatusChanged)

public:
    explicit MediaLibraryService(QObject *parent = nullptr);

    void setSettings(Settings *settings);
    void load();

    void registerAdapter(std::shared_ptr<MediaServerAdapter> adapter);
    void unregisterAdapter(const QString &id);
    QList<std::shared_ptr<MediaServerAdapter>> adapters() const { return m_adapters; }

    bool available() const { return !m_items.isEmpty() || !m_roots.isEmpty(); }
    bool paired() const { return m_paired; }
    QString emptyTitle() const { return QCoreApplication::translate("Palune", "媒体库还是空的"); }
    QString emptyHint() const
    {
        return QCoreApplication::translate(
            "Palune", "添加本地视频目录后会自动扫描并生成缩略图；支持 MP4 / MKV / AVI / MOV / WEBM / TS 等格式。");
    }
    QVariantList libraries() const { return m_libraries; }
    int adapterCount() const { return static_cast<int>(m_adapters.size()); }

    QVariantList items() const { return m_items; }
    int itemCount() const { return static_cast<int>(m_items.size()); }
    bool scanning() const { return m_scanning; }
    QStringList roots() const { return m_roots; }
    QString viewMode() const { return m_viewMode; }
    QString indexFile() const;
    QString scanStatus() const { return m_scanStatus; }
    QString serverStatus() const { return m_serverStatus; }
    QString serverKind() const { return m_serverKind; }
    QString serverUrl() const { return m_serverUrl; }
    QString serverUser() const { return m_serverUser; }
    // 登录名（表单回填用）；serverUser 是 Jellyfin 的 userId，两者不能混用。
    QString serverAccount() const { return m_serverAccount; }
    QString serverAuthMode() const { return m_serverAuthMode; }
    bool serverConnected() const { return static_cast<bool>(m_server); }
    bool serverFailed() const { return m_serverFailed; }

    // 媒体服务器（Jellyfin 协议族）：认证成功后保存会话并拉取媒体库。
    // kind 取 jellyfin / emby / jellyfin-compat（极空间、飞牛）的稳定 id；
    // authMode 取 password（用户名+密码）或 apikey（Jellyfin 控制台生成的密钥）。
    bool connectServer(const QUrl &baseUrl, const QString &kind, const QString &user,
                       const QString &secret, const QString &authMode, QString *error = nullptr);
    void disconnectServer();

    // 设置页表单入口：测试不落盘；保存会写凭据存储并立即拉库；删除清空配置与会话。
    Q_INVOKABLE bool testServerConnection(const QString &kind, const QString &url,
                                          const QString &user, const QString &secret,
                                          const QString &authMode);
    Q_INVOKABLE bool saveServerConnection(const QString &kind, const QString &url,
                                          const QString &user, const QString &secret,
                                          const QString &authMode);
    Q_INVOKABLE void removeServerConnection();
    // 播放进度回写：path 为条目播放地址；stopped 为真时发送停止事件。
    Q_INVOKABLE bool reportRemoteProgress(const QString &path, double positionSeconds,
                                          bool stopped = false);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addRoot();
    Q_INVOKABLE void removeRoot(const QString &path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void setViewMode(const QString &mode);

    // 存储整组：缩略图缓存只清可重建内容（缩略图），索引重建会同时清空缩略图。
    // 上限来自 settings.toml 的 storage.thumbCacheLimitMb（默认 2000 MB）。
    Q_INVOKABLE void clearThumbCache();
    Q_INVOKABLE void rebuildLibraryIndex();
    Q_INVOKABLE void trimThumbCache();
    Q_INVOKABLE void openLibraryDir();

signals:
    void libraryChanged();
    void adaptersChanged();
    void itemsChanged();
    void scanningChanged();
    void rootsChanged();
    void viewModeChanged();
    void scanStatusChanged();
    void serverStatusChanged();

private:
    void scanInBackground();
    void persistIndex(const QVariantList &items) const;
    void setScanning(bool scanning);
    void setScanStatus(const QString &status);
    void setServerStatus(const QString &status);
    void setServerFailure(const QString &error);
    QString thumbDir() const;

    // 远端条目与本地索引合并；本地扫描完成时复用同一入口。
    void rebuildItems();
    void refreshServer();
    QVariantMap mapRemoteItem(const MediaItem &item) const;

    QList<std::shared_ptr<MediaServerAdapter>> m_adapters;
    QVariantList m_localItems;
    QVariantList m_remoteItems;
    QList<MediaItem> m_serverItems;
    QHash<QString, QString> m_playSessions;
    std::shared_ptr<MediaServerAdapter> m_server;
    QString m_serverStatus;
    QString m_serverKind;
    QString m_serverUrl;
    QString m_serverUser;
    QString m_serverAccount;
    QString m_serverAuthMode = QStringLiteral("password");
    bool m_serverFailed = false;
    QVariantList m_libraries;
    QVariantList m_items;
    QStringList m_roots;
    QString m_viewMode = QStringLiteral("poster");
    bool m_paired = false;
    bool m_scanning = false;
    QString m_scanStatus;
    Settings *m_settings = nullptr;
};

} // namespace palune
