#include "ui/PlayQueue.h"

#include "diag/Log.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace palune {

namespace {

QString normalizePath(const QVariant &value)
{
    if (value.canConvert<QUrl>()) {
        const QUrl url = value.toUrl();
        if (url.isValid() && !url.scheme().isEmpty())
            return url.isLocalFile() ? url.toLocalFile() : url.toString();
    }
    return value.toString();
}

QString displayName(const QString &path)
{
    if (path.contains(QStringLiteral("://"))) {
        const QUrl url(path);
        const QString name = QFileInfo(url.path()).fileName();
        return name.isEmpty() ? path : name;
    }
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

QVariantMap makeItem(const QString &path)
{
    return QVariantMap{{QStringLiteral("path"), path},
                       {QStringLiteral("name"), displayName(path)}};
}

} // namespace

PlayQueue::PlayQueue(QObject *parent)
    : QObject(parent)
{
}

void PlayQueue::setPersistPath(const QString &path)
{
    m_persistPath = path;
}

void PlayQueue::notifyChanged()
{
    autoSave();
    emit changed();
}

int PlayQueue::appendPath(const QString &path)
{
    return append(QVariantList{path});
}

int PlayQueue::append(const QVariantList &urls)
{
    int first = -1;
    for (const QVariant &value : urls) {
        const QString path = normalizePath(value).trimmed();
        if (path.isEmpty())
            continue;
        const bool exists = std::any_of(m_items.cbegin(), m_items.cend(),
                                        [&path](const QVariant &item) {
                                            return item.toMap().value(QStringLiteral("path")).toString()
                                                == path;
                                        });
        if (exists)
            continue;
        if (first < 0)
            first = m_items.size();
        m_items.append(makeItem(path));
    }
    if (first >= 0)
        notifyChanged();
    return first;
}

void PlayQueue::setUrls(const QVariantList &urls, int startIndex)
{
    m_items.clear();
    for (const QVariant &value : urls) {
        const QString path = normalizePath(value).trimmed();
        if (!path.isEmpty())
            m_items.append(makeItem(path));
    }
    m_current = m_items.isEmpty() ? -1 : qBound(0, startIndex, int(m_items.size()) - 1);
    notifyChanged();
}

void PlayQueue::removeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_items.removeAt(index);
    if (m_items.isEmpty()) {
        m_current = -1;
    } else if (index < m_current) {
        --m_current;
    } else if (index == m_current) {
        m_current = qMin(m_current, int(m_items.size()) - 1);
    }
    notifyChanged();
}

void PlayQueue::moveUp(int index)
{
    if (index <= 0 || index >= m_items.size())
        return;
    m_items.swapItemsAt(index, index - 1);
    if (m_current == index)
        --m_current;
    else if (m_current == index - 1)
        ++m_current;
    notifyChanged();
}

void PlayQueue::moveDown(int index)
{
    if (index < 0 || index + 1 >= m_items.size())
        return;
    m_items.swapItemsAt(index, index + 1);
    if (m_current == index)
        ++m_current;
    else if (m_current == index + 1)
        --m_current;
    notifyChanged();
}

void PlayQueue::sortByName()
{
    if (m_items.size() < 2)
        return;
    const QString currentPath = (m_current >= 0 && m_current < m_items.size())
        ? m_items.at(m_current).toMap().value(QStringLiteral("path")).toString()
        : QString();
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(m_items.begin(), m_items.end(),
                     [&collator](const QVariant &a, const QVariant &b) {
                         return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                                                 b.toMap().value(QStringLiteral("name")).toString())
                             < 0;
                     });
    m_current = -1;
    if (!currentPath.isEmpty()) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).toMap().value(QStringLiteral("path")).toString() == currentPath) {
                m_current = i;
                break;
            }
        }
    }
    notifyChanged();
}

void PlayQueue::clear()
{
    if (m_items.isEmpty() && m_current < 0)
        return;
    m_items.clear();
    m_current = -1;
    notifyChanged();
}

void PlayQueue::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size() || index == m_current)
        return;
    m_current = index;
    notifyChanged();
    emit playRequested(index);
}

bool PlayQueue::save(const QString &path, QString *error) const
{
    QJsonArray items;
    for (const QVariant &value : m_items) {
        const QVariantMap item = value.toMap();
        items.append(QJsonObject{{QStringLiteral("path"),
                                  item.value(QStringLiteral("path")).toString()}});
    }
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("current"), m_current},
                           {QStringLiteral("items"), items}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool PlayQueue::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return false;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("队列文件格式无效");
        return false;
    }
    const QJsonObject root = doc.object();
    QVariantList items;
    for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray()) {
        const QString itemPath = value.toObject().value(QStringLiteral("path")).toString();
        if (!itemPath.isEmpty())
            items.append(makeItem(itemPath));
    }
    m_items = items;
    const int current = root.value(QStringLiteral("current")).toInt(-1);
    m_current = m_items.isEmpty() ? -1 : qBound(-1, current, int(m_items.size()) - 1);
    emit changed();
    return true;
}

bool PlayQueue::saveTo(const QString &path)
{
    QString error;
    if (!save(path, &error)) {
        Log::warn(QStringLiteral("队列保存失败：%1").arg(error));
        return false;
    }
    return true;
}

bool PlayQueue::loadFrom(const QString &path)
{
    QString error;
    if (!load(path, &error)) {
        Log::warn(QStringLiteral("队列载入失败：%1").arg(error));
        return false;
    }
    return true;
}

void PlayQueue::autoSave()
{
    if (m_persistPath.isEmpty())
        return;
    const QFileInfo info(m_persistPath);
    if (!info.dir().exists())
        QDir().mkpath(info.absolutePath());
    QString error;
    if (!save(m_persistPath, &error))
        Log::warn(QStringLiteral("队列自动保存失败：%1").arg(error));
}

} // namespace palune
