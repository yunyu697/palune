import QtQuick
import "shell"
import "tokens"
import "components"

// shell：视频承载 + 布局槽位 + 覆盖层。
// 布局由 C++ LayoutHost 挂到 layoutSlot；VideoHost 永远在布局树外。
Item {
    id: root
    focus: true

    property string overlayPanel: ""
    property string toastText: ""
    // 截图基线用静态背景；正常运行时为空，画面由 VideoHost 下方的原生窗口提供。
    property url backdrop: ""
    property bool errorDismissed: false

    readonly property string errorMessage: typeof player !== "undefined" ? player.errorMessage : ""

    Tokens { id: tokens }

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

    Image {
        anchors.fill: parent
        visible: root.backdrop.toString() !== ""
        source: root.backdrop
        fillMode: Image.PreserveAspectCrop
        asynchronous: false
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

    // 播放错误页：真实绑定 MpvClient.errorMessage，不让失败静默。
    Rectangle {
        id: errorCard
        visible: root.errorMessage !== "" && !root.errorDismissed
        anchors.centerIn: parent
        width: 520
        height: 168
        radius: tokens.radiusL
        color: "#f2160f12"
        border.width: 1
        border.color: "#66e7b27c"

        Column {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 10

            Text {
                text: "播放失败"
                color: tokens.accent
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: root.errorMessage
                color: tokens.text
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 10
                PillButton {
                    label: "打开其他文件"
                    highlighted: true
                    actionId: "openFile"
                    onClicked: win.action("openFile")
                }
                PillButton {
                    label: "知道了"
                    onClicked: root.errorDismissed = true
                }
            }
        }
    }

    Connections {
        target: typeof player !== "undefined" ? player : null
        function onErrorMessageChanged() { root.errorDismissed = false }
    }

    SettingsPanel {
        objectName: "settingsPanel"
        anchors.fill: parent
        visible: root.overlayPanel === "settings"
        onCloseRequested: root.closePanel()
    }

    LibraryPanel {
        anchors.fill: parent
        visible: root.overlayPanel === "library"
        onCloseRequested: root.closePanel()
    }

    // 快捷键统一走 C++ ActionRegistry（40 个 P0 动作、平台键位、冲突检测）。
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape) {
            if (root.overlayPanel !== "")
                root.closePanel()
            else if (win.fullscreen)
                win.action("toggleFullscreen")
            event.accepted = true
            return
        }
        if (typeof actions !== "undefined" && actions.handleKey(event.key, event.modifiers))
            event.accepted = true
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
