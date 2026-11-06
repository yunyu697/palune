#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace palune {

class Settings;

// L3：UI 状态存储骨架（M2 完整实现）。M1 只保证：
// 切换主题不重置滚动位置/选中项/焦点 ActionId，面板宽度按主题独立持久化。
class UiStateStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int scrollIndex READ scrollIndex WRITE setScrollIndex NOTIFY scrollIndexChanged)
    Q_PROPERTY(QString selectedItemId READ selectedItemId WRITE setSelectedItemId NOTIFY selectedItemIdChanged)
    Q_PROPERTY(QString focusActionId READ focusActionId WRITE setFocusActionId NOTIFY focusActionIdChanged)
    Q_PROPERTY(QVariantMap panelWidths READ panelWidths NOTIFY panelWidthsChanged)
    Q_PROPERTY(bool chromeVisible READ chromeVisible WRITE setChromeVisible NOTIFY chromeVisibleChanged)
    Q_PROPERTY(bool overlayOpen READ overlayOpen WRITE setOverlayOpen NOTIFY overlayOpenChanged)

public:
    explicit UiStateStore(Settings *settings, QObject *parent = nullptr);

    int scrollIndex() const { return m_scrollIndex; }
    void setScrollIndex(int index);

    QString selectedItemId() const { return m_selectedItemId; }
    void setSelectedItemId(const QString &id);

    QString focusActionId() const { return m_focusActionId; }
    void setFocusActionId(const QString &id);

    QVariantMap panelWidths() const { return m_panelWidths; }

    bool chromeVisible() const { return m_chromeVisible; }
    void setChromeVisible(bool visible);
    bool overlayOpen() const { return m_overlayOpen; }
    void setOverlayOpen(bool open);

    Q_INVOKABLE int panelWidth(const QString &themeId) const;
    Q_INVOKABLE void setPanelWidth(const QString &themeId, int width);

signals:
    void scrollIndexChanged();
    void selectedItemIdChanged();
    void focusActionIdChanged();
    void panelWidthsChanged();
    void chromeVisibleChanged();
    void overlayOpenChanged();

private:
    Settings *m_settings = nullptr;
    int m_scrollIndex = 0;
    QString m_selectedItemId;
    QString m_focusActionId;
    QVariantMap m_panelWidths;
    bool m_chromeVisible = true;
    bool m_overlayOpen = false;
};

} // namespace palune
