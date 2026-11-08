import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 A：经典播放器（设计稿 3.A）。
// 顶部悬浮标题条 84px（品牌 / 居中标题 / 窗口按钮），底部双行控制栏 86px。
// 拖拽与边缘缩放由 Main.qml 的壳层统一接管，布局内不再自己算位移。
Item {
    id: layoutA

    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    readonly property bool showSecondary: width >= 900 && !layoutA.compact
    // 设计稿 §1.5：A 的空态隐藏标题条与控制条，只留空态卡。
    readonly property bool hasFile: player.filePath !== ""
    // 媒体库以 352px 滑出侧栏打开时，底部控制条右边界收至 368px（设计稿 §3.A）。
    readonly property bool libraryOpen: typeof uiState !== "undefined" && uiState.overlayOpen

    Tokens { id: tokens }

    EmptyState {
        visible: !layoutA.hasFile
        anchors.centerIn: parent
        onOpenRequested: win.action("openFile")
    }

    // 空态下标题条隐藏，但仍需要能关闭/最小化窗口（窗口按钮单独浮在右上）。
    WindowButtons {
        visible: !layoutA.hasFile && uiState.chromeVisible
        anchors { top: parent.top; right: parent.right; margins: 18 }
        compact: layoutA.compact
    }

    // 顶部悬浮标题条：设计稿 a-main 里没有实体底条，品牌/标题/窗控直接浮在画面上。
    Rectangle {
        id: topBar
        visible: uiState.chromeVisible && layoutA.hasFile
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: 10
        }
        height: tokens.topBarHeight
        radius: 16
        color: "transparent"
        border.width: 0

        Text {
            id: brand
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: "PALUNE"
            color: tokens.accent
            font.pixelSize: tokens.fontS
            font.letterSpacing: 3
            font.weight: Font.DemiBold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width - 320, 520)
            horizontalAlignment: Text.AlignHCenter
            text: player.title === "" ? qsTr("未打开文件") : player.title
            color: tokens.text
            font.pixelSize: 15
            elide: Text.ElideRight
        }

        WindowButtons {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            compact: layoutA.compact
        }
    }

    // 底部双行控制栏
    Rectangle {
        id: bottomBar
        visible: uiState.chromeVisible && layoutA.hasFile
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            topMargin: 0
            bottomMargin: 10
            leftMargin: 10
            rightMargin: layoutA.libraryOpen ? 368 : 10
        }
        height: layoutA.compact ? 64 : tokens.transportHeight
        radius: 16
        color: tokens.glass
        border.width: 1
        border.color: tokens.border

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 2

            SeekBar {
                Layout.fillWidth: true
                position: player.position
                duration: player.duration
                onSeekRequested: function (value) { win.action("seekTo", value) }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                PillButton {
                    label: player.paused ? qsTr("播放") : qsTr("暂停")
                    highlighted: true
                    actionId: "playPause"
                    onClicked: win.action("playPause")
                }
                PillButton { label: "-10s"; actionId: "seekBack10"; onClicked: win.action("seekBack10") }
                PillButton { label: "+10s"; actionId: "seekForward10"; onClicked: win.action("seekForward10") }

                VolumeBar {
                    visible: layoutA.showSecondary
                    Layout.preferredWidth: 110
                    Layout.alignment: Qt.AlignVCenter
                    value: player.muted ? 0 : player.volume
                    onVolumeSet: function (v) { win.action("volumeSet", v) }
                }
                PillButton {
                    label: player.muted ? qsTr("取消静音") : qsTr("静音")
                    actionId: "toggleMute"
                    onClicked: win.action("toggleMute")
                }

                Item { Layout.fillWidth: true }

                PillButton {
                    label: Math.round(player.speed * 100) / 100 + "x"
                    actionId: "speedUp"
                    onClicked: win.action("speedUp")
                }
                PillButton {
                    visible: layoutA.showSecondary
                    label: qsTr("音轨 ") + player.audioTracks.length
                    actionId: "cycleAudio"
                    onClicked: win.action("cycleAudio")
                }
                PillButton {
                    visible: layoutA.showSecondary
                    label: qsTr("字幕 ") + player.subtitleTracks.length
                    actionId: "cycleSubtitle"
                    onClicked: win.action("cycleSubtitle")
                }
                PillButton { visible: layoutA.showSecondary; label: qsTr("媒体库"); actionId: "openLibrary"; onClicked: win.action("openLibrary") }
                PillButton { label: qsTr("设置"); actionId: "openSettings"; onClicked: win.action("openSettings") }
                PillButton {
                    label: win.fullscreen ? qsTr("退出全屏") : qsTr("全屏")
                    actionId: "toggleFullscreen"
                    onClicked: win.action("toggleFullscreen")
                }
            }
        }
    }
}
