import QtQuick
import "../tokens"

// L1：窗口控制按钮（最小化 / 最大化 / 关闭）。
// 无边框窗口必须始终保留这三个入口，A/B/C 三套布局共用同一实现。
//
// 图标统一用几何图元绘制（不混用等宽字体里的 \u2014 / \u25a1 / \u2750 / \u2715，
// 那四个字符在不同字体下粗细、大小、基线都不一致）。三个按钮共用同一个
// 10×10 视觉画布、1px 描边，缩放时不会出现“最小化像短线、关闭像斜杠”的错位。
Row {
    id: row
    spacing: 8

    property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    readonly property int size: row.compact ? 26 : 30
    readonly property int iconSize: row.compact ? 9 : 10

    Tokens { id: tokens }

    // 最小化：一条 10×1 横线
    Rectangle {
        width: row.size
        height: row.size
        radius: width / 2
        color: minHover.containsMouse ? "#33ffffff" : "#1affffff"
        border.width: 1
        border.color: tokens.border
        Rectangle {
            anchors.centerIn: parent
            width: row.iconSize
            height: 1
            color: tokens.text
        }
        MouseArea {
            id: minHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: win.minimizeWindow()
        }
    }

    // 最大化 / 还原：1px 描边方框；最大化后画两个错位方框表示“还原”
    Rectangle {
        width: row.size
        height: row.size
        radius: width / 2
        color: maxHover.containsMouse ? "#33ffffff" : "#1affffff"
        border.width: 1
        border.color: tokens.border
        Item {
            anchors.centerIn: parent
            width: row.iconSize
            height: row.iconSize
            visible: !win.maximized
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: tokens.text
            }
        }
        Item {
            anchors.centerIn: parent
            width: row.iconSize
            height: row.iconSize
            visible: win.maximized
            Rectangle {
                x: 0
                y: 2
                width: row.iconSize - 2
                height: row.iconSize - 2
                color: "transparent"
                border.width: 1
                border.color: tokens.text
            }
            Rectangle {
                x: 2
                y: 0
                width: row.iconSize - 2
                height: row.iconSize - 2
                color: "transparent"
                border.width: 1
                border.color: tokens.text
            }
        }
        MouseArea {
            id: maxHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: win.toggleMaximize()
        }
    }

    // 关闭：两条等长线旋转 45°，与最小化横线同宽同粗
    Rectangle {
        width: row.size
        height: row.size
        radius: width / 2
        color: closeHover.containsMouse ? "#cc8a3b3b" : "#1affffff"
        border.width: 1
        border.color: closeHover.containsMouse ? "#cc8a3b3b" : tokens.border
        Item {
            anchors.centerIn: parent
            width: row.iconSize
            height: row.iconSize
            Rectangle {
                anchors.centerIn: parent
                width: row.iconSize
                height: 1
                rotation: 45
                color: closeHover.containsMouse ? "#ffd9d9" : tokens.text
            }
            Rectangle {
                anchors.centerIn: parent
                width: row.iconSize
                height: 1
                rotation: -45
                color: closeHover.containsMouse ? "#ffd9d9" : tokens.text
            }
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
