import QtQuick
import "../tokens"

// L1：窗口控制按钮（最小化 / 最大化 / 关闭）。
// 无边框窗口必须始终保留这三个入口，A/B/C 三套布局共用同一实现。
Row {
    id: row
    spacing: 8

    property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    readonly property int size: row.compact ? 26 : 30

    Tokens { id: tokens }

    Rectangle {
        width: row.size
        height: row.size
        radius: width / 2
        color: minHover.containsMouse ? "#33ffffff" : "#1affffff"
        border.width: 1
        border.color: tokens.border
        Text {
            anchors.centerIn: parent
            text: "\u2014"
            color: tokens.text
            font.pixelSize: tokens.fontM
        }
        MouseArea {
            id: minHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: win.minimizeWindow()
        }
    }

    Rectangle {
        width: row.size
        height: row.size
        radius: width / 2
        color: maxHover.containsMouse ? "#33ffffff" : "#1affffff"
        border.width: 1
        border.color: tokens.border
        Text {
            anchors.centerIn: parent
            text: win.maximized ? "\u2750" : "\u25a1"
            color: tokens.text
            font.pixelSize: tokens.fontS
        }
        MouseArea {
            id: maxHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: win.toggleMaximize()
        }
    }

    Rectangle {
        width: row.size
        height: row.size
        radius: width / 2
        color: closeHover.containsMouse ? "#cc8a3b3b" : "#1affffff"
        border.width: 1
        border.color: closeHover.containsMouse ? "#cc8a3b3b" : tokens.border
        Text {
            anchors.centerIn: parent
            text: "\u2715"
            color: closeHover.containsMouse ? "#ffd9d9" : tokens.text
            font.pixelSize: tokens.fontS
        }
        MouseArea {
            id: closeHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: win.action("exit")
        }
    }
}
