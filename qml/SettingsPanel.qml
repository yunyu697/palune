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

    readonly property string accent: "#e7b27c"
    readonly property string textColor: "#e8eaee"
    readonly property string mutedColor: "#9aa0a6"
    readonly property string lineColor: "#26ffffff"

    readonly property bool hasWindow: typeof win !== "undefined"
    readonly property string appVersion: hasWindow ? win.version
                                                    : (typeof updater !== "undefined" ? updater.currentVersion : "")
    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact
    // 窄/矮窗口（480×270 及紧凑断点附近）：页签改横向、内容区可滚动，
    // 避免设置卡片超出窗口造成截断（M2 缩放基线判据）。
    readonly property bool narrow: panelRoot.width < 720 || panelRoot.height < 520

    readonly property var pageTitles: ["通用", "播放", "字幕", "音频", "快捷键", "关于"]

    Rectangle {
        anchors.fill: parent
        color: panelRoot.shotMode ? "#0b0c10" : "#99000000"
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(1000, parent.width - (panelRoot.narrow ? 16 : 72))
        height: Math.min(660, parent.height - (panelRoot.narrow ? 16 : 72))
        radius: panelRoot.narrow ? 8 : 18
        color: "#f20d0e11"
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
                        text: "设置"
                        color: panelRoot.textColor
                        font.pixelSize: panelRoot.narrow ? 16 : 20
                        font.weight: Font.DemiBold
                    }
                    Text {
                        visible: !panelRoot.narrow || panelRoot.height >= 320
                        text: "Palune " + panelRoot.appVersion + " · M2"
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
                        text: "关闭"
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
                visible: panelRoot.narrow
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

                // 左侧页签（窄窗口下隐藏，改用上方横向页签）
                Column {
                    visible: !panelRoot.narrow
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
                    visible: !panelRoot.narrow
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: panelRoot.lineColor
                }

                // 内容区：窄窗口下高度可能不足，用 Flickable 保证可滚动、不截断
                Flickable {
                    id: contentFlick
                    Layout.fillWidth: true
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
                            text: "外观"
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        RowLayout {
                            spacing: 12
                            Text {
                                text: "主题"
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                            Row {
                                spacing: 8
                                Repeater {
                                    model: typeof themeRegistry !== "undefined" ? themeRegistry.themes : []
                                    delegate: Rectangle {
                                        required property string modelData
                                        width: 62
                                        height: 30
                                        radius: 8
                                        color: (typeof themeRegistry !== "undefined"
                                                && themeRegistry.currentTheme === modelData)
                                               ? panelRoot.accent + "33" : "#10ffffff"
                                        border.width: 1
                                        border.color: (typeof themeRegistry !== "undefined"
                                                       && themeRegistry.currentTheme === modelData)
                                                      ? panelRoot.accent : panelRoot.lineColor
                                        Text {
                                            anchors.centerIn: parent
                                            text: "布局 " + modelData
                                            color: panelRoot.textColor
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
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: typeof themeRegistry !== "undefined"
                                         && themeRegistry.fallbackReason !== ""
                                text: typeof themeRegistry !== "undefined" ? themeRegistry.fallbackReason : ""
                                color: panelRoot.accent
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            text: "只提供 A / B / C 三套布局，不设皮肤或主题包入口。"
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text {
                            text: "紧凑模式"
                            color: panelRoot.textColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: "断点：" + (typeof compactMode !== "undefined" ? compactMode.breakpoint : "-")
                                  + "px · 滞回 ±" + (typeof compactMode !== "undefined" ? compactMode.hysteresis : "-")
                                  + "px · 当前：" + (panelRoot.compact ? "紧凑" : "常规")
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
                            text: "断点在 settings.toml 的 [ui.layout.<主题>] compactBelow 中配置，非法值回退 560。"
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

                        Text { text: "当前播放"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { text: "文件：" + (player.filePath === "" ? "未打开" : player.filePath)
                               color: panelRoot.mutedColor; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Text { text: "标题：" + (player.title === "" ? "未打开" : player.title)
                               color: panelRoot.mutedColor; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Text { text: "状态：" + (player.paused ? "暂停" : "播放中")
                               color: panelRoot.mutedColor; font.pixelSize: 12 }
                        Text { text: "时长：" + Math.round(player.duration) + "s · 速度：" + player.speed + "x"
                               color: panelRoot.mutedColor; font.pixelSize: 12 }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: "解码与渲染"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { text: "渲染：libmpv（动态加载）· 输出：" + Qt.platform.os + " 原生窗口"
                               color: panelRoot.mutedColor; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { text: "硬件解码、缓冲与 HDR 状态在 M3/M4 接入。"
                               color: panelRoot.mutedColor; font.pixelSize: 11 }

                        Item { Layout.fillHeight: true }
                    }

                    // 2 字幕
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 2
                        spacing: 10

                        Text { text: "字幕轨道"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Repeater {
                            model: player.subtitleTracks
                            delegate: Text {
                                required property var modelData
                                text: (modelData.selected ? "● " : "○ ") + (modelData.title || ("轨道 " + modelData.id))
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                        }
                        Text {
                            visible: player.subtitleTracks.length === 0
                            text: "当前文件没有内嵌字幕轨道。"
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: "字号 / 延迟 / 外挂字幕在 M3 接入；本页为轨道状态展示。"
                               color: panelRoot.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                        Item { Layout.fillHeight: true }
                    }

                    // 3 音频
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panelRoot.page === 3
                        spacing: 12

                        Text { text: "音量"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        RowLayout {
                            spacing: 12
                            VolumeBar {
                                value: player.muted ? 0 : player.volume
                                onVolumeSet: function (v) { win.action("volumeSet", v) }
                            }
                            Text {
                                text: Math.round(player.volume) + "%" + (player.muted ? "（静音）" : "")
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                        }
                        PillButton {
                            label: player.muted ? "取消静音" : "静音"
                            actionId: "toggleMute"
                            onClicked: win.action("toggleMute")
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: "音频轨道"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Repeater {
                            model: player.audioTracks
                            delegate: Text {
                                required property var modelData
                                text: (modelData.selected ? "● " : "○ ") + (modelData.title || ("轨道 " + modelData.id))
                                color: panelRoot.mutedColor
                                font.pixelSize: 12
                            }
                        }
                        Text {
                            visible: player.audioTracks.length === 0
                            text: "当前文件没有音频轨道。"
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

                        Text { text: "已接线快捷键"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: "空格 播放/暂停 · F11 全屏 · Esc 退出面板/全屏 · O 打开文件\n"
                                  + "← / → 后退/前进 10 秒 · ↑ / ↓ 音量 ±5 · Enter 激活焦点控件\n"
                                  + "Tab 在可操作控件间移动焦点"
                            color: panelRoot.mutedColor
                            font.pixelSize: 12
                            lineHeight: 1.4
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: "快捷键系统进度"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text {
                            text: "40 个 P0 动作的绑定表、启动冲突检测与解绑界面尚未完成（M2 待办）。当前 10 项为主窗口直接接线。"
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

                        Text { text: "Palune " + panelRoot.appVersion; color: panelRoot.textColor; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Text { text: "Qt " + Qt.application.version + " · libmpv 动态链接 · 渲染路径：" + Qt.platform.os
                               color: panelRoot.mutedColor; font.pixelSize: 12
                               wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text {
                            visible: panelRoot.hasWindow
                            text: panelRoot.hasWindow
                                  ? "配置：" + win.configDir + "\n日志：" + win.logFile + "\n崩溃：" + win.crashDir
                                  : ""
                            color: panelRoot.mutedColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panelRoot.lineColor }

                        Text { text: "更新"; color: panelRoot.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        UpdateSection {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                    }   // contentColumn
                }       // contentFlick
            }
        }
    }
}
