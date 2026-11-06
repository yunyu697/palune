import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 C：紧凑影院。顶部细进度 + 底部悬浮胶囊。
Item {
    id: layoutC

    Tokens { id: tokens }

    EmptyState {
        visible: player.filePath === ""
        anchors.centerIn: parent
        onOpenRequested: win.action("openFile")
    }

    // 顶部进度
    Item {
        id: topProgress
        visible: uiState.chromeVisible
        height: 30
        anchors { top: parent.top; left: parent.left; right: parent.right }

        Rectangle {
            anchors.fill: parent
            color: tokens.glassSoft
        }
        Rectangle {
            height: 2
            anchors.bottom: parent.bottom
            width: parent.width * (player.duration > 0 ? player.position / player.duration : 0)
            color: tokens.accent
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: (player.title === "" ? "PALUNE" : player.title)
            color: tokens.textMuted
            font.pixelSize: tokens.fontS
            elide: Text.ElideRight
            width: parent.width - 140
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: win.fullscreen ? "全屏" : "窗口"
            color: tokens.textFaint
            font.pixelSize: tokens.fontS
        }
    }

    // 底部胶囊
    Rectangle {
        id: capsule
        visible: uiState.chromeVisible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 26
        height: tokens.capsuleHeight
        width: capsuleRow.implicitWidth + 36
        radius: height / 2
        color: tokens.glass
        border.width: 1
        border.color: tokens.borderStrong

        RowLayout {
            id: capsuleRow
            anchors.centerIn: parent
            spacing: 10

            PillButton {
                label: player.paused ? "播放" : "暂停"
                highlighted: true
                onClicked: win.action("playPause")
            }
            PillButton { label: "-10"; onClicked: win.action("seekBy", -10) }
            PillButton { label: "+10"; onClicked: win.action("seekBy", 10) }
            Text {
                text: fmt(player.position) + " / " + fmt(player.duration)
                color: tokens.textMuted
                font.pixelSize: tokens.fontS
                font.family: "Consolas"
            }
            VolumeBar {
                value: player.muted ? 0 : player.volume
                onVolumeSet: function (v) { win.action("volumeSet", v) }
            }
            PillButton {
                label: Math.round(player.speed * 100) / 100 + "x"
                onClicked: win.action("speedCycle")
            }
            PillButton { label: "音轨"; onClicked: win.action("cycleAudio") }
            PillButton { label: "字幕"; onClicked: win.action("cycleSubtitle") }
            PillButton { label: "打开"; onClicked: win.action("openFile") }
            PillButton { label: "切换布局"; onClicked: win.action("cycleTheme") }
            PillButton { label: "设置"; onClicked: win.action("openSettings") }
            PillButton {
                label: win.fullscreen ? "还原" : "全屏"
                onClicked: win.action("toggleFullscreen")
            }
            PillButton { label: "关闭"; onClicked: win.action("exit") }
        }
    }

    function fmt(seconds) {
        if (!seconds || seconds <= 0)
            return "0:00"
        var total = Math.floor(seconds)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : "" + v }
        return (h > 0 ? h + ":" : "") + pad(m) + ":" + pad(s)
    }
}
