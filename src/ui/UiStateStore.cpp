#include "ui/UiStateStore.h"

#include "diag/Log.h"
#include "settings/Settings.h"

namespace palune {

UiStateStore::UiStateStore(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (m_settings) {
        for (const QString &theme : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")})
            m_panelWidths.insert(theme, m_settings->panelWidth(theme));
    }
}

void UiStateStore::setScrollIndex(int index)
{
    const int clamped = qMax(0, index);
    if (m_scrollIndex == clamped)
        return;
    m_scrollIndex = clamped;
    emit scrollIndexChanged();
}

void UiStateStore::setSelectedItemId(const QString &id)
{
    if (m_selectedItemId == id)
        return;
    m_selectedItemId = id;
    emit selectedItemIdChanged();
}

void UiStateStore::setFocusActionId(const QString &id)
{
    if (m_focusActionId == id)
        return;
    m_focusActionId = id;
    emit focusActionIdChanged();
}

void UiStateStore::setChromeVisible(bool visible)
{
    if (m_chromeVisible == visible)
        return;
    m_chromeVisible = visible;
    emit chromeVisibleChanged();
}

void UiStateStore::setOverlayOpen(bool open)
{
    if (m_overlayOpen == open)
        return;
    m_overlayOpen = open;
    emit overlayOpenChanged();
}

int UiStateStore::panelWidth(const QString &themeId) const
{
    const int width = m_panelWidths.value(themeId).toInt();
    return width > 0 ? width : 320;
}

void UiStateStore::setPanelWidth(const QString &themeId, int width)
{
    const int clamped = qBound(160, width, 720);
    if (panelWidth(themeId) == clamped && m_panelWidths.contains(themeId))
        return;
    m_panelWidths.insert(themeId, clamped);
    if (m_settings) {
        m_settings->setPanelWidth(themeId, clamped);
        QString error;
        if (!m_settings->save(&error))
            Log::warn(QStringLiteral("面板宽度持久化失败：%1").arg(error));
    }
    emit panelWidthsChanged();
}

} // namespace palune
