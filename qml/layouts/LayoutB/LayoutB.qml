import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 B：工作台。左侧信息坞（含最近文件）+ 底部运输条。
Item {
    id: layoutB

    Tokens { id: tokens }

    EmptyState {
        visible: player.filePath === ""
        anchors.centerIn: parent
        onOpenRequested: win.action("openFile")
    }

    // 左上角品牌与窗口按钮
    Row {
        visible: uiState.chromeVisible
        z: 2
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: tokens.spaceM
        spacing: tokens.spaceS

        PillButton { label: "PALUNE · 工作台"; highlighted: true; onClicked: win.action("openLibrary") }
        PillButton { label: "打开"; onClicked: win.action("openFile") }
        PillButton { label: "文件夹"; onClicked: win.action("openFolder") }
    }

    Row {
        visible: uiState.chromeVisible
        z: 2
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: tokens.spaceM
        spacing: tokens.spaceS

        PillButton {
            label: win.fullscreen ? "退出全屏" : "全屏"
            onClicked: win.action("toggleFullscreen")
        }
        PillButton { label: "设置"; onClicked: win.action("openSettings") }
        PillButton { label: "关闭"; onClicked: win.action("exit") }
    }

    // 左坞
    Rectangle {
        id: dock
        visible: uiState.chromeVisible
        width: uiState.panelWidth("B")
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        color: tokens.glassSoft
        border.width: 1
        border.color: tokens.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: tokens.spaceM
            spacing: tokens.spaceS

            Text {
                text: "播放中"
                color: tokens.accent
                font.pixelSize: tokens.fontS
                font.letterSpacing: 2
            }
            Text {
                Layout.fillWidth: true
                text: player.title === "" ? "未打开文件" : player.title
                color: tokens.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: player.filePath
                color: tokens.textMuted
                font.pixelSize: tokens.fontS
                elide: Text.ElideMiddle
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: tokens.border }

            Text {
                text: "最近打开"
                color: tokens.textMuted
                font.pixelSize: tokens.fontS
            }

            ListView {
                id: recentList
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                clip: true
                model: win.recentFiles
                spacing: 2

                delegate: Rectangle {
                    required property var modelData
                    width: recentList.width
                    height: 30
                    radius: tokens.radiusS
                    color: recentHover.containsMouse ? "#22ffffff" : "transparent"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.name
                        color: tokens.text
                        font.pixelSize: tokens.fontS
                        elide: Text.ElideMiddle
                    }
                    MouseArea {
                        id: recentHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.action("openRecent", modelData.path)
                    }
                }
            }

            Item { Layout.fillHeight: true }

            Text {
                text: "音轨 " + player.audioTracks.length + " · 字幕 " + player.subtitleTracks.length
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: tokens.spaceS

                PillButton {
                    label: player.paused ? "播放" : "暂停"
                    highlighted: true
                    onClicked: win.action("playPause")
                }
                PillButton { label: "-10s"; onClicked: win.action("seekBy", -10) }
                PillButton { label: "+10s"; onClicked: win.action("seekBy", 10) }
                Item { Layout.fillWidth: true }
                PillButton {
                    label: Math.round(player.speed * 100) / 100 + "x"
                    onClicked: win.action("speedCycle")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: tokens.spaceS
                PillButton { label: "音轨"; onClicked: win.action("cycleAudio") }
                PillButton { label: "字幕"; onClicked: win.action("cycleSubtitle") }
                Item { Layout.fillWidth: true }
                PillButton { label: "切换布局"; onClicked: win.action("cycleTheme") }
            }
        }
    }

    // 底部运输条
    Rectangle {
        visible: uiState.chromeVisible
        height: tokens.transportHeight - 20
        anchors { left: dock.right; right: parent.right; bottom: parent.bottom }
        color: tokens.glass

        RowLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: tokens.spaceM

            SeekBar {
                Layout.fillWidth: true
                position: player.position
                duration: player.duration
                onSeekRequested: function (value) { win.action("seekTo", value) }
            }
        }
    }
}
