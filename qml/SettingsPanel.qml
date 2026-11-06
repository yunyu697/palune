import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: panelRoot
    signal closeRequested()

    // 截图模式：不叠在视频上，使用纯色背景。
    property bool shotMode: false

    readonly property string accent: "#e7b27c"
    readonly property string textColor: "#e8eaee"
    readonly property string mutedColor: "#9aa0a6"
    readonly property string lineColor: "#26ffffff"

    Rectangle {
        anchors.fill: parent
        color: panelRoot.shotMode ? "#0b0c10" : "#99000000"
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(1000, parent.width - 72)
        height: Math.min(660, parent.height - 72)
        radius: 18
        color: "#f20d0e11"
        border.width: 1
        border.color: panelRoot.lineColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            RowLayout {
                Layout.fillWidth: true

                Column {
                    spacing: 4
                    Text {
                        text: "设置"
                        color: panelRoot.textColor
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Palune " + updater.currentVersion + " · M0"
                        color: panelRoot.mutedColor
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    id: closeBtn
                    implicitWidth: 64
                    implicitHeight: 30
                    radius: 9
                    color: closeHover.containsMouse ? "#22ffffff" : "#10ffffff"
                    Text {
                        anchors.centerIn: parent
                        text: "关闭"
                        color: panelRoot.textColor
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panelRoot.closeRequested()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: panelRoot.lineColor
            }

            // 关于
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text {
                    text: "关于"
                    color: panelRoot.textColor
                    font.pixelSize: 14
                }
                Text {
                    text: "Qt " + Qt.application.version + " · libmpv 动态链接 · 渲染路径：" + Qt.platform.os
                    color: panelRoot.mutedColor
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: panelRoot.lineColor
            }

            // 更新模块
            Text {
                text: "更新"
                color: panelRoot.textColor
                font.pixelSize: 14
            }

            UpdateSection {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
}
