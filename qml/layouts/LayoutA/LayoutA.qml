import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 A：经典播放器。顶部标题栏 + 底部控制栏。
Item {
    id: layoutA

    Tokens { id: tokens }

    EmptyState {
        visible: player.filePath === ""
        anchors.centerIn: parent
        onOpenRequested: win.action("openFile")
    }

    // 顶部栏
    Rectangle {
        id: topBar
        visible: uiState.chromeVisible && player.filePath !== ""
        height: tokens.topBarHeight
        anchors { top: parent.top; left: parent.left; right: parent.right }
        color: tokens.glass

        MouseArea {
            id: dragArea
            anchors.fill: parent
            property real lastX: 0
            property real lastY: 0
            onPressed: function (mouse) {
                lastX = mouse.x
                lastY = mouse.y
            }
            onPositionChanged: function (mouse) {
                win.moveBy(mouse.x - lastX, mouse.y - lastY)
            }
        }

        RowLayout {
            z: 1
            anchors.fill: parent
            anchors.leftMargin: tokens.spaceL
            anchors.rightMargin: tokens.spaceM
            spacing: tokens.spaceS

            Text {
                text: "PALUNE"
                color: tokens.accent
                font.pixelSize: tokens.fontS
                font.letterSpacing: 3
                font.weight: Font.DemiBold
            }
            Text {
                text: player.title === "" ? "未打开文件" : player.title
                color: tokens.text
                font.pixelSize: tokens.fontM
                elide: Text.ElideRight
                Layout.maximumWidth: 520
            }
            Item { Layout.fillWidth: true }

            PillButton { label: "打开"; onClicked: win.action("openFile") }
            PillButton { label: "文件夹"; onClicked: win.action("openFolder") }
            PillButton { label: "媒体库"; onClicked: win.action("openLibrary") }
            PillButton { label: "设置"; onClicked: win.action("openSettings") }
            PillButton {
                label: win.fullscreen ? "退出全屏" : "全屏"
                onClicked: win.action("toggleFullscreen")
            }
            PillButton { label: "关闭"; onClicked: win.action("exit") }
        }
    }

    // 底部控制栏
    Rectangle {
        id: bottomBar
        visible: uiState.chromeVisible
        height: tokens.transportHeight
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        color: tokens.glass

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            spacing: tokens.spaceS

            SeekBar {
                Layout.fillWidth: true
                position: player.position
                duration: player.duration
                onSeekRequested: function (value) { win.action("seekTo", value) }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                PillButton {
                    label: player.paused ? "播放" : "暂停"
                    highlighted: true
                    onClicked: win.action("playPause")
                }
                PillButton { label: "-10s"; onClicked: win.action("seekBy", -10) }
                PillButton { label: "+10s"; onClicked: win.action("seekBy", 10) }

                Text {
                    text: player.muted ? "已静音" : "音量"
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontS
                }
                VolumeBar {
                    value: player.muted ? 0 : player.volume
                    onVolumeSet: function (v) { win.action("volumeSet", v) }
                }
                PillButton {
                    label: player.muted ? "取消静音" : "静音"
                    onClicked: win.action("toggleMute")
                }

                Item { Layout.fillWidth: true }

                PillButton {
                    label: Math.round(player.speed * 100) / 100 + "x"
                    onClicked: win.action("speedCycle")
                }
                PillButton {
                    label: "音轨 " + player.audioTracks.length
                    onClicked: win.action("cycleAudio")
                }
                PillButton {
                    label: "字幕 " + player.subtitleTracks.length
                    onClicked: win.action("cycleSubtitle")
                }
                PillButton {
                    label: "切换布局"
                    onClicked: win.action("cycleTheme")
                }
            }
        }
    }
}
