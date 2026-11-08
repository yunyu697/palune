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
    // 改键捕获：非空时，设置窗口根节点把下一次按键写入该动作。
    property string captureActionId: ""
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

    readonly property var pageTitles: [qsTr("通用"), qsTr("播放"), qsTr("字幕"), qsTr("音频"), qsTr("快捷键"), qsTr("媒体库"), qsTr("存储"), qsTr("关于")]

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
                            text: qsTr("当前：") + (panelRoot.compact ? qsTr("紧凑") : qsTr("常规"))
                                  + qsTr(" · 滞回 ±") + (typeof compactMode !== "undefined" ? compactMode.hysteresis : "-") + qsTr("px")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }
                        SettingRow {
                            label: qsTr("紧凑断点")
                            hint: qsTr("窗口宽度低于该值切到紧凑布局，写入 [ui.layout.<主题>] compactBelow，即时生效（360–1600）")
                            SettingStepper {
                                value: prefs.compactBreakpoint
                                minimum: 360
                                maximum: 1600
                                step: 20
                                suffix: "px"
                                onChanged: function (v) { prefs.compactBreakpoint = v }
                            }
                        }
                        Text {
                            visible: typeof compactMode !== "undefined" && compactMode.configNotice !== ""
                            text: typeof compactMode !== "undefined" ? compactMode.configNotice : ""
                            color: panelRoot.accent
                            font.pixelSize: 11
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text {
                            text: qsTr("启动与关闭")
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        SettingRow {
                            label: qsTr("恢复上次会话")
                            hint: qsTr("启动时自动打开最近播放的文件（[ui] restoreLastSession，默认关）")
                            SettingSwitch {
                                checked: prefs.restoreLastSession
                                onToggled: function (v) { prefs.restoreLastSession = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("打开文件后自动播放")
                            hint: qsTr("[ui] autoPlay，默认开")
                            SettingSwitch {
                                checked: prefs.autoPlay
                                onToggled: function (v) { prefs.autoPlay = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("启动时最大化")
                            hint: qsTr("[ui] startMaximized，默认关（下次启动生效）")
                            SettingSwitch {
                                checked: prefs.startMaximized
                                onToggled: function (v) { prefs.startMaximized = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("关闭窗口时留在托盘")
                            hint: qsTr("[ui] closeToTray，默认关；托盘菜单「退出」始终真正退出")
                            SettingSwitch {
                                checked: prefs.closeToTray
                                onToggled: function (v) { prefs.closeToTray = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("最小化时隐藏到托盘")
                            hint: qsTr("[ui] minimizeToTray，默认关")
                            SettingSwitch {
                                checked: prefs.minimizeToTray
                                onToggled: function (v) { prefs.minimizeToTray = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("退出前确认")
                            hint: qsTr("[ui] confirmExit，默认关")
                            SettingSwitch {
                                checked: prefs.confirmExit
                                onToggled: function (v) { prefs.confirmExit = v }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text {
                            text: qsTr("诊断")
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        SettingRow {
                            label: qsTr("日志级别")
                            hint: qsTr("写入 [diag] logLevel，即时生效；debug 仅用于排查，会产生大量日志")
                            SettingCycle {
                                current: prefs.logLevel
                                options: [
                                    { label: qsTr("调试"), value: "debug" },
                                    { label: qsTr("信息"), value: "info" },
                                    { label: qsTr("警告"), value: "warn" },
                                    { label: qsTr("错误"), value: "error" }
                                ]
                                onPicked: function (v) { prefs.logLevel = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("界面语言")
                            hint: qsTr("写入 [ui] language：system / zh_CN / en_US，默认跟随系统，重启后生效")
                            SettingCycle {
                                current: prefs.language
                                options: [
                                    { label: qsTr("跟随系统"), value: "system" },
                                    { label: qsTr("简体中文"), value: "zh_CN" },
                                    { label: "English", value: "en_US" }
                                ]
                                onPicked: function (v) { prefs.language = v }
                            }
                        }
                        Text {
                            text: qsTr("当前生效：") + (prefs.language === "en_US" ? "English"
                                                  : prefs.language === "zh_CN" ? qsTr("简体中文")
                                                  : qsTr("跟随系统（当前界面文案）"))
                                  + qsTr(" · 切换语言需重启 Palune")
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
                        SettingRow {
                            label: qsTr("硬件解码")
                            hint: qsTr("[playback] hwdec = auto / hardware / off，即时生效（切文件后重新初始化解码器）")
                            SettingCycle {
                                current: prefs.hwdecMode
                                options: [
                                    { label: qsTr("自动"), value: "auto" },
                                    { label: qsTr("强制硬解"), value: "hardware" },
                                    { label: qsTr("关闭（软解）"), value: "off" }
                                ]
                                onPicked: function (v) { prefs.hwdecMode = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("缓存时长")
                            hint: qsTr("[playback] cacheSeconds = 0/30/60；0 表示 mpv 默认，下次打开文件生效")
                            SettingCycle {
                                current: prefs.cacheSeconds
                                options: [
                                    { label: qsTr("默认"), value: 0 },
                                    { label: qsTr("30 秒"), value: 30 },
                                    { label: qsTr("60 秒"), value: 60 }
                                ]
                                onPicked: function (v) { prefs.cacheSeconds = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("HDR 色调映射（BT.2390）")
                            hint: qsTr("[playback] toneMapping，默认开；关闭时交给 mpv 选映射算法")
                            SettingSwitch {
                                checked: prefs.toneMapping
                                onToggled: function (v) { prefs.toneMapping = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("映射目标峰值")
                            hint: qsTr("[playback] targetPeakNits，默认 1000 nits，即时生效")
                            SettingCycle {
                                current: prefs.targetPeakNits
                                options: [
                                    { label: "600 nits", value: 600 },
                                    { label: "800 nits", value: 800 },
                                    { label: "1000 nits", value: 1000 }
                                ]
                                onPicked: function (v) { prefs.targetPeakNits = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("去隔行")
                            hint: qsTr("[playback] deinterlace = auto / on / off，即时生效")
                            SettingCycle {
                                current: prefs.deinterlace
                                options: [
                                    { label: qsTr("自动"), value: "auto" },
                                    { label: qsTr("开启"), value: "on" },
                                    { label: qsTr("关闭"), value: "off" }
                                ]
                                onPicked: function (v) { prefs.deinterlace = v }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("播放行为"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        SettingRow {
                            label: qsTr("断点续播")
                            hint: qsTr("[playback] resume，默认开；关闭后不再自动跳回上次位置")
                            SettingSwitch {
                                checked: prefs.resumePlayback
                                onToggled: function (v) { prefs.resumePlayback = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("自动播放下一集")
                            hint: qsTr("[playback] autoNext，默认关；播完当前文件后顺序播放下一个同目录文件")
                            SettingSwitch {
                                checked: prefs.autoNext
                                onToggled: function (v) { prefs.autoNext = v }
                            }
                        }
                        Text { text: qsTr("播放速度在退出时写入 [playback] speed，下次启动恢复。")
                               color: panelRoot.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                        Item { Layout.fillHeight: true }
                    }

                    // 2 字幕
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 2
                        spacing: 10

                        Text { text: qsTr("字幕轨道"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: player.subtitleTracks
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 24
                                    radius: 6
                                    color: modelData.selected ? panelRoot.accent + "22" : "transparent"
                                    border.width: 1
                                    border.color: modelData.selected ? panelRoot.accent : panelRoot.lineColor
                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        text: (modelData.selected ? "● " : "○ ") + (modelData.title || (qsTr("轨道 ") + modelData.id))
                                        color: modelData.selected ? panelRoot.accent : panelRoot.mutedColor
                                        font.pixelSize: 11
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: win.action("subtitleSet", modelData.id)
                                    }
                                }
                            }
                        }
                        Text {
                            visible: player.subtitleTracks.length === 0
                            text: qsTr("当前文件没有内嵌字幕轨道。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }
                        PillButton {
                            visible: player.subtitleTracks.length > 0
                            label: qsTr("关闭字幕")
                            onClicked: win.action("subtitleSet", -1)
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("字幕样式"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        SettingRow {
                            label: qsTr("字号")
                            hint: qsTr("[sub] fontSize，默认 15，即时生效")
                            SettingStepper {
                                value: prefs.subtitleFontSize
                                minimum: 8
                                maximum: 64
                                step: 1
                                onChanged: function (v) { prefs.subtitleFontSize = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("颜色")
                            hint: qsTr("[sub] color，默认白，即时生效")
                            SettingCycle {
                                current: prefs.subtitleColor
                                options: [
                                    { label: qsTr("白色"), value: "#ffffff" },
                                    { label: qsTr("黄色"), value: "#ffe082" },
                                    { label: qsTr("青色"), value: "#a5f3fc" },
                                    { label: qsTr("绿色"), value: "#bbf7d0" }
                                ]
                                onPicked: function (v) { prefs.subtitleColor = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("描边")
                            hint: qsTr("[sub] outline，默认 1.0px，即时生效")
                            SettingStepper {
                                value: prefs.subtitleOutline
                                minimum: 0
                                maximum: 6
                                step: 0.5
                                decimals: 1
                                suffix: "px"
                                onChanged: function (v) { prefs.subtitleOutline = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("延迟")
                            hint: qsTr("[sub] delay，默认 0.0s（正值为字幕推迟），即时生效")
                            SettingStepper {
                                value: prefs.subtitleDelay
                                minimum: -10
                                maximum: 10
                                step: 0.1
                                decimals: 1
                                suffix: "s"
                                onChanged: function (v) { prefs.subtitleDelay = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("底部位置")
                            hint: qsTr("[sub] position，默认贴底 8%，即时生效")
                            SettingStepper {
                                value: prefs.subtitlePosition
                                minimum: 0
                                maximum: 40
                                step: 2
                                suffix: "%"
                                onChanged: function (v) { prefs.subtitlePosition = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("自动加载外挂字幕")
                            hint: qsTr("[sub] autoLoad，默认开（模糊匹配同名字幕）")
                            SettingSwitch {
                                checked: prefs.autoLoadSubtitles
                                onToggled: function (v) { prefs.autoLoadSubtitles = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("字幕编码")
                            hint: qsTr("[sub] encoding，默认自动检测；乱码时手动指定")
                            SettingCycle {
                                current: prefs.subtitleEncoding
                                options: [
                                    { label: qsTr("自动"), value: "auto" },
                                    { label: "UTF-8", value: "utf-8" },
                                    { label: "GB18030", value: "gb18030" }
                                ]
                                onPicked: function (v) { prefs.subtitleEncoding = v }
                            }
                        }
                        PillButton {
                            label: qsTr("加载外挂字幕…")
                            onClicked: prefs.chooseSubtitleFile()
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 84
                            radius: 8
                            color: "#0b0c10"
                            border.width: 1
                            border.color: panelRoot.lineColor
                            clip: true
                            // 预览与实际设置联动：字号、颜色、描边（0–6px）、底部位置。
                            OutlinedText {
                                width: parent.width - 24
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: parent.height - height - Math.max(4, parent.height * (prefs.subtitlePosition / 100))
                                text: qsTr("字幕预览：字号、颜色、描边、位置按左侧设置实时变化")
                                pixelSize: prefs.subtitleFontSize
                                textColor: prefs.subtitleColor
                                outline: prefs.subtitleOutline
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

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
                        Text {
                            text: qsTr("音量与静音状态在退出时写入 [audio] volume / muted，下次启动恢复。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("音频轨道"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: player.audioTracks
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 24
                                    radius: 6
                                    color: modelData.selected ? panelRoot.accent + "22" : "transparent"
                                    border.width: 1
                                    border.color: modelData.selected ? panelRoot.accent : panelRoot.lineColor
                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        text: (modelData.selected ? "● " : "○ ") + (modelData.title || (qsTr("轨道 ") + modelData.id))
                                        color: modelData.selected ? panelRoot.accent : panelRoot.mutedColor
                                        font.pixelSize: 11
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: win.action("audioSet", modelData.id)
                                    }
                                }
                            }
                        }
                        Text {
                            visible: player.audioTracks.length === 0
                            text: qsTr("当前文件没有音频轨道。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("输出"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        SettingRow {
                            label: qsTr("输出设备")
                            hint: qsTr("[audio] device，默认跟随系统；列表由 mpv 的 audio-device-list 提供，即时生效")
                            SettingCycle {
                                current: prefs.audioDevice
                                options: {
                                    var list = [{ label: qsTr("自动（系统默认）"), value: "auto" }]
                                    var devices = prefs.audioDevices()
                                    for (var i = 0; i < devices.length; ++i)
                                        list.push({ label: devices[i].label, value: devices[i].value })
                                    return list
                                }
                                onPicked: function (v) { prefs.audioDevice = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("声道")
                            hint: qsTr("[audio] channels = auto / stereo / 5.1，即时生效")
                            SettingCycle {
                                current: prefs.audioChannels
                                options: [
                                    { label: qsTr("自动（原样）"), value: "auto" },
                                    { label: qsTr("立体声"), value: "stereo" },
                                    { label: "5.1", value: "5.1" }
                                ]
                                onPicked: function (v) { prefs.audioChannels = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("音频延迟")
                            hint: qsTr("[audio] delay，默认 0.0s；音画不同步时微调，即时生效")
                            SettingStepper {
                                value: prefs.audioDelay
                                minimum: -10
                                maximum: 10
                                step: 0.05
                                decimals: 2
                                suffix: "s"
                                onChanged: function (v) { prefs.audioDelay = v }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // 4 快捷键
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 4
                        spacing: 8

                        Text {
                            text: qsTr("快捷键（共 ") + prefs.keyRows().length + qsTr(" 项）")
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("点击「改键」后直接按下新组合键；「解绑」置为空；「默认」删除自定义项。写入 [keys] <动作ID>，即时生效并重启保持。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: panelRoot.captureActionId !== ""
                            text: qsTr("正在等待新快捷键…按 Esc 取消")
                            color: panelRoot.accent
                            font.pixelSize: 12
                        }
                        PillButton {
                            visible: panelRoot.captureActionId !== ""
                            label: qsTr("取消改键")
                            onClicked: panelRoot.captureActionId = ""
                        }
                        Text {
                            visible: prefs.keyConflicts().length > 0
                            text: qsTr("检测到 ") + prefs.keyConflicts().length + qsTr(" 处键位冲突（同作用域内相同组合键）")
                            color: "#ff8a80"
                            font.pixelSize: 11
                        }
                        Repeater {
                            model: prefs.keyConflicts()
                            delegate: Text {
                                required property var modelData
                                text: "  · " + modelData.keys + qsTr("（") + modelData.scope + qsTr("）：")
                                      + modelData.first + qsTr(" / ") + modelData.second
                                color: "#ff8a80"
                                font.pixelSize: 10
                            }
                        }
                        PillButton {
                            visible: prefs.keyRows().some(function (row) { return row.custom })
                            label: qsTr("全部恢复默认")
                            onClicked: {
                                var rows = prefs.keyRows()
                                for (var i = 0; i < rows.length; ++i) {
                                    if (rows[i].custom)
                                        prefs.resetKeyBinding(rows[i].id)
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Repeater {
                            model: prefs.keyRows()
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: panelRoot.textColor
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.preferredWidth: 92
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.key === "" ? qsTr("（未绑定）") : modelData.key
                                    color: modelData.custom ? panelRoot.accent : panelRoot.mutedColor
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                PillButton {
                                    label: panelRoot.captureActionId === modelData.id ? qsTr("按键中…") : qsTr("改键")
                                    onClicked: panelRoot.captureActionId = modelData.id
                                }
                                PillButton {
                                    label: qsTr("解绑")
                                    onClicked: {
                                        panelRoot.captureActionId = ""
                                        prefs.clearKeyBinding(modelData.id)
                                    }
                                }
                                PillButton {
                                    visible: modelData.custom
                                    label: qsTr("默认")
                                    onClicked: prefs.resetKeyBinding(modelData.id)
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // 5 媒体库（账户状态 + 根目录管理；P0 补齐设置页入口）
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 5
                        spacing: 12

                        Text { text: qsTr("账户"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("本地模式：不提供账户、登录与云端同步。账户体系无后端支撑，1.1 之后显式延后，不计入 1.0.0 范围。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("媒体库"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("条目 %1 · 根目录 %2 · 状态：%3")
                                  .arg(library.itemCount).arg(library.roots.length)
                                  .arg(library.scanStatus === "" ? qsTr("就绪") : library.scanStatus)
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("索引文件：") + (library.available ? library.indexFile : qsTr("（尚未生成）"))
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            spacing: 8
                            PillButton {
                                label: qsTr("添加目录…")
                                onClicked: library.addRoot()
                            }
                            PillButton {
                                label: qsTr("重新扫描")
                                onClicked: library.rescan()
                            }
                            PillButton {
                                label: qsTr("打开媒体库")
                                onClicked: { if (panelRoot.hasWindow) win.action("openLibrary") }
                            }
                            PillButton {
                                label: qsTr("打开索引目录")
                                onClicked: library.openLibraryDir()
                            }
                        }
                        SettingRow {
                            label: qsTr("默认视图")
                            hint: qsTr("写入 [library] viewMode = poster / list，即时生效，重启后保持")
                            SettingCycle {
                                current: library.viewMode
                                options: [
                                    { label: qsTr("海报墙"), value: "poster" },
                                    { label: qsTr("列表"), value: "list" }
                                ]
                                onPicked: function (v) { library.setViewMode(v) }
                            }
                        }

                        Text {
                            visible: library.roots.length === 0
                            text: qsTr("还没有添加视频目录。点击「添加目录…」后会扫描并生成缩略图。")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: library.roots
                                delegate: RowLayout {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: panelRoot.textColor
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                    }
                                    PillButton {
                                        label: qsTr("移除")
                                        onClicked: library.removeRoot(modelData)
                                    }
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 6 存储（整组：占用展示、可重建缓存清理、索引重建、配置导出/迁移）
                    ColumnLayout {
                        id: storagePage
                        Layout.fillWidth: true
                        visible: panelRoot.page === 6
                        spacing: 10

                        property var usage: ({})
                        function refreshUsage() { storagePage.usage = prefs.storageUsage() }
                        Component.onCompleted: refreshUsage()
                        onVisibleChanged: if (visible) refreshUsage()

                        Timer {
                            interval: 2500
                            repeat: true
                            running: storagePage.visible
                            onTriggered: storagePage.refreshUsage()
                        }

                        Text { text: qsTr("缓存与索引占用"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("缩略图 %1 · 索引 %2 · 日志 %3 · 配置 %4 · 合计 %5")
                                  .arg(storagePage.usage.thumbsText || "-")
                                  .arg(storagePage.usage.indexText || "-")
                                  .arg(storagePage.usage.logsText || "-")
                                  .arg(storagePage.usage.configText || "-")
                                  .arg(storagePage.usage.totalText || "-")
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("配置目录：") + (storagePage.usage.configDir || "")
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        SettingRow {
                            label: qsTr("缩略图缓存上限")
                            hint: qsTr("写入 [storage] thumbCacheLimitMb，默认 2000 MB；下次扫描结束时按最旧优先淘汰")
                            SettingStepper {
                                value: prefs.thumbCacheLimitMb
                                minimum: 100
                                maximum: 20000
                                step: 100
                                suffix: "MB"
                                onChanged: function (v) { prefs.thumbCacheLimitMb = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("日志保留天数")
                            hint: qsTr("写入 [storage] logRetentionDays，默认 14 天；修改后立即清理一次，启动时再清理")
                            SettingStepper {
                                value: prefs.logRetentionDays
                                minimum: 1
                                maximum: 365
                                step: 1
                                suffix: qsTr("天")
                                onChanged: function (v) { prefs.logRetentionDays = v }
                            }
                        }

                        RowLayout {
                            spacing: 8
                            PillButton {
                                label: qsTr("清理缩略图缓存")
                                onClicked: { library.clearThumbCache(); storagePage.refreshUsage() }
                            }
                            PillButton {
                                label: qsTr("按上限立即清理")
                                onClicked: { library.trimThumbCache(); storagePage.refreshUsage() }
                            }
                            PillButton {
                                label: qsTr("重建媒体库索引")
                                onClicked: { library.rebuildLibraryIndex(); storagePage.refreshUsage() }
                            }
                        }
                        RowLayout {
                            spacing: 8
                            PillButton {
                                label: qsTr("导出配置…")
                                onClicked: { prefs.chooseExportConfig(); storagePage.refreshUsage() }
                            }
                            PillButton {
                                label: qsTr("导入配置（重启生效）…")
                                onClicked: { prefs.chooseImportConfig(); storagePage.refreshUsage() }
                            }
                            PillButton {
                                label: qsTr("打开配置目录")
                                onClicked: prefs.openStorageDir()
                            }
                        }
                        Text {
                            visible: prefs.storageNotice !== ""
                            text: prefs.storageNotice
                            color: panelRoot.accent
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: library.scanStatus !== ""
                            text: qsTr("媒体库：") + library.scanStatus
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 7 关于
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 7
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
                        SettingRow {
                            label: qsTr("更新通道")
                            hint: qsTr("[update] channel = stable / beta；beta 读取 latest-beta.json，需发行仓库发布该清单后才有内容")
                            SettingCycle {
                                current: prefs.updateChannel
                                options: [
                                    { label: qsTr("正式版"), value: "stable" },
                                    { label: qsTr("测试版"), value: "beta" }
                                ]
                                onPicked: function (v) { prefs.updateChannel = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("启动时自动检查更新")
                            hint: qsTr("[update] autoCheck，默认开；启动 3 秒后静默检查，只提示下载，不自动安装")
                            SettingSwitch {
                                checked: prefs.autoCheckUpdates
                                onToggled: function (v) { prefs.autoCheckUpdates = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("网络代理")
                            hint: qsTr("[update] proxy，默认跟随系统；即时应用，下次启动仍读取该值")
                            SettingCycle {
                                current: prefs.updateProxy
                                options: [
                                    { label: qsTr("跟随系统"), value: "system" },
                                    { label: qsTr("不使用代理"), value: "none" }
                                ]
                                onPicked: function (v) { prefs.updateProxy = v }
                            }
                        }
                        SettingRow {
                            label: qsTr("下载目录")
                            hint: prefs.downloadDirEffective()
                            PillButton {
                                label: qsTr("选择…")
                                onClicked: prefs.chooseDownloadDir()
                            }
                        }
                        UpdateSection {
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: qsTr("许可与诊断"); color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        RowLayout {
                            spacing: 8
                            PillButton {
                                label: qsTr("查看许可与开源声明")
                                onClicked: prefs.openLicense()
                            }
                            PillButton {
                                label: qsTr("打开配置目录")
                                onClicked: prefs.openConfigDir()
                            }
                            PillButton {
                                label: qsTr("打开崩溃目录")
                                onClicked: prefs.openCrashDir()
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                    }   // contentColumn
                }       // contentFlick

                // B 的第三栏：字幕预览（绑定真实设置）+ BT.2390 预览降级说明
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
                        clip: true

                        OutlinedText {
                            width: parent.width - 32
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: parent.height - height - Math.max(6, parent.height * (prefs.subtitlePosition / 100))
                            text: qsTr("这是一行用于校准字号与描边的字幕预览")
                            pixelSize: prefs.subtitleFontSize
                            textColor: prefs.subtitleColor
                            outline: prefs.subtitleOutline
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                    Text {
                        text: qsTr("字号 %1 · 描边 %2px · 底部安全距 %3%")
                              .arg(prefs.subtitleFontSize)
                              .arg(prefs.subtitleOutline.toFixed(1))
                              .arg(prefs.subtitlePosition)
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
                    Text {
                        text: qsTr("目标峰值 %1 nits（playback.targetPeakNits，即时生效）").arg(prefs.targetPeakNits)
                        color: panelRoot.textColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        radius: 8
                        color: "#0b0c10"
                        border.width: 1
                        border.color: panelRoot.lineColor
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 24
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("曲线预览不可用\n缺少真实 BT.2390 数据源，1.0 不提供图形曲线（非占位图）")
                            color: panelRoot.mutedColor
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        text: qsTr("提示：目标峰值来自播放页的「目标峰值」，修改后立即作用于 mpv。")
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
