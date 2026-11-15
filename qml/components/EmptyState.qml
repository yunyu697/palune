import QtQuick
import "."
import "../tokens"

// L1：空状态卡片。
Rectangle {
    id: empty
    signal openRequested()

    width: tokens.emptyCardW
    height: tokens.emptyCardH
    radius: tokens.radiusL
    color: tokens.glass
    border.width: tokens.stroke
    border.color: tokens.border

    Tokens { id: tokens }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: empty.openRequested()
    }

    Column {
        anchors.centerIn: parent
        spacing: tokens.spaceM

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "PALUNE"
            color: tokens.accent
            font.pixelSize: tokens.fontL
            font.letterSpacing: tokens.trackingL
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
            spacing: tokens.spaceM
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
