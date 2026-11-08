import QtQuick
import "components"

// 设置窗根项：作为独立顶层窗口的内容，铺满自身窗口。
// 不再绘制压暗层（那是主窗内弹层的形态），保留主题圆角与外框由 SettingsPanel 的卡片承担。
Item {
    id: root
    focus: true

    property int panelRadius: typeof settingsPanelRadius !== "undefined" ? settingsPanelRadius : 18

    SettingsPanel {
        anchors.fill: parent
        windowMode: true
        panelRadius: root.panelRadius
        onCloseRequested: settingsWindow.requestClose()
    }

    Keys.onEscapePressed: settingsWindow.requestClose()
}
