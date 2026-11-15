#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

namespace palune {

// 播放队列：与「最近打开」分离的可编辑片单。
// - 条目为 {path, name}，path 可以是本地绝对路径或网络地址；
// - 任何变更都会自动写回 queue.json（persistPath），供下次启动恢复；
// - 播放请求通过 playRequested(index) 交给 WindowController，队列本身不直接碰播放器。
class PlayQueue : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit PlayQueue(QObject *parent = nullptr);

    QVariantList items() const { return m_items; }
    int currentIndex() const { return m_current; }
    bool empty() const { return m_items.isEmpty(); }
    int count() const { return m_items.size(); }

    void setPersistPath(const QString &path);
    QString persistPath() const { return m_persistPath; }

    // 用一组地址重建队列并请求播放 startIndex。
    Q_INVOKABLE void setUrls(const QVariantList &urls, int startIndex = 0);
    // 追加一组地址，返回追加后的首个索引；空列表返回 -1。
    Q_INVOKABLE int append(const QVariantList &urls);
    Q_INVOKABLE int appendPath(const QString &path);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void moveUp(int index);
    Q_INVOKABLE void moveDown(int index);
    Q_INVOKABLE void sortByName();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setCurrentIndex(int index);

    bool save(const QString &path, QString *error = nullptr) const;
    bool load(const QString &path, QString *error = nullptr);
    Q_INVOKABLE bool saveTo(const QString &path);
    Q_INVOKABLE bool loadFrom(const QString &path);

signals:
    void changed();
    void playRequested(int index);

private:
    void autoSave();
    void notifyChanged();

    QVariantList m_items;
    int m_current = -1;
    QString m_persistPath;
};

} // namespace palune
