import QtQuick
import QtQuick.Layouts
import "../tokens"
import "../components"

// 媒体库空状态（M4：本地目录扫描）。
Item {
    id: emptyLibrary

    Tokens { id: tokens }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 40)
        spacing: tokens.spaceM

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: typeof library !== "undefined" ? library.emptyTitle : qsTr("媒体库还是空的")
            color: tokens.text
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: typeof library !== "undefined" ? library.emptyHint : ""
            color: tokens.textMuted
            font.pixelSize: tokens.fontM
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: tokens.spaceS
            PillButton {
                label: qsTr("添加目录")
                highlighted: true
                onClicked: library.addRoot()
            }
            PillButton {
                label: qsTr("重新扫描")
                enabled: typeof library !== "undefined" && !library.scanning
                onClicked: library.rescan()
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: typeof library !== "undefined" && library.scanStatus !== ""
            text: typeof library !== "undefined" ? library.scanStatus : ""
            color: tokens.textFaint
            font.pixelSize: tokens.fontS
        }
    }
}
