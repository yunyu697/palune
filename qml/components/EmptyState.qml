import QtQuick
import "."
import "../tokens"

// L1：空状态卡片。
Rectangle {
    id: empty
    signal openRequested()

    width: 460
    height: 168
    radius: tokens.radiusL
    color: tokens.glass
    border.width: 1
    border.color: tokens.border

    Tokens { id: tokens }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: empty.openRequested()
    }

    Column {
        anchors.centerIn: parent
        spacing: 10

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "PALUNE"
            color: tokens.accent
            font.pixelSize: tokens.fontL
            font.letterSpacing: 4
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("将视频拖到这里，或点击下方按钮")
            color: tokens.textMuted
            font.pixelSize: tokens.fontM
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            PillButton {
                label: qsTr("打开文件")
                highlighted: true
                actionId: "openFile"
                onClicked: empty.openRequested()
            }
            PillButton {
                label: qsTr("设置")
                actionId: "openSettings"
                onClicked: win.action("openSettings")
            }
            PillButton {
                label: qsTr("媒体库")
                actionId: "openLibrary"
                onClicked: win.action("openLibrary")
            }
        }
    }
}
