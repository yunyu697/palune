import QtQuick
import QtQuick.Layouts
import "tokens"
import "components"
import "library"

// 媒体库覆盖层（shell 层，不属于任何布局）。
Item {
    id: libraryPanel
    signal closeRequested()

    Tokens { id: tokens }

    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        MouseArea { anchors.fill: parent }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(880, parent.width - 96)
        height: Math.min(560, parent.height - 96)
        radius: tokens.radiusL
        color: "#f20d0e11"
        border.width: 1
        border.color: tokens.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: tokens.spaceM

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "媒体库"
                    color: tokens.text
                    font.pixelSize: tokens.fontL
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    label: "关闭"
                    onClicked: libraryPanel.closeRequested()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: tokens.border
            }

            MediaLibraryEmpty {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
}
