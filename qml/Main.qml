import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    focus: true

    property bool chromeVisible: true
    property bool settingsOpen: false

    function fmtTime(sec) {
        if (!sec || sec <= 0)
            return "00:00"
        var total = Math.floor(sec)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : "" + v }
        return (h > 0 ? h + ":" : "") + pad(m) + ":" + pad(s)
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    // 点击：显示/隐藏控件；双击：全屏切换
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: root.chromeVisible = !root.chromeVisible
        onDoubleClicked: win.toggleFullscreen()
    }

    DropArea {
        anchors.fill: parent
        onDropped: function (drop) {
            if (drop.hasUrls && drop.urls.length > 0)
                win.openFile(drop.urls[0])
        }
    }

    // 未打开文件时的空状态
    Rectangle {
        visible: player.filePath === "" && !root.settingsOpen
        width: 420
        height: 128
        radius: 16
        anchors.centerIn: parent
        color: "#cc101216"
        border.width: 1
        border.color: "#26ffffff"

        Column {
            anchors.centerIn: parent
            spacing: 10
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "PALUNE"
                color: "#e7b27c"
                font.pixelSize: 20
                font.letterSpacing: 4
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "将视频拖到这里，或点击「打开」"
                color: "#9aa0a6"
                font.pixelSize: 13
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: win.openFileDialog()
        }
    }

    // ------------------------- 顶栏 -------------------------
    Rectangle {
        id: topBar
        visible: root.chromeVisible && !root.settingsOpen
        height: 56
        anchors { top: parent.top; left: parent.left; right: parent.right }
        color: "#b30b0c10"

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
            anchors.leftMargin: 18
            anchors.rightMargin: 12
            spacing: 8

            Text {
                text: "PALUNE"
                color: "#e7b27c"
                font.pixelSize: 13
                font.letterSpacing: 3
                font.weight: Font.DemiBold
            }

            Text {
                text: player.title === "" ? "未打开文件" : player.title
                color: "#d7dade"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.maximumWidth: 520
            }

            Item { Layout.fillWidth: true }

            PillButton {
                label: "打开"
                onClicked: win.openFileDialog()
            }
            PillButton {
                label: win.fullscreen ? "退出全屏" : "全屏"
                onClicked: win.toggleFullscreen()
            }
            PillButton {
                label: "设置"
                onClicked: root.settingsOpen = true
            }
            PillButton {
                label: "最小化"
                onClicked: win.minimize()
            }
            PillButton {
                label: "关闭"
                onClicked: win.close()
            }
        }
    }

    // ------------------------- 底部控制栏 -------------------------
    Rectangle {
        id: bottomBar
        visible: root.chromeVisible && !root.settingsOpen
        height: 92
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        color: "#cc0b0c10"

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: root.fmtTime(player.position)
                    color: "#e8eaee"
                    font.pixelSize: 12
                    font.family: "Consolas"
                    Layout.preferredWidth: 56
                }

                Slider {
                    id: seekSlider
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, player.duration)
                    property bool userSeeking: false
                    value: 0
                    onPressedChanged: {
                        if (pressed) {
                            userSeeking = true
                        } else if (userSeeking) {
                            userSeeking = false
                            player.seek(value)
                        }
                    }
                    Binding on value {
                        when: !seekSlider.userSeeking
                        value: player.position
                    }
                }

                Text {
                    text: root.fmtTime(player.duration)
                    color: "#9aa0a6"
                    font.pixelSize: 12
                    font.family: "Consolas"
                    Layout.preferredWidth: 56
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                PillButton {
                    label: player.paused ? "播放" : "暂停"
                    onClicked: player.togglePause()
                }

                Text {
                    text: player.muted ? "已静音" : "音量"
                    color: "#9aa0a6"
                    font.pixelSize: 12
                }

                Slider {
                    id: volumeSlider
                    Layout.preferredWidth: 130
                    from: 0
                    to: 100
                    value: 0
                    onMoved: player.setVolume(value)
                    Binding on value {
                        when: !volumeSlider.pressed
                        value: player.volume
                    }
                }

                PillButton {
                    label: player.muted ? "取消静音" : "静音"
                    onClicked: player.toggleMute()
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: win.fullscreen ? "全屏模式" : ""
                    color: "#6b7076"
                    font.pixelSize: 11
                }

                PillButton {
                    label: win.fullscreen ? "退出全屏" : "全屏"
                    onClicked: win.toggleFullscreen()
                }
                PillButton {
                    label: "设置"
                    onClicked: root.settingsOpen = true
                }
            }
        }
    }

    // ------------------------- 设置面板 -------------------------
    SettingsPanel {
        id: settingsPanel
        anchors.fill: parent
        visible: root.settingsOpen
        onCloseRequested: root.settingsOpen = false
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Space) {
            player.togglePause()
            event.accepted = true
        } else if (event.key === Qt.Key_F11) {
            win.toggleFullscreen()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && win.fullscreen) {
            win.setFullscreen(false)
            event.accepted = true
        } else if (event.key === Qt.Key_O) {
            win.openFileDialog()
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            player.seek(Math.max(0, player.position - 10))
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            player.seek(player.position + 10)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            player.setVolume(player.volume + 5)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            player.setVolume(player.volume - 5)
            event.accepted = true
        }
    }

    Connections {
        target: win
        function onFullscreenChanged() {
            if (win.fullscreen)
                root.chromeVisible = false
        }
    }

    component PillButton: Rectangle {
        id: btn
        property string label: ""
        signal clicked()
        implicitWidth: labelText.implicitWidth + 26
        implicitHeight: 30
        radius: 9
        color: hover.containsMouse ? "#26ffffff" : "#14ffffff"
        border.width: 1
        border.color: "#1fffffff"

        Text {
            id: labelText
            anchors.centerIn: parent
            text: btn.label
            color: "#e6e8ec"
            font.pixelSize: 12
        }

        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }
}
