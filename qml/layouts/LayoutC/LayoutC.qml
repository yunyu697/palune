import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 C：紧凑影院（设计稿 3.C）。
// 窗口圆角 20（由壳层控制）；顶部细进度仅全屏显示；底部悬浮胶囊 62px 单行，
// 进度与时间都在胶囊内；左上标题胶囊、右上三枚圆形窗口按钮。
Item {
    id: layoutC

    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    readonly property bool hasFile: player.filePath !== ""

    Tokens { id: tokens }

    EmptyState {
        visible: !layoutC.hasFile
        anchors.centerIn: parent
        onOpenRequested: win.action("openFile")
    }

    // 全屏时才显示 2px 顶部进度条（设计稿 3.C）
    Item {
        id: topProgress
        visible: uiState.chromeVisible && win.fullscreen
        height: 2
        anchors { top: parent.top; left: parent.left; right: parent.right }
        Rectangle {
            anchors.fill: parent
            color: tokens.accent
            width: parent.width * (player.duration > 0 ? player.position / player.duration : 0)
        }
    }

    // 左上标题胶囊
    Rectangle {
        visible: uiState.chromeVisible && layoutC.hasFile
        anchors { top: parent.top; left: parent.left; margins: 16 }
        height: 36
        width: titleText.implicitWidth + 32
        radius: 18
        color: tokens.glass
        border.width: 1
        border.color: tokens.border

        Text {
            id: titleText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 360)
            text: player.title === "" ? "PALUNE" : player.title
            color: tokens.text
            font.pixelSize: tokens.fontM
            elide: Text.ElideRight
        }
    }

    // 右上圆形窗口按钮
    WindowButtons {
        visible: uiState.chromeVisible
        anchors { top: parent.top; right: parent.right; margins: 16 }
        compact: layoutC.compact
    }

    // 底部悬浮胶囊
    Rectangle {
        id: capsule
        visible: uiState.chromeVisible && layoutC.hasFile
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: layoutC.compact ? 12 : 24
        height: layoutC.compact ? 44 : tokens.capsuleHeight
        width: Math.min(parent.width - (layoutC.compact ? 24 : 48),
                        layoutC.compact ? 720 : 980)
        radius: height / 2
        color: tokens.glass
        border.width: 1
        border.color: tokens.borderStrong

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 12
            spacing: 8

            PillButton {
                label: player.paused ? qsTr("播放") : qsTr("暂停")
                highlighted: true
                actionId: "playPause"
                onClicked: win.action("playPause")
            }
            PillButton { label: "-10"; actionId: "seekBack10"; onClicked: win.action("seekBack10") }
            PillButton { label: "+10"; actionId: "seekForward10"; onClicked: win.action("seekForward10") }

            SeekBar {
                Layout.fillWidth: true
                position: player.position
                duration: player.duration
                onSeekRequested: function (value) { win.action("seekTo", value) }
            }

            VolumeBar {
                visible: !layoutC.compact
                Layout.preferredWidth: 92
                value: player.muted ? 0 : player.volume
                onVolumeSet: function (v) { win.action("volumeSet", v) }
            }
            PillButton {
                visible: !layoutC.compact
                label: Math.round(player.speed * 100) / 100 + "x"
                actionId: "speedUp"
                onClicked: win.action("speedUp")
            }
            PillButton { visible: !layoutC.compact; label: qsTr("音轨"); actionId: "cycleAudio"; onClicked: win.action("cycleAudio") }
            PillButton { visible: !layoutC.compact; label: qsTr("字幕"); actionId: "cycleSubtitle"; onClicked: win.action("cycleSubtitle") }
            PillButton { label: qsTr("媒体库"); actionId: "openLibrary"; onClicked: win.action("openLibrary") }
            PillButton { label: qsTr("设置"); actionId: "openSettings"; onClicked: win.action("openSettings") }
            PillButton {
                label: win.fullscreen ? qsTr("还原") : qsTr("全屏")
                actionId: "toggleFullscreen"
                onClicked: win.action("toggleFullscreen")
            }
        }
    }
}
