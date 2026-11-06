import QtQuick
import QtQuick.Layouts
import "../tokens"
import "../components"

// 媒体库空状态（M1 只有空状态页）。
Item {
    id: emptyLibrary

    Tokens { id: tokens }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 80)
        spacing: tokens.spaceM

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: library.emptyTitle
            color: tokens.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: library.emptyHint
            color: tokens.textMuted
            font.pixelSize: tokens.fontM
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "已注册内容源：" + library.adapterCount + " · 可用库：" + library.libraries.length
            color: tokens.textFaint
            font.pixelSize: tokens.fontS
        }
        PillButton {
            Layout.alignment: Qt.AlignHCenter
            label: "刷新"
            highlighted: true
            onClicked: library.refresh()
        }
    }
}
