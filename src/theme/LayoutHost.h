#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>

#include <vector>

class QQmlEngine;

namespace palune {

class ThemeRegistry;

// L3：布局宿主。只负责创建/销毁当前主题的布局子树，绝不触碰视频窗、
// 播放器或 QML 引擎的根上下文，因此切换主题不会触发 loadfile/end-file。
class LayoutHost : public QObject
{
    Q_OBJECT
public:
    LayoutHost(QQmlEngine *engine, ThemeRegistry *registry, QObject *parent = nullptr);
    ~LayoutHost() override;

    void setHostItem(QQuickItem *slot);
    QQuickItem *currentLayout() const { return m_layout; }
    QQuickItem *hostItem() const { return m_slot; }
    QString lastError() const { return m_error; }
    double lastBuildMs() const { return m_lastBuildMs; }
    int buildCount() const { return m_buildCount; }

    // 判据辅助：布局子树中不允许出现视频承载组件（VideoHost 必须在布局树外）。
    bool layoutContainsVideoHost() const;

    bool switchTo(const QString &theme);

signals:
    void layoutBuilt(const QString &theme, double elapsedMs);
    void layoutFailed(const QString &theme, const QString &error);

private:
    void rebuild();
    void syncSize();
    void clearLayout();
    QQuickItem *createLayout(const QString &theme, QString *error);

    QQmlEngine *m_engine = nullptr;
    ThemeRegistry *m_registry = nullptr;
    QPointer<QQuickItem> m_slot;
    QPointer<QQuickItem> m_layout;
    QString m_error;
    double m_lastBuildMs = 0.0;
    int m_buildCount = 0;
    std::vector<QMetaObject::Connection> m_slotConnections;
};

} // namespace palune
