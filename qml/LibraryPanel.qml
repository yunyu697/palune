import QtQuick
import QtQuick.Layouts
import "tokens"
import "components"
import "library"

// 媒体库覆盖层（shell 层，不属于任何布局）。
// 形态跟随布局（设计稿 §1.3）：A = 右侧 352px 滑出侧栏；B = 居中宽面板（队列用全宽表格）；
// C = 右侧 76% 抽屉（覆盖画面而非推让）。
Item {
    id: libraryPanel
    signal closeRequested()

    Tokens { id: tokens }

    readonly property string mode: typeof themeRegistry !== "undefined"
                                   ? themeRegistry.currentTheme : "A"
    readonly property bool railMode: libraryPanel.mode === "A"
    readonly property bool drawerMode: libraryPanel.mode === "C"
    readonly property int railWidth: 352
    readonly property int drawerWidth: Math.round(parent.width * 0.76)

    // A 是滑出侧栏、不压暗画面；C 是覆盖式抽屉，压暗背景。
    Rectangle {
        anchors.fill: parent
        color: libraryPanel.railMode ? "transparent" : "#99000000"
        MouseArea {
            anchors.fill: parent
            onClicked: libraryPanel.closeRequested()
        }
    }

    Rectangle {
        id: card
        width: libraryPanel.railMode ? Math.min(libraryPanel.railWidth, parent.width - 20)
              : libraryPanel.drawerMode ? libraryPanel.drawerWidth
              : Math.min(1080, parent.width - 96)
        height: libraryPanel.railMode ? parent.height - 114
              : libraryPanel.drawerMode ? parent.height
              : Math.min(560, parent.height - 96)
        x: libraryPanel.railMode || libraryPanel.drawerMode ? parent.width - width - (libraryPanel.railMode ? 10 : 0)
                                                            : (parent.width - width) / 2
        y: libraryPanel.railMode ? 104 : (libraryPanel.drawerMode ? 0 : (parent.height - height) / 2)
        radius: libraryPanel.drawerMode ? 0 : tokens.radiusL
        color: "#f20d0e11"
        border.width: libraryPanel.drawerMode ? 0 : 1
        border.color: tokens.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: libraryPanel.railMode ? 16 : 24
            spacing: tokens.spaceM

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("媒体库")
                    color: tokens.text
                    font.pixelSize: libraryPanel.railMode ? 16 : tokens.fontL
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    label: qsTr("关闭")
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
