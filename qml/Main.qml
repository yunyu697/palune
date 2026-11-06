import QtQuick
import "shell"

// shell：视频承载 + 布局槽位 + 覆盖层。
// 布局由 C++ LayoutHost 挂到 layoutSlot；VideoHost 永远在布局树外。
Item {
    id: root
    focus: true

    property string overlayPanel: ""
    property string toastText: ""

    function openPanel(name) {
        root.overlayPanel = name
        uiState.overlayOpen = name !== ""
    }

    function closePanel() {
        root.overlayPanel = ""
        uiState.overlayOpen = false
    }

    function showToast(text) {
        root.toastText = text
        toastTimer.restart()
    }

    VideoHost {
        anchors.fill: parent
    }

    // 布局槽位（LayoutHost 只接管这里）
    Item {
        objectName: "layoutSlot"
        anchors.fill: parent
    }

    // 点击空白显示/隐藏控件；双击全屏
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        enabled: root.overlayPanel === ""
        onClicked: uiState.chromeVisible = !uiState.chromeVisible
        onDoubleClicked: win.action("toggleFullscreen")
    }

    DropArea {
        anchors.fill: parent
        onDropped: function (drop) {
            if (drop.hasUrls && drop.urls.length > 0)
                win.openFile(drop.urls[0])
        }
    }

    // 提示条
    Rectangle {
        id: toast
        visible: root.toastText !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 110
        width: toastLabel.implicitWidth + 36
        height: 36
        radius: 18
        color: "#e610141a"
        border.width: 1
        border.color: "#40ffffff"

        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastText
            color: "#e8eaee"
            font.pixelSize: 12
        }
    }
    Timer {
        id: toastTimer
        interval: 2600
        onTriggered: root.toastText = ""
    }

    SettingsPanel {
        anchors.fill: parent
        visible: root.overlayPanel === "settings"
        onCloseRequested: root.closePanel()
    }

    LibraryPanel {
        anchors.fill: parent
        visible: root.overlayPanel === "library"
        onCloseRequested: root.closePanel()
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Space) {
            win.action("playPause")
            event.accepted = true
        } else if (event.key === Qt.Key_F11) {
            win.action("toggleFullscreen")
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (root.overlayPanel !== "")
                root.closePanel()
            else if (win.fullscreen)
                win.action("toggleFullscreen")
            event.accepted = true
        } else if (event.key === Qt.Key_O) {
            win.action("openFile")
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            win.action("seekBy", -10)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            win.action("seekBy", 10)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            win.action("volumeBy", 5)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            win.action("volumeBy", -5)
            event.accepted = true
        }
    }

    Connections {
        target: win
        function onUiActionRequested(name, value) {
            if (name === "openSettings") {
                root.openPanel("settings")
            } else if (name === "openLibrary") {
                root.openPanel("library")
            } else if (name === "notice") {
                root.showToast(String(value))
            }
        }
        function onFullscreenChanged() {
            if (win.fullscreen)
                uiState.chromeVisible = false
        }
    }
}
