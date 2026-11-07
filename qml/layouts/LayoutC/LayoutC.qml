import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 C：紧凑影院。顶部细进度 + 底部悬浮胶囊。
Item {
    id: layoutC

    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact

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
            text: win.fullscreen ? qsTr("全屏") : qsTr("窗口")
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
        anchors.bottomMargin: layoutC.compact ? 12 : 26
        height: layoutC.compact ? 44 : tokens.capsuleHeight
        width: capsuleRow.implicitWidth + (layoutC.compact ? 20 : 36)
        radius: height / 2
        color: tokens.glass
        border.width: 1
        border.color: tokens.borderStrong

        RowLayout {
            id: capsuleRow
            anchors.centerIn: parent
            spacing: 10

            PillButton {
                label: player.paused ? qsTr("播放") : qsTr("暂停")
                highlighted: true
                actionId: "playPause"
                onClicked: win.action("playPause")
            }
            PillButton { label: "-10"; actionId: "seekBack10"; onClicked: win.action("seekBack10") }
            PillButton { label: "+10"; actionId: "seekForward10"; onClicked: win.action("seekForward10") }
            Text {
                visible: !layoutC.compact
                text: fmt(player.position) + " / " + fmt(player.duration)
                color: tokens.textMuted
                font.pixelSize: tokens.fontS
                font.family: "Consolas"
            }
            VolumeBar {
                visible: !layoutC.compact
                value: player.muted ? 0 : player.volume
                onVolumeSet: function (v) { win.action("volumeSet", v) }
            }
            PillButton {
                label: Math.round(player.speed * 100) / 100 + "x"
                actionId: "speedUp"
                onClicked: win.action("speedUp")
            }
            PillButton { visible: !layoutC.compact; label: qsTr("音轨"); actionId: "cycleAudio"; onClicked: win.action("cycleAudio") }
            PillButton { visible: !layoutC.compact; label: qsTr("字幕"); actionId: "cycleSubtitle"; onClicked: win.action("cycleSubtitle") }
            PillButton { label: qsTr("打开"); actionId: "openFile"; onClicked: win.action("openFile") }
            PillButton { label: qsTr("切换布局"); actionId: "cycleTheme"; onClicked: win.action("cycleTheme") }
            PillButton { label: qsTr("设置"); actionId: "openSettings"; onClicked: win.action("openSettings") }
            PillButton {
                label: win.fullscreen ? qsTr("还原") : qsTr("全屏")
                actionId: "toggleFullscreen"
                onClicked: win.action("toggleFullscreen")
            }
            PillButton { label: qsTr("关闭"); actionId: "exit"; onClicked: win.action("exit") }
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
