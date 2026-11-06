#include "theme/LayoutHost.h"

#include "diag/Log.h"
#include "theme/ThemeRegistry.h"

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

namespace palune {

LayoutHost::LayoutHost(QQmlEngine *engine, ThemeRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_registry(registry)
{
    if (m_registry)
        connect(m_registry, &ThemeRegistry::currentThemeChanged, this, &LayoutHost::rebuild);
}

LayoutHost::~LayoutHost()
{
    clearLayout();
}

void LayoutHost::setHostItem(QQuickItem *slot)
{
    if (m_slot == slot)
        return;
    for (auto &connection : m_slotConnections)
        disconnect(connection);
    m_slotConnections.clear();

    m_slot = slot;
    if (m_slot) {
        m_slotConnections.push_back(connect(m_slot, &QQuickItem::widthChanged, this, &LayoutHost::syncSize));
        m_slotConnections.push_back(connect(m_slot, &QQuickItem::heightChanged, this, &LayoutHost::syncSize));
    }
    rebuild();
}

void LayoutHost::clearLayout()
{
    if (m_layout) {
        m_layout->setParentItem(nullptr);
        delete m_layout;
        m_layout = nullptr;
    }
}

QQuickItem *LayoutHost::createLayout(const QString &theme, QString *error)
{
    if (!m_engine) {
        if (error) *error = QStringLiteral("QML 引擎不可用");
        return nullptr;
    }
    QQmlComponent component(m_engine, ThemeRegistry::layoutUrl(theme));
    if (component.isError()) {
        if (error)
            *error = component.errors().isEmpty() ? QStringLiteral("布局组件加载失败")
                                                  : component.errors().first().toString();
        return nullptr;
    }
    QObject *object = component.create();
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        if (error) *error = QStringLiteral("布局根节点不是 QQuickItem");
        delete object;
        return nullptr;
    }
    return item;
}

void LayoutHost::rebuild()
{
    if (!m_slot || !m_registry) {
        return;
    }
    const QString theme = m_registry->currentTheme();
    QElapsedTimer timer;
    timer.start();

    QString error;
    QQuickItem *layout = createLayout(theme, &error);
    if (!layout) {
        m_error = error;
        m_lastBuildMs = timer.nsecsElapsed() / 1.0e6;
        Log::error(QStringLiteral("布局 %1 构建失败：%2").arg(theme, error));
        emit layoutFailed(theme, error);
        return;
    }

    clearLayout();
    m_layout = layout;
    m_layout->setParentItem(m_slot);
    m_layout->setVisible(true);
    syncSize();
    m_lastBuildMs = timer.nsecsElapsed() / 1.0e6;
    ++m_buildCount;
    m_error.clear();
    Log::info(QStringLiteral("布局 %1 已构建，耗时 %2ms（第 %3 次）")
                  .arg(theme, QString::number(m_lastBuildMs, 'f', 2))
                  .arg(m_buildCount));
    emit layoutBuilt(theme, m_lastBuildMs);
}

void LayoutHost::syncSize()
{
    if (!m_layout || !m_slot)
        return;
    m_layout->setWidth(m_slot->width());
    m_layout->setHeight(m_slot->height());
}

bool LayoutHost::layoutContainsVideoHost() const
{
    if (!m_layout)
        return false;
    return !m_layout->findChildren<QQuickItem *>(QStringLiteral("videoHost"), Qt::FindChildrenRecursively).isEmpty();
}

bool LayoutHost::switchTo(const QString &theme)
{
    if (!m_registry)
        return false;
    const ThemeRegistry::SwitchResult result = m_registry->switchTo(theme);
    return result.ok;
}

} // namespace palune
