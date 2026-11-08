import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

// 设置面板（6 页）：通用 / 播放 / 字幕 / 音频 / 快捷键 / 关于。
// 截图模式通过 page 属性直接定位到某一页。
Item {
    id: panelRoot
    signal closeRequested()

    property bool shotMode: false
    property int page: 0
    // 独立顶层窗口模式：隐藏压暗层，卡片铺满自身窗口，圆角与原生 Region 共用 panelRadius。
    property bool windowMode: false
    property int panelRadius: 18

    readonly property string accent: "#e7b27c"
    readonly property string textColor: "#e8eaee"
    readonly property string mutedColor: "#9aa0a6"
    readonly property string lineColor: "#26ffffff"

    readonly property bool hasWindow: typeof win !== "undefined"
    readonly property string appVersion: hasWindow ? win.version
                                                    : (typeof updater !== "undefined" ? updater.currentVersion : "")
    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    // 设置形态跟随布局（设计稿 §1.3）：A 居中 1020×660 双栏、B 居中 1080×660 三栏、
    // C 右下 560×452 弹出卡（两栏：导航 + 表单）。
    readonly property string layoutMode: typeof themeRegistry !== "undefined"
                                         ? themeRegistry.currentTheme : "A"
    readonly property bool capsulePopup: panelRoot.layoutMode === "C"
    // 设计稿 a-settings / b-settings 均为「导航 + 表单 + 字幕预览」三栏；A 旧实现只有两栏，
    // 右侧留出大片空白，与原型不一致。C 保持两栏弹出卡。
    readonly property bool threeColumn: (panelRoot.layoutMode === "A"
                                         || panelRoot.layoutMode === "B")
                                        && !panelRoot.narrow
    readonly property int cardWidth: panelRoot.windowMode
                                     ? panelRoot.width
                                     : (panelRoot.capsulePopup
                                        ? Math.min(560, parent.width - 24)
                                        : Math.min(panelRoot.layoutMode === "B" ? 1080 : 1020,
                                                   parent.width - (panelRoot.narrow ? 16 : 72)))
    readonly property int cardHeight: panelRoot.windowMode
                                      ? panelRoot.height
                                      : (panelRoot.capsulePopup
                                         ? Math.min(452, parent.height - 24)
                                         : Math.min(660, parent.height - (panelRoot.narrow ? 16 : 72)))
    // 窄/矮窗口（480×270 及紧凑断点附近）：页签改横向、内容区可滚动，
    // 避免设置卡片超出窗口造成截断（M2 缩放基线判据）。
    readonly property bool narrow: !panelRoot.windowMode && (panelRoot.width < 720 || panelRoot.height < 520)

    readonly property var pageTitles: [qsTr("通用"), qsTr("播放"), qsTr("字幕"), qsTr("音频"), qsTr("快捷键"), qsTr("关于")]

    Rectangle {
        anchors.fill: parent
        visible: !panelRoot.windowMode
        color: panelRoot.shotMode ? "#0b0c10" : "#99000000"
    }

    Rectangle {
        id: card
        width: panelRoot.cardWidth
        height: panelRoot.cardHeight
        x: panelRoot.windowMode ? 0
           : (panelRoot.capsulePopup ? parent.width - width - 16 : (parent.width - width) / 2)
        y: panelRoot.windowMode ? 0
           : (panelRoot.capsulePopup ? parent.height - height - 16 : (parent.height - height) / 2)
        radius: panelRoot.windowMode ? panelRoot.panelRadius : (panelRoot.narrow ? 8 : 18)
        // 独立窗口模式下必须完全不透明：窗口本身是透明的，卡片用 0xf2 会让桌面/视频透上来。
        color: panelRoot.windowMode ? "#ff0d0e11" : "#f20d0e11"
        border.width: 1
        border.color: panelRoot.lineColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: panelRoot.narrow ? 12 : 24
            spacing: panelRoot.narrow ? 8 : 14

            RowLayout {
                Layout.fillWidth: true

                Column {
                    spacing: 4
                    Text {
                        text: qsTr("设置")
                        color: panelRoot.textColor
                        font.pixelSize: panelRoot.narrow ? 16 : 20
                        font.weight: Font.DemiBold
                    }
                    Text {
                        visible: !panelRoot.narrow || panelRoot.height >= 320
                        text: "Palune " + panelRoot.appVersion
                        color: panelRoot.mutedColor
                        font.pixelSize: panelRoot.narrow ? 10 : 11
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    id: closeBtn
                    implicitWidth: 64
                    implicitHeight: 30
                    radius: 9
                    color: closeHover.containsMouse ? "#22ffffff" : "#10ffffff"
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("关闭")
                        color: panelRoot.textColor
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panelRoot.closeRequested()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: panelRoot.lineColor
            }

            // 窄窗口下页签横向排列，省出垂直空间给内容区
            Flow {
                id: compactTabs
                visible: panelRoot.narrow && !panelRoot.capsulePopup
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                spacing: 4

                Repeater {
                    model: panelRoot.pageTitles
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        width: 64
                        height: 26
                        radius: 8
                        color: panelRoot.page === index ? panelRoot.accent + "33"
                                                        : (compactTabHover.containsMouse ? "#18ffffff" : "transparent")
                        border.width: 1
                        border.color: panelRoot.page === index ? panelRoot.accent : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: panelRoot.page === index ? panelRoot.accent : panelRoot.textColor
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: compactTabHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panelRoot.page = index
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 18

                // 左侧页签（窄窗口下隐藏，改用上方横向页签；C 的 560×452 弹出卡保留两栏）
                Column {
                    visible: !panelRoot.narrow || panelRoot.capsulePopup
                    spacing: 6
                    Repeater {
                        model: panelRoot.pageTitles
                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: 118
                            height: 34
                            radius: 9
                            color: panelRoot.page === index ? panelRoot.accent + "33"
                                                            : (tabHover.containsMouse ? "#18ffffff" : "transparent")
                            border.width: 1
                            border.color: panelRoot.page === index ? panelRoot.accent : "transparent"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                verticalAlignment: Text.AlignVCenter
                                text: modelData
                                color: panelRoot.page === index ? panelRoot.accent : panelRoot.textColor
                                font.pixelSize: 13
                            }
                            MouseArea {
                                id: tabHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panelRoot.page = index
                            }
                        }
                    }
                }

                Rectangle {
                    visible: !panelRoot.narrow || panelRoot.capsulePopup
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: panelRoot.lineColor
                }

                // 内容区：窄窗口下高度可能不足，用 Flickable 保证可滚动、不截断
                Flickable {
                    id: contentFlick
                    Layout.fillWidth: true
                    // 有第三栏时 RowLayout 会按 preferredWidth 分剩余空间，这里给出基准宽度，
                    // 否则内容区会被压到 0（实机表现为「表单空白、第三栏占满」）。
                    Layout.preferredWidth: 420
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: contentColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        policy: contentFlick.contentHeight > contentFlick.height
                                ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }

                    ColumnLayout {
                        id: contentColumn
                        width: contentFlick.width
                        spacing: 0

                    // 0 通用
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 0
                        spacing: 12

                        Text {
                            text: qsTr("外观")
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("主题")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }
                        Item {
                            // 标签「布局 X」比 108px 卡片宽，首卡标签会越过内容区左边界被 Flickable 裁掉
                            // （实机显示为「局 A」）。外层留 16px 内边距，保证三张标签完整可见。
                            Layout.preferredWidth: 364
                            Layout.preferredHeight: 88
                            Row {
                            x: 16
                            spacing: 12
                            // ColumnLayout 不会为 Repeater 生成的子项推导高度，显式给 Row 尺寸，
                            // 否则后续分组会叠在预览卡上（实机出现过标签被「紧凑模式」覆盖）。
                            width: 348
                            height: 88
                            Repeater {
                                model: typeof themeRegistry !== "undefined" ? themeRegistry.themes : []
                                delegate: Column {
                                    required property string modelData
                                    spacing: 4
                                    readonly property bool selected: typeof themeRegistry !== "undefined"
                                                                  && themeRegistry.currentTheme === modelData
                                    // 布局示意图：必须与实际布局一致（控制条位置、侧栏形态、信息密度）。
                                    Rectangle {
                                        width: 108
                                        height: 66
                                        radius: 6
                                        color: "#0b0c10"
                                        border.width: 1
                                        border.color: selected ? panelRoot.accent : panelRoot.lineColor

                                        // A：悬浮标题条 + 底部双行控制栏，无侧栏，信息密度低
                                        Rectangle {
                                            visible: modelData === "A"
                                            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 5 }
                                            height: 7; radius: 2; color: "#2affffff"
                                        }
                                        Rectangle {
                                            visible: modelData === "A"
                                            anchors { bottom: parent.bottom; left: parent.left; right: parent.right; margins: 5 }
                                            height: 15; radius: 3; color: "#33ffffff"
                                            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 3 } height: 2; radius: 1; color: panelRoot.accent }
                                        }

                                        // B：顶部扁平栏 + 右侧常驻检查器 + 底部技术条/控制条，信息密度高
                                        Rectangle {
                                            visible: modelData === "B"
                                            anchors { top: parent.top; left: parent.left; right: parent.right }
                                            height: 9; color: "#2affffff"
                                        }
                                        Rectangle {
                                            visible: modelData === "B"
                                            anchors { top: parent.top; right: parent.right; bottom: parent.bottom }
                                            width: 28; color: "#40ffffff"
                                        }
                                        Rectangle {
                                            visible: modelData === "B"
                                            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                                            height: 13; color: "#33ffffff"
                                        }

                                        // C：左上标题胶囊 + 右上窗口按钮 + 底部悬浮胶囊，无侧栏，信息密度最低
                                        Rectangle {
                                            visible: modelData === "C"
                                            anchors { top: parent.top; left: parent.left; margins: 5 }
                                            width: 30; height: 7; radius: 3.5; color: "#2affffff"
                                        }
                                        Row {
                                            visible: modelData === "C"
                                            anchors { top: parent.top; right: parent.right; margins: 5 }
                                            spacing: 2
                                            Repeater {
                                                model: 3
                                                Rectangle { width: 5; height: 5; radius: 2.5; color: "#2affffff" }
                                            }
                                        }
                                        Rectangle {
                                            visible: modelData === "C"
                                            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 6 }
                                            width: 68; height: 9; radius: 4.5; color: "#33ffffff"
                                        }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: qsTr("布局 ") + modelData
                                        color: selected ? panelRoot.accent : panelRoot.textColor
                                        font.pixelSize: 11
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (panelRoot.hasWindow)
                                                win.action("setTheme", modelData)
                                        }
                                    }
                                }
                            }
                            }
                        }
                        Text {
                            visible: typeof themeRegistry !== "undefined"
                                     && themeRegistry.fallbackReason !== ""
                            text: typeof themeRegistry !== "undefined" ? themeRegistry.fallbackReason : ""
                            color: panelRoot.accent
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("只提供 A / B / C 三套布局，不设皮肤或主题包入口。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text {
                            text: qsTr("紧凑模式")
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("断点：") + (typeof compactMode !== "undefined" ? compactMode.breakpoint : "-")
                                  + qsTr("px · 滞回 ±") + (typeof compactMode !== "undefined" ? compactMode.hysteresis : "-")
                                  + qsTr("px · 当前：") + (panelRoot.compact ? qsTr("紧凑") : qsTr("常规"))
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: typeof compactMode !== "undefined" && compactMode.configNotice !== ""
                            text: typeof compactMode !== "undefined" ? compactMode.configNotice : ""
                            color: panelRoot.accent
                            font.pixelSize: 11
                        }
                        Text {
                            text: qsTr("断点在 settings.toml 的 [ui.layout.<主题>] compactBelow 中配置，非法值回退 560。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // 1 播放
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 1
                        spacing: 10

                        Text { text: qsTr("当前播放"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { text: qsTr("文件：") + (player.filePath === "" ? qsTr("未打开") : player.filePath)
                               color: panelRoot.mutedColor; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Text { text: qsTr("标题：") + (player.title === "" ? qsTr("未打开") : player.title)
                               color: panelRoot.mutedColor; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Text { text: qsTr("状态：") + (player.paused ? qsTr("暂停") : qsTr("播放中"))
                               color: panelRoot.mutedColor; font.pixelSize: 12 }
                        Text { text: qsTr("时长：") + Math.round(player.duration) + qsTr("s · 速度：") + player.speed + "x"
                               color: panelRoot.mutedColor; font.pixelSize: 12 }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("解码与渲染"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { text: qsTr("渲染：libmpv（动态加载）· 输出：") + Qt.platform.os + qsTr(" 原生窗口")
                               color: panelRoot.mutedColor; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { text: qsTr("硬件解码、缓冲与 HDR 状态在 M3/M4 接入。")
                               color: panelRoot.mutedColor; font.pixelSize: 11 }

                        Item { Layout.fillHeight: true }
                    }

                    // 2 字幕
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 2
                        spacing: 10

                        Text { text: qsTr("字幕轨道"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Repeater {
                            model: player.subtitleTracks
                            delegate: Text {
                                required property var modelData
                                text: (modelData.selected ? "● " : "○ ") + (modelData.title || (qsTr("轨道 ") + modelData.id))
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                        }
                        Text {
                            visible: player.subtitleTracks.length === 0
                            text: qsTr("当前文件没有内嵌字幕轨道。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("字号 / 延迟 / 外挂字幕在 M3 接入；本页为轨道状态展示。")
                               color: panelRoot.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                        Item { Layout.fillHeight: true }
                    }

                    // 3 音频
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 3
                        spacing: 12

                        Text { text: qsTr("音量"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        RowLayout {
                            spacing: 12
                            VolumeBar {
                                value: player.muted ? 0 : player.volume
                                onVolumeSet: function (v) { win.action("volumeSet", v) }
                            }
                            Text {
                                text: Math.round(player.volume) + "%" + (player.muted ? qsTr("（静音）") : "")
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                        }
                        PillButton {
                            label: player.muted ? qsTr("取消静音") : qsTr("静音")
                            actionId: "toggleMute"
                            onClicked: win.action("toggleMute")
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("音频轨道"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Repeater {
                            model: player.audioTracks
                            delegate: Text {
                                required property var modelData
                                text: (modelData.selected ? "● " : "○ ") + (modelData.title || (qsTr("轨道 ") + modelData.id))
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                        }
                        Text {
                            visible: player.audioTracks.length === 0
                            text: qsTr("当前文件没有音频轨道。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // 4 快捷键
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 4
                        spacing: 8

                        Text { text: qsTr("已接线快捷键"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("空格 播放/暂停 · F11 全屏 · Esc 退出面板/全屏 · O 打开文件\n")
                                  + qsTr("← / → 后退/前进 10 秒 · ↑ / ↓ 音量 ±5 · Enter 激活焦点控件\n")
                                  + qsTr("Tab 在可操作控件间移动焦点")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                            lineHeight: 1.4
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("快捷键系统进度"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("40 个 P0 动作的绑定表、启动冲突检测与解绑界面尚未完成（M2 待办）。当前 10 项为主窗口直接接线。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // 5 关于
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 5
                        spacing: 12

                        // 品牌图标与窗口/任务栏/exe 共用同一份源（resources/brand）。
                        Image {
                            source: "qrc:/packaging/palune-256.png"
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 64
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        Text { text: "Palune " + panelRoot.appVersion; color: panelRoot.textColor; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Text { text: "Qt " + Qt.application.version + qsTr(" · libmpv 动态链接 · 渲染路径：") + Qt.platform.os
                               color: panelRoot.mutedColor; font.pixelSize: 12
                               wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text {
                            visible: panelRoot.hasWindow
                            text: panelRoot.hasWindow
                                  ? qsTr("配置：") + win.configDir + qsTr("\n日志：") + win.logFile + qsTr("\n崩溃：") + win.crashDir
                                  : ""
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("更新"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        UpdateSection {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                    }   // contentColumn
                }       // contentFlick

                // B 的第三栏：字幕预览 + BT.2390 曲线（设计稿 §3.B / §7.1）
                Rectangle {
                    visible: panelRoot.threeColumn
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: panelRoot.lineColor
                }

                ColumnLayout {
                    visible: panelRoot.threeColumn
                    Layout.preferredWidth: 300
                    Layout.maximumWidth: 300
                    Layout.fillHeight: true
                    spacing: 10

                    Text {
                        text: qsTr("字幕预览")
                        color: panelRoot.textColor
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        radius: 10
                        color: "#0b0c10"
                        border.width: 1
                        border.color: panelRoot.lineColor

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("这是一行用于校准字号与描边的字幕预览")
                            color: panelRoot.textColor
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        text: qsTr("字号 15 · 描边 1px · 底部安全距 8%")
                        color: panelRoot.mutedColor
                        font.pixelSize: 11
                    }

                    Text {
                        text: qsTr("BT.2390 色调映射")
                        color: panelRoot.textColor
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        Layout.topMargin: 6
                    }
                    Canvas {
                        id: toneCurve
                        Layout.fillWidth: true
                        Layout.preferredHeight: 140
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = "#26ffffff"
                            ctx.lineWidth = 1
                            for (var i = 1; i < 4; ++i) {
                                var gx = width * i / 4
                                var gy = height * i / 4
                                ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, height); ctx.stroke()
                                ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke()
                            }
                            // BT.2390：高光段压缩更平缓（低段近线性）
                            ctx.strokeStyle = "#e7b27c"
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            for (var x = 0; x <= width; ++x) {
                                var t = x / width
                                var y = height * (1 - (t < 0.6 ? t : 0.6 + (t - 0.6) * 0.7))
                                if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                            }
                            ctx.stroke()
                            // 参考线性曲线
                            ctx.strokeStyle = "#55e7b27c"
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(0, height); ctx.lineTo(width, 0)
                            ctx.stroke()
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                    }
                    Text {
                        text: qsTr("目标 1000 nits · 输入 HDR10（占位曲线，接入 mpv 色调映射参数后替换）")
                        color: panelRoot.mutedColor
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
