#pragma once

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QVariantList>

#include <memory>

#include "media/MediaServerAdapter.h"

namespace palune {

// 内容源服务（M1 为空实现）：只维护适配器注册表与空库状态，不发网络请求。
// 未配对时返回空库、UI 显示空状态，且不阻塞启动。
class MediaLibraryService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY libraryChanged)
    Q_PROPERTY(bool paired READ paired NOTIFY libraryChanged)
    Q_PROPERTY(QString emptyTitle READ emptyTitle CONSTANT)
    Q_PROPERTY(QString emptyHint READ emptyHint CONSTANT)
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY libraryChanged)
    Q_PROPERTY(int adapterCount READ adapterCount NOTIFY adaptersChanged)

public:
    explicit MediaLibraryService(QObject *parent = nullptr);

    void registerAdapter(std::shared_ptr<MediaServerAdapter> adapter);
    void unregisterAdapter(const QString &id);
    QList<std::shared_ptr<MediaServerAdapter>> adapters() const { return m_adapters; }

    bool available() const { return !m_libraries.isEmpty(); }
    bool paired() const { return m_paired; }
    QString emptyTitle() const { return QCoreApplication::translate("Palune", "还没有媒体库"); }
    QString emptyHint() const
    {
        return QCoreApplication::translate("Palune", "M1 只预留内容源接口，未接入服务器。配对入口将在媒体库里程碑开放。");
    }
    QVariantList libraries() const { return m_libraries; }
    int adapterCount() const { return static_cast<int>(m_adapters.size()); }

    Q_INVOKABLE void refresh();

signals:
    void libraryChanged();
    void adaptersChanged();

private:
    QList<std::shared_ptr<MediaServerAdapter>> m_adapters;
    QVariantList m_libraries;
    bool m_paired = false;
};

} // namespace palune
