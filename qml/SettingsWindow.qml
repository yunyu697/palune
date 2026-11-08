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

    // 拖动热区：只覆盖标题行（右侧让出关闭按钮），命中后交给系统移动循环。
    // 根因：设置窗是 FramelessWindowHint + 无 WS_THICKFRAME 的顶层 Tool 窗，
    // 没有系统标题栏可拖；QML 卡片内容又铺满整窗，鼠标事件全被内容吃掉。
    // 设计决定：设置窗是固定尺寸对话框，双击标题不做最大化（会破坏跟随主窗的锚点规则）。
    Item {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Math.min(56, root.height / 4)
        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 112
            acceptedButtons: Qt.LeftButton
            onPressed: settingsWindow.startMove()
        }
    }

    Keys.onEscapePressed: settingsWindow.requestClose()
}
