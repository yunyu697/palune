import QtQuick
import QtQuick.Controls.Basic
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
    readonly property string themeId: typeof themeRegistry !== "undefined" ? themeRegistry.currentTheme : "A"
    readonly property bool hasFile: typeof player !== "undefined" && player.filePath !== ""
    readonly property bool compactUi: typeof compactMode !== "undefined" && compactMode.compact
    // 每个主题顶部可拖拽区的高度（A 浮动标题条 84 + 外边距，B 52，C 标题胶囊 40）。
    readonly property int dragStripHeight: root.themeId === "B" ? 52 : (root.themeId === "C" ? 44 : 94)
    // 控制条所在整条区域的高度（含隐藏状态），用于悬停热区。
    readonly property int controlZoneHeight: root.themeId === "A" ? (root.compactUi ? 64 : tokens.transportHeight)
                                               : root.themeId === "B" ? (root.compactUi ? 64 : 82)
                                               : (root.compactUi ? 44 : tokens.capsuleHeight)
    readonly property int controlZoneMargin: root.themeId === "A" ? 10
                                             : (root.themeId === "C" ? (root.compactUi ? 12 : 24) : 0)

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

    // 视频区鼠标定义（docs/MOUSE-INTERACTION.md）：
    // 单击 = 播放/暂停；双击 = 全屏；右键 = 上下文菜单；滚轮 = 音量（Ctrl+滚轮 = seek ±10s）。
    // 本层位于 layoutSlot **之下**：布局/控制条上的点击优先，不会穿透到视频。
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: root.overlayPanel === ""
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                if (root.hasFile)
                    contextMenu.popup()
            } else if (root.hasFile) {
                win.action("playPause")
            }
        }
        onDoubleClicked: function (mouse) {
            if (mouse.button === Qt.LeftButton && root.hasFile)
                win.action("toggleFullscreen")
        }
        onWheel: function (wheel) {
            if (wheel.modifiers & Qt.ControlModifier)
                win.action("seekBy", wheel.angleDelta.y > 0 ? 10 : -10)
            else
                win.action("volumeBy", (wheel.angleDelta.y / 120) * 5)
        }
    }

    Menu {
        id: contextMenu
        palette.text: "#e8eaee"
        palette.windowText: "#e8eaee"
        palette.window: "#0d0e11"
        background: Rectangle {
            color: "#f20d0e11"
            radius: 10
            border.width: 1
            border.color: "#40ffffff"
        }
        MenuItem { text: qsTr("播放 / 暂停"); onTriggered: win.action("playPause") }
        MenuItem { text: qsTr("全屏 / 还原"); onTriggered: win.action("toggleFullscreen") }
        MenuItem { text: qsTr("打开文件…"); onTriggered: win.action("openFile") }
        MenuItem { text: qsTr("媒体库"); onTriggered: win.action("openLibrary") }
        MenuItem { text: qsTr("设置"); onTriggered: win.action("openSettings") }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: "#26ffffff" } }
        MenuItem { text: qsTr("退出"); onTriggered: win.action("exit") }
    }

    // 控制条悬停热区：整条区域（含隐藏状态下该区域）进入即显示控制栏，
    // 离开后按 2500ms 延迟隐藏；暂停态不自动隐藏。
    // 用 HoverHandler 而不是 MouseArea：它只收悬停，不消费点击，隐藏态不会拦住视频点击。
    Item {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: root.controlZoneHeight + root.controlZoneMargin
        HoverHandler {
            id: controlHover
            onHoveredChanged: {
                if (hovered)
                    uiState.chromeVisible = true
            }
        }
    }

    // 声明式驱动自动隐藏：只要「有文件 + 正在播放 + 控制栏可见 + 鼠标不在热区 + 无面板」
    // 条件成立就开始 2500ms 倒计时，任一条件变化自动重置。
    // 之前用 hoveredChanged 手动 restart，启动后从未进入热区时计时器根本不跑，
    // 实机表现为控制栏永不隐藏（alpha10 复验发现，差分仅 4.6%）。
    Timer {
        id: chromeHideTimer
        interval: 2500
        repeat: false
        running: root.hasFile && !player.paused && uiState.chromeVisible
                 && !controlHover.hovered && root.overlayPanel === ""
        onTriggered: uiState.chromeVisible = false
    }

    // 无边框窗口拖拽：走系统移动循环（startSystemMove），保留 Aero Snap 与跨 DPI 行为。
    // 位于背景之上、布局之下：布局里的按钮优先接收点击，标题栏空白处落到这里。
    Item {
        id: dragStrip
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: root.dragStripHeight
        visible: !win.fullscreen
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: win.startSystemMove()
            onDoubleClicked: win.toggleMaximize()
        }
    }

    // 布局槽位（LayoutHost 只接管这里）
    Item {
        objectName: "layoutSlot"
        anchors.fill: parent
    }

    // 无边框窗口缩放：四条边 + 四角，交给系统缩放循环。
    Item {
        anchors.fill: parent
        visible: !win.fullscreen
        MouseArea {
            width: 8
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.LeftEdge)
        }
        MouseArea {
            width: 8
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.RightEdge)
        }
        MouseArea {
            height: 8
            anchors { top: parent.top; left: parent.left; right: parent.right }
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.TopEdge)
        }
        MouseArea {
            height: 8
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.BottomEdge)
        }
        MouseArea {
            width: 12; height: 12
            anchors { left: parent.left; top: parent.top }
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
        }
        MouseArea {
            width: 12; height: 12
            anchors { right: parent.right; top: parent.top }
            cursorShape: Qt.SizeDiagCursor
            onPressed: win.startSystemResize(Qt.RightEdge | Qt.TopEdge)
        }
        MouseArea {
            width: 12; height: 12
            anchors { left: parent.left; bottom: parent.bottom }
            cursorShape: Qt.SizeDiagCursor
            onPressed: win.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
        }
        MouseArea {
            width: 12; height: 12
            anchors { right: parent.right; bottom: parent.bottom }
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }

    // 窗口描边：与原生 Region 同一圆角，弥补 DWM 不会给无边框透明窗做圆角。
    // 窗口发丝描边：设计稿 §1.3 hairline alpha 0.06–0.14；内缩 0.5px 避免被原生 Region 裁掉半边，
    // 最大化/全屏时不画，避免与系统边框叠成「两道方框」。
    Rectangle {
        anchors.fill: parent
        anchors.margins: 0.5
        visible: !win.fullscreen && !win.maximized
        radius: win.windowRadius - 0.5
        color: "transparent"
        border.width: 1
        border.color: "#1affffff"
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
                text: qsTr("播放失败")
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
                    label: qsTr("打开其他文件")
                    highlighted: true
                    actionId: "openFile"
                    onClicked: win.action("openFile")
                }
                PillButton {
                    label: qsTr("知道了")
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
        onFileSelected: function (path) {
            root.closePanel()
            win.openPath(path)
        }
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
