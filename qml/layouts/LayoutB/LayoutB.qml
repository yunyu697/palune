import QtQuick
import QtQuick.Layouts
import "../../tokens"
import "../../components"

// 布局 B：工作台。左侧信息坞（含最近文件）+ 底部运输条。
Item {
    id: layoutB

    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    // 拖动中的临时宽度；松手后写入 UiStateStore（按主题独立持久化）。
    property real dragWidth: 0
    readonly property int storedWidth: typeof uiState !== "undefined" ? uiState.panelWidth("B") : 320
    readonly property int dockWidth: layoutB.compact ? 220
        : (dragWidth > 0 ? dragWidth : storedWidth)
    // 极矮窗口（480×270 验收尺寸）：隐藏次要标签并压缩最近列表，
    // 保证左坞内容不越过窗口边界。
    readonly property bool shortHeight: layoutB.height < 360

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

        PillButton { label: "PALUNE · 工作台"; highlighted: true; actionId: "openLibrary"; onClicked: win.action("openLibrary") }
        PillButton { label: "打开"; actionId: "openFile"; onClicked: win.action("openFile") }
        PillButton { visible: !layoutB.compact; label: "文件夹"; actionId: "openFolder"; onClicked: win.action("openFolder") }
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
            actionId: "toggleFullscreen"
            onClicked: win.action("toggleFullscreen")
        }
        PillButton { label: "设置"; actionId: "openSettings"; onClicked: win.action("openSettings") }
        PillButton { label: "关闭"; actionId: "exit"; onClicked: win.action("exit") }
    }

    // 左坞
    Rectangle {
        id: dock
        objectName: "dockB"
        visible: uiState.chromeVisible
        width: layoutB.dockWidth
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        color: tokens.glassSoft
        border.width: 1
        border.color: tokens.border

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: tokens.spaceM
            anchors.rightMargin: tokens.spaceM
            anchors.bottomMargin: tokens.spaceM
            // 顶部让开悬浮的品牌/窗口按钮行，否则标题会被按钮压住。
            anchors.topMargin: tokens.spaceM + (layoutB.compact ? 26 : 30) + tokens.spaceS
            spacing: tokens.spaceS

            Text {
                visible: !layoutB.shortHeight
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

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: tokens.border }

            Text {
                visible: !layoutB.shortHeight
                text: "最近打开"
                color: tokens.textMuted
                font.pixelSize: tokens.fontS
            }

            ListView {
                id: recentList
                objectName: "recentListB"
                Layout.fillWidth: true
                Layout.preferredHeight: layoutB.shortHeight ? 60
                    : (layoutB.compact ? 110 : 160)
                clip: true
                model: win.recentFiles
                spacing: 2
                // 与 UiStateStore 双向同步：切换布局重建后回到同一项。
                currentIndex: typeof uiState !== "undefined" ? uiState.scrollIndex : -1
                onCurrentIndexChanged: {
                    // 模型尚未装载时 ListView 会把 currentIndex 压成 -1/0，别把这当成用户滚动。
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

            Item { Layout.fillHeight: true }

            Text {
                visible: !layoutB.shortHeight
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
                    actionId: "playPause"
                    onClicked: win.action("playPause")
                }
                PillButton { label: "-10s"; actionId: "seekBack10"; onClicked: win.action("seekBack10") }
                PillButton { label: "+10s"; actionId: "seekForward10"; onClicked: win.action("seekForward10") }
                Item { Layout.fillWidth: true }
                PillButton {
                    label: Math.round(player.speed * 100) / 100 + "x"
                    actionId: "speedUp"
                    onClicked: win.action("speedUp")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: tokens.spaceS
                PillButton { label: "音轨"; actionId: "cycleAudio"; onClicked: win.action("cycleAudio") }
                PillButton { label: "字幕"; actionId: "cycleSubtitle"; onClicked: win.action("cycleSubtitle") }
                Item { Layout.fillWidth: true }
                PillButton { label: "切换布局"; actionId: "cycleTheme"; onClicked: win.action("cycleTheme") }
            }
        }

        // 右边缘拖拽改检查器宽度（紧凑模式固定宽度，不提供拖拽）
        Rectangle {
            id: resizeHandle
            visible: !layoutB.compact
            width: 6
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
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
                    layoutB.dragWidth = Math.max(160,
                                                 Math.min(720, startWidth + globalX - startMouseX))
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

    // 底部运输条
    Rectangle {
        visible: uiState.chromeVisible
        height: layoutB.compact ? tokens.transportHeight - 32 : tokens.transportHeight - 20
        anchors { left: dock.right; right: parent.right; bottom: parent.bottom }
        color: tokens.glass

        RowLayout {
            anchors.fill: parent
            anchors.margins: layoutB.compact ? 8 : 14
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
