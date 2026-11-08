#pragma once

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QStringList>
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

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addRoot();
    Q_INVOKABLE void removeRoot(const QString &path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void setViewMode(const QString &mode);

signals:
    void libraryChanged();
    void adaptersChanged();
    void itemsChanged();
    void scanningChanged();
    void rootsChanged();
    void viewModeChanged();
    void scanStatusChanged();

private:
    void scanInBackground();
    void persistIndex(const QVariantList &items) const;
    void setScanning(bool scanning);
    void setScanStatus(const QString &status);
    QString thumbDir() const;

    QList<std::shared_ptr<MediaServerAdapter>> m_adapters;
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
