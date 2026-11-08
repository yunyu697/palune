import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 B：专业工作台（设计稿 3.B）。
// 顶部扁平栏 52px（品牌 / 标签页 / 文件名 / 窗口按钮）、右侧检查器 344px、
// 底部技术信息条 34px + 控制条 82px。拖拽与缩放由壳层统一接管。
Item {
    id: layoutB

    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    // 拖动中的临时宽度；松手后写入 UiStateStore（按主题独立持久化）。
    property real dragWidth: 0
    readonly property int storedWidth: typeof uiState !== "undefined" ? uiState.panelWidth("B") : 344
    readonly property int dockWidth: layoutB.compact ? 224 : (dragWidth > 0 ? dragWidth : storedWidth)
    readonly property bool shortHeight: layoutB.height < 360
    readonly property bool hasFile: player.filePath !== ""
    property string inspectorTab: "media"

    readonly property int topBarHeight: 52
    readonly property int techBarHeight: layoutB.compact ? 0 : 34
    readonly property int controlBarHeight: layoutB.compact ? 64 : 82

    Tokens { id: tokens }

    EmptyState {
        visible: player.filePath === ""
        anchors.centerIn: parent
        onOpenRequested: win.action("openFile")
    }

    // 右列不透明底：B 的视频窗按检查器宽度向右内缩（dockWidth + 12），检查器上下留白
    // 与底部的右列区域没有视频；而 topBar/techBar/controlBar 都是半透明玻璃，
    // 桌面/上一个窗口会直接透上来（实机表现为「右侧变透明」）。
    // 先在整个右列铺一层不透明底，宽度与 WindowController::setVideoInsets 的右内缩完全一致。
    Rectangle {
        id: sideBackdrop
        visible: uiState.chromeVisible
        anchors { top: parent.top; right: parent.right; bottom: parent.bottom }
        width: layoutB.dockWidth + 12
        color: "#ff0c0e12"
    }

    // 顶部扁平栏
    Rectangle {
        id: topBar
        visible: uiState.chromeVisible
        height: layoutB.topBarHeight
        anchors { top: parent.top; left: parent.left; right: parent.right }
        color: tokens.glass

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 12
            spacing: 8

            Text {
                text: "PALUNE"
                color: tokens.accent
                font.pixelSize: tokens.fontS
                font.letterSpacing: 3
                font.weight: Font.DemiBold
            }
            Rectangle { width: 1; height: 20; color: tokens.border }

            PillButton {
                label: qsTr("媒体")
                highlighted: layoutB.inspectorTab === "media"
                actionId: "openFile"
                onClicked: layoutB.inspectorTab = "media"
            }
            PillButton {
                label: qsTr("章节")
                highlighted: layoutB.inspectorTab === "chapters"
                onClicked: layoutB.inspectorTab = "chapters"
            }
            PillButton {
                label: qsTr("队列")
                highlighted: layoutB.inspectorTab === "queue"
                onClicked: layoutB.inspectorTab = "queue"
            }
            PillButton {
                visible: !layoutB.compact
                label: qsTr("媒体库")
                actionId: "openLibrary"
                onClicked: win.action("openLibrary")
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                horizontalAlignment: Text.AlignLeft
                text: player.title === "" ? qsTr("未打开文件") : player.title
                color: tokens.text
                font.pixelSize: tokens.fontM
                elide: Text.ElideMiddle
            }

            PillButton {
                visible: !layoutB.compact
                label: qsTr("设置")
                actionId: "openSettings"
                onClicked: win.action("openSettings")
            }
            PillButton {
                label: win.fullscreen ? qsTr("退出全屏") : qsTr("全屏")
                actionId: "toggleFullscreen"
                onClicked: win.action("toggleFullscreen")
            }
            WindowButtons {
                Layout.alignment: Qt.AlignVCenter
                compact: layoutB.compact
            }
        }
    }

    // 右侧检查器
    // 右侧检查器：设计稿里是浮在视频之上的独立卡片（四周留 12px，圆角 8），
    // 不是贴死窗口右缘/底缘的实心栏。
    Rectangle {
        id: dock
        objectName: "dockB"
        visible: uiState.chromeVisible
        width: layoutB.dockWidth
        radius: tokens.radiusS
        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            rightMargin: 12
            topMargin: layoutB.topBarHeight + 8
            bottomMargin: layoutB.techBarHeight + layoutB.controlBarHeight + 16
        }
        // 不透明底：alpha 0xf2 会让视频透上来，实机表现为「点击后右栏变透明」。
        color: "#ff0c0e12"
        border.width: 1
        border.color: tokens.border
        // 显式压在控制条之上、且留 16px 安全间距，避免任何状态（主题切换/紧凑切换）
        // 下与控制条重叠（alpha10 问题 4）。
        z: 2

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: tokens.spaceM
            spacing: tokens.spaceS

            // 媒体页
            ColumnLayout {
                visible: layoutB.inspectorTab === "media"
                Layout.fillWidth: true
                spacing: tokens.spaceS

                Text {
                    text: qsTr("媒体信息")
                    color: tokens.accent
                    font.pixelSize: tokens.fontS
                    font.letterSpacing: 2
                }
                Text {
                    Layout.fillWidth: true
                    text: layoutB.hasFile ? player.title : "—"
                    color: tokens.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    text: layoutB.hasFile ? player.filePath : ""
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontS
                    elide: Text.ElideMiddle
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: tokens.border }
                Text {
                    text: layoutB.hasFile ? qsTr("时长 ") + fmt(player.duration) : qsTr("时长 —")
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontS
                }
                Text {
                    text: layoutB.hasFile
                          ? qsTr("音轨 ") + player.audioTracks.length + qsTr(" · 字幕 ") + player.subtitleTracks.length
                          : qsTr("音轨 — · 字幕 —")
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontS
                }
                Text {
                    text: layoutB.hasFile
                          ? qsTr("解码 ") + layoutB.codecShort(player.videoCodec)
                          : qsTr("解码 —")
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontS
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: layoutB.hasFile
                          ? qsTr("速度 ") + (Math.round(player.speed * 100) / 100) + "x"
                          : qsTr("速度 —")
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontS
                }
                Item { Layout.fillHeight: true }
            }

            // 章节页
            ColumnLayout {
                visible: layoutB.inspectorTab === "chapters"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: tokens.spaceS
                Text {
                    text: qsTr("章节")
                    color: tokens.accent
                    font.pixelSize: tokens.fontS
                    font.letterSpacing: 2
                }
                Text {
                    text: qsTr("当前文件没有章节信息")
                    color: tokens.textFaint
                    font.pixelSize: tokens.fontS
                }
                Item { Layout.fillHeight: true }
            }

            // 队列页：最近打开
            ColumnLayout {
                visible: layoutB.inspectorTab === "queue"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: tokens.spaceS
                Text {
                    text: qsTr("最近打开")
                    color: tokens.accent
                    font.pixelSize: tokens.fontS
                    font.letterSpacing: 2
                }
                ListView {
                    id: recentList
                    objectName: "recentListB"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.recentFiles
                    spacing: 2
                    currentIndex: typeof uiState !== "undefined" ? uiState.scrollIndex : -1
                    onCurrentIndexChanged: {
                        if (typeof uiState !== "undefined" && currentIndex >= 0
                                && recentList.count > 0)
                            uiState.scrollIndex = currentIndex
                    }

                    delegate: Rectangle {
                        id: recentRow
                        required property var modelData
                        required property int index
                        width: recentList.width
                        height: 30
                        radius: tokens.radiusS
                        readonly property bool selected: typeof uiState !== "undefined"
                                                          && uiState.selectedItemId === modelData.path
                        color: selected ? tokens.accentSoft
                                        : (recentHover.containsMouse ? "#22ffffff" : "transparent")

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.name
                            color: selected ? tokens.accent : tokens.text
                            font.pixelSize: tokens.fontS
                            elide: Text.ElideMiddle
                        }
                        MouseArea {
                            id: recentHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                recentList.currentIndex = recentRow.index
                                if (typeof uiState !== "undefined")
                                    uiState.selectedItemId = recentRow.modelData.path
                                win.action("openRecent", recentRow.modelData.path)
                            }
                        }
                    }
                }
            }
        }

        // 左边缘拖拽改检查器宽度（紧凑模式固定宽度）
        Rectangle {
            id: resizeHandle
            visible: !layoutB.compact
            width: 6
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            color: "transparent"

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real startMouseX: 0
                property int startWidth: 0
                property bool dragging: false
                onPressed: function (mouse) {
                    dragging = true
                    startWidth = dock.width
                    startMouseX = mapToItem(layoutB, mouse.x, 0).x
                }
                onPositionChanged: function (mouse) {
                    if (!dragging)
                        return
                    const globalX = mapToItem(layoutB, mouse.x, 0).x
                    layoutB.dragWidth = Math.max(240, Math.min(720,
                        startWidth + startMouseX - globalX))
                }
                onReleased: {
                    dragging = false
                    if (typeof uiState !== "undefined" && layoutB.dragWidth > 0)
                        uiState.setPanelWidth("B", layoutB.dragWidth)
                }
                onCanceled: dragging = false
            }
        }
    }

    // 底部技术信息条（设计稿 §4：字段映射 mpv 属性；空态隐藏）
    Rectangle {
        id: techBar
        visible: uiState.chromeVisible && !layoutB.compact && layoutB.hasFile
        height: layoutB.techBarHeight
        anchors {
            left: parent.left
            right: parent.right
            bottom: controlBar.top
        }
        color: "#cc0d0f14"
        border.width: 1
        border.color: tokens.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 18
            Text {
                text: qsTr("解码 ") + layoutB.codecShort(player.videoCodec)
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                elide: Text.ElideRight
                Layout.maximumWidth: 260
            }
            Text {
                text: qsTr("硬解 ") + layoutB.techValue(player.hwdec)
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                font.family: "Consolas"
            }
            Text {
                text: player.videoFps > 0 ? player.videoFps.toFixed(3) + " fps" : "—"
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                font.family: "Consolas"
            }
            Text {
                text: layoutB.bitrateText(player.videoBitrate)
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                font.family: "Consolas"
            }
            Text {
                text: qsTr("HDR ") + layoutB.techValue(player.hdrMode)
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
            }
            Text {
                text: qsTr("音轨 ") + player.audioTracks.length
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
            }
            Text {
                text: qsTr("字幕 ") + player.subtitleTracks.length
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("时长 ") + fmt(player.duration)
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                font.family: "Consolas"
            }
            Text {
                text: qsTr("工作台")
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                font.letterSpacing: 2
            }
        }
    }

    // 底部控制条
    Rectangle {
        id: controlBar
        visible: uiState.chromeVisible
        height: layoutB.controlBarHeight
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        color: tokens.glass

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: layoutB.compact ? 10 : 18
            anchors.rightMargin: layoutB.compact ? 10 : 18
            anchors.topMargin: layoutB.compact ? 6 : 10
            anchors.bottomMargin: layoutB.compact ? 6 : 10
            spacing: 8

            SeekBar {
                Layout.fillWidth: true
                position: player.position
                duration: player.duration
                onSeekRequested: function (value) { win.action("seekTo", value) }
            }
            PillButton {
                label: player.paused ? qsTr("播放") : qsTr("暂停")
                highlighted: true
                actionId: "playPause"
                onClicked: win.action("playPause")
            }
            PillButton { label: "-10s"; actionId: "seekBack10"; onClicked: win.action("seekBack10") }
            PillButton { label: "+10s"; actionId: "seekForward10"; onClicked: win.action("seekForward10") }
            VolumeBar {
                visible: !layoutB.compact
                Layout.preferredWidth: 90
                value: player.muted ? 0 : player.volume
                onVolumeSet: function (v) { win.action("volumeSet", v) }
            }
            PillButton {
                label: Math.round(player.speed * 100) / 100 + "x"
                actionId: "speedUp"
                onClicked: win.action("speedUp")
            }
            PillButton { visible: !layoutB.compact; label: qsTr("音轨"); actionId: "cycleAudio"; onClicked: win.action("cycleAudio") }
            PillButton { visible: !layoutB.compact; label: qsTr("字幕"); actionId: "cycleSubtitle"; onClicked: win.action("cycleSubtitle") }
            PillButton { label: qsTr("切换布局"); actionId: "cycleTheme"; onClicked: win.action("cycleTheme") }
        }
    }

    // 视频区让位给右侧常驻检查器；布局切换时新布局会重新下发自己的内缩值。
    function syncVideoInsets() {
        if (typeof win === "undefined")
            return
        win.setVideoInsets(0, 0, dock.visible ? dock.width + 12 : 0, 0)
    }

    Component.onCompleted: layoutB.syncVideoInsets()
    Component.onDestruction: if (typeof win !== "undefined") win.setVideoInsets(0, 0, 0, 0)

    Connections {
        target: dock
        function onWidthChanged() { layoutB.syncVideoInsets() }
        function onVisibleChanged() { layoutB.syncVideoInsets() }
    }

    function techValue(value) {
        return value && value !== "" ? value : "—"
    }

    // mpv 的 video-codec 会给出完整描述（H.264 / AVC / MPEG-4 AVC / ...），
    // 技术条只显示第一段，避免把其余字段挤出可视区。
    function codecShort(value) {
        if (!value || value === "")
            return "—"
        return String(value).split(" / ")[0]
    }

    function bitrateText(value) {
        if (!value || value <= 0)
            return "—"
        return (value / 1000000).toFixed(2) + " Mbps"
    }

    function fmt(seconds) {
        if (!seconds || seconds <= 0)
            return "00:00"
        var total = Math.floor(seconds)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : "" + v }
        return (h > 0 ? h + ":" : "") + pad(m) + ":" + pad(s)
    }
}
