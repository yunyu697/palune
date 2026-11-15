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
    readonly property int dragStripHeight: root.themeId === "B" ? tokens.topBarB : (root.themeId === "C" ? tokens.compactCapsuleH : tokens.dragStripA)
    // 控制条所在整条区域的高度（含隐藏状态），用于悬停热区。
    readonly property int controlZoneHeight: root.themeId === "A" ? (root.compactUi ? tokens.compactBarH : tokens.transportHeight)
                                               : root.themeId === "B" ? (root.compactUi ? tokens.compactBarH : tokens.controlBarB)
                                               : (root.compactUi ? tokens.compactCapsuleH : tokens.capsuleHeight)
    readonly property int controlZoneMargin: root.themeId === "A" ? tokens.spaceM
                                             : (root.themeId === "C" ? (root.compactUi ? tokens.spaceL : tokens.space3XL) : 0)

    Tokens { id: tokens }

    // 系统窗框边框色跟随主题：由 C++ 下发 DWMWA_BORDER_COLOR，QML 不再画窗口级描边。
    Component.onCompleted: win.setWindowFrameColor(tokens.layerSolid)
    onThemeIdChanged: win.setWindowFrameColor(tokens.layerSolid)

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
        palette.text: tokens.text
        palette.windowText: tokens.text
        palette.window: "#0d0e11"
        background: Rectangle {
            color: tokens.layerCard
            radius: tokens.radiusM
            border.width: tokens.stroke
            border.color: tokens.borderStrong
        }
        MenuItem { text: qsTr("播放 / 暂停"); onTriggered: win.action("playPause") }
        MenuItem { text: qsTr("全屏 / 还原"); onTriggered: win.action("toggleFullscreen") }
        MenuItem { text: qsTr("打开文件…"); onTriggered: win.action("openFile") }
        MenuItem { text: qsTr("打开网络地址…"); onTriggered: win.action("openUrl") }
        MenuItem { text: qsTr("添加到队列…"); onTriggered: win.action("addToQueue") }
        MenuItem { text: qsTr("截图"); onTriggered: win.action("screenshot") }
        MenuItem { text: qsTr("媒体库"); onTriggered: win.action("openLibrary") }
        MenuItem { text: qsTr("设置"); onTriggered: win.action("openSettings") }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: tokens.stateHover } }
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
            cursorShape: Qt.SizeBDiagCursor
            onPressed: win.startSystemResize(Qt.RightEdge | Qt.TopEdge)
        }
        MouseArea {
            width: 12; height: 12
            anchors { left: parent.left; bottom: parent.bottom }
            cursorShape: Qt.SizeBDiagCursor
            onPressed: win.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
        }
        MouseArea {
            width: 12; height: 12
            anchors { right: parent.right; bottom: parent.bottom }
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }

    // 窗口外框不再自绘：圆角与边框色都交给系统（DWMWA_WINDOW_CORNER_PREFERENCE /
    // DWMWA_BORDER_COLOR，见 src/window/SystemChrome.h）。这里原来是一条
    // `radius: win.windowRadius - 0.5` + 1px `#1affffff` 的发丝描边，
    // 在深色界面下就是四角/四边那圈很浅的白边，且与原生 Region 的裁切边界不重合。

    DropArea {
        anchors.fill: parent
        onDropped: function (drop) {
            if (drop.hasUrls && drop.urls.length > 0)
                win.handleDrop(drop.urls)
        }
    }

    // 提示条
    Rectangle {
        id: toast
        visible: root.toastText !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: tokens.toastInset
        width: toastLabel.implicitWidth + 36
        height: 36
        radius: tokens.radiusL
        color: tokens.layerPopover
        border.width: tokens.stroke
        border.color: tokens.borderStrong

        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastText
            color: tokens.text
            font.pixelSize: tokens.fontBody
        }
    }
    Timer {
        id: toastTimer
        interval: 2600
        onTriggered: root.toastText = ""
    }

    // 播放错误页：真实绑定 player.errorMessage（播放器门面），不让失败静默。
    Rectangle {
        id: errorCard
        visible: root.errorMessage !== "" && !root.errorDismissed
        anchors.centerIn: parent
        width: 520
        height: 168
        radius: tokens.radiusL
        color: tokens.layerDanger
        border.width: tokens.stroke
        border.color: tokens.accentBorder

        Column {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: tokens.spaceM

            Text {
                text: qsTr("播放失败")
                color: tokens.accent
                font.pixelSize: tokens.fontTitle
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: root.errorMessage
                color: tokens.text
                font.pixelSize: tokens.fontBody
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: tokens.spaceM
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

    // 覆盖面板存在时屏蔽窗口热区：面板背景是普通 Rectangle，本身不消费鼠标，
    // 之前点击设置/媒体库页面空白处会落到下层 dragStrip，把整个窗口拖走。
    // 这一层只吃事件、不画东西，位于 dragStrip 之上、各面板之下。
    Item {
        objectName: "overlayBlocker"
        anchors.fill: parent
        visible: root.overlayPanel !== ""
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function (wheel) { wheel.accepted = true }
        }
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
            } else if (name === "openUrlDialog") {
                urlDialog.open()
            }
        }
        function onFullscreenChanged() {
            if (win.fullscreen)
                uiState.chromeVisible = false
        }
    }

    // 打开网络地址：Ctrl+U 或右键菜单进入，确认后交给 C++ 判定协议并入队播放。
    Dialog {
        id: urlDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 520
        padding: tokens.spaceXXL
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: tokens.layerPopover
            radius: tokens.radiusL
            border.width: tokens.stroke
            border.color: tokens.statePressed
        }
        contentItem: Column {
            spacing: tokens.spaceL
            Text {
                text: qsTr("打开网络地址")
                color: tokens.text
                font.pixelSize: tokens.fontSubtitle
                font.weight: Font.DemiBold
            }
            TextField {
                id: urlField
                width: parent.width
                placeholderText: qsTr("https://、rtsp://、rtmp:// …")
                color: tokens.text
                placeholderTextColor: tokens.borderStrong
                selectByMouse: true
                font.pixelSize: tokens.fontBodyL
                background: Rectangle {
                    color: tokens.stateRest
                    radius: tokens.radiusS
                    border.width: tokens.stroke
                    border.color: urlField.activeFocus ? tokens.accentBorder : tokens.stateHover
                }
                onAccepted: {
                    win.openUrl(text)
                    urlDialog.close()
                }
            }
            Row {
                spacing: tokens.spaceM
                anchors.right: parent.right
                Button {
                    text: qsTr("取消")
                    onClicked: urlDialog.close()
                }
                Button {
                    text: qsTr("播放")
                    enabled: urlField.text.trim() !== ""
                    onClicked: {
                        win.openUrl(urlField.text)
                        urlDialog.close()
                    }
                }
            }
        }
        onOpened: {
            urlField.text = ""
            urlField.forceActiveFocus()
        }
    }
}
