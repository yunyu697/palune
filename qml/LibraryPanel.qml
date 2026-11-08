import QtQuick
import QtQuick.Layouts
import "tokens"
import "components"
import "library"

// 媒体库覆盖层（shell 层，不属于任何布局）。
// 形态跟随布局（设计稿 §1.3）：A = 右侧 352px 滑出侧栏；B = 居中宽面板；C = 右侧 76% 抽屉。
// 数据源：MediaLibraryService（本地目录扫描 + JSON 索引 + 缩略图）。
// 两种形态：海报墙（GridView）与列表/表格（ListView），由 library.viewMode 持久化。
Item {
    id: libraryPanel
    signal closeRequested()
    signal fileSelected(string path)

    Tokens { id: tokens }

    // 扫描进行中且还没有任何结果时的轻量指示（不引入 Qt Quick Controls 依赖之外的模块）。
    component BusyLabel: Item {
        id: busyBase
        property int dots: 0
        implicitWidth: busyText.implicitWidth + 12
        implicitHeight: 32
        Text {
            id: busyText
            anchors.centerIn: parent
            text: qsTr("正在扫描") + ".".repeat(busyBase.dots)
            color: tokens.textMuted
            font.pixelSize: tokens.fontM
        }
        Timer {
            interval: 400
            repeat: true
            running: busyBase.visible
            onTriggered: busyBase.dots = (busyBase.dots + 1) % 4
        }
    }

    readonly property string mode: typeof themeRegistry !== "undefined"
                                   ? themeRegistry.currentTheme : "A"
    readonly property bool railMode: libraryPanel.mode === "A"
    readonly property bool drawerMode: libraryPanel.mode === "C"
    readonly property int railWidth: 352
    readonly property int drawerWidth: Math.round(parent.width * 0.76)
    readonly property bool hasItems: typeof library !== "undefined" && library.items.length > 0
    readonly property bool posterMode: typeof library === "undefined" || library.viewMode !== "list"

    // A 是滑出侧栏、不压暗画面；C 是覆盖式抽屉，压暗背景。
    Rectangle {
        anchors.fill: parent
        color: libraryPanel.railMode ? "transparent" : "#99000000"
        MouseArea {
            anchors.fill: parent
            onClicked: libraryPanel.closeRequested()
        }
    }

    Rectangle {
        id: card
        width: libraryPanel.railMode ? Math.min(libraryPanel.railWidth, parent.width - 20)
              : libraryPanel.drawerMode ? libraryPanel.drawerWidth
              : Math.min(1120, parent.width - 96)
        height: libraryPanel.railMode ? parent.height - 114
              : libraryPanel.drawerMode ? parent.height
              : Math.min(640, parent.height - 96)
        x: libraryPanel.railMode || libraryPanel.drawerMode ? parent.width - width - (libraryPanel.railMode ? 10 : 0)
                                                            : (parent.width - width) / 2
        y: libraryPanel.railMode ? 104 : (libraryPanel.drawerMode ? 0 : (parent.height - height) / 2)
        radius: libraryPanel.drawerMode ? 0 : tokens.radiusL
        color: "#f20d0e11"
        border.width: libraryPanel.drawerMode ? 0 : 1
        border.color: tokens.border

        // 卡片自身的点击不关闭面板（只处理边距区域的点击）。
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: libraryPanel.railMode ? 16 : 24
            spacing: tokens.spaceM

            RowLayout {
                Layout.fillWidth: true
                spacing: tokens.spaceS
                Text {
                    text: qsTr("媒体库")
                    color: tokens.text
                    font.pixelSize: libraryPanel.railMode ? 16 : tokens.fontL
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: libraryPanel.hasItems
                    text: typeof library !== "undefined" ? library.itemCount : 0
                    color: tokens.textMuted
                    font.pixelSize: tokens.fontM
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    label: qsTr("关闭")
                    onClicked: libraryPanel.closeRequested()
                }
            }

            // 目录 + 操作条
            RowLayout {
                Layout.fillWidth: true
                spacing: tokens.spaceS
                PillButton {
                    label: qsTr("添加目录")
                    highlighted: true
                    onClicked: library.addRoot()
                }
                PillButton {
                    label: typeof library !== "undefined" && library.scanning ? qsTr("扫描中…") : qsTr("重新扫描")
                    enabled: typeof library !== "undefined" && !library.scanning
                    onClicked: library.rescan()
                }
                Item { Layout.fillWidth: true }
                // 视图切换：海报墙 / 列表
                Row {
                    spacing: 2
                    Rectangle {
                        width: posterLabel.implicitWidth + 18
                        height: 26
                        radius: tokens.radiusS
                        color: libraryPanel.posterMode ? tokens.accentSoft : "#14ffffff"
                        border.width: 1
                        border.color: libraryPanel.posterMode ? tokens.accent : tokens.border
                        Text {
                            id: posterLabel
                            anchors.centerIn: parent
                            text: qsTr("海报")
                            color: libraryPanel.posterMode ? tokens.accent : tokens.text
                            font.pixelSize: tokens.fontM
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: library.setViewMode("poster")
                        }
                    }
                    Rectangle {
                        width: listLabel.implicitWidth + 18
                        height: 26
                        radius: tokens.radiusS
                        color: !libraryPanel.posterMode ? tokens.accentSoft : "#14ffffff"
                        border.width: 1
                        border.color: !libraryPanel.posterMode ? tokens.accent : tokens.border
                        Text {
                            id: listLabel
                            anchors.centerIn: parent
                            text: qsTr("列表")
                            color: !libraryPanel.posterMode ? tokens.accent : tokens.text
                            font.pixelSize: tokens.fontM
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: library.setViewMode("list")
                        }
                    }
                }
            }

            // 根目录 chips（可逐个移除）
            Flow {
                Layout.fillWidth: true
                visible: typeof library !== "undefined" && library.roots.length > 0
                spacing: 6
                Repeater {
                    model: typeof library !== "undefined" ? library.roots : []
                    delegate: Rectangle {
                        height: 22
                        width: rootText.implicitWidth + 30
                        radius: 11
                        color: "#14ffffff"
                        border.width: 1
                        border.color: tokens.border
                        Text {
                            id: rootText
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                const value = String(modelData)
                                const normalized = value.replace(/\\/g, "/")
                                const parts = normalized.split("/").filter(p => p.length > 0)
                                return parts.length > 0 ? parts[parts.length - 1] : value
                            }
                            color: tokens.textMuted
                            font.pixelSize: tokens.fontS
                            elide: Text.ElideMiddle
                            width: Math.min(implicitWidth, libraryPanel.railMode ? 200 : 300)
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "×"
                            color: tokens.textFaint
                            font.pixelSize: 12
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: library.removeRoot(String(modelData))
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: tokens.border
            }

            // 扫描状态放在工具栏下方，不覆盖内容区（alpha10 之后发现底部叠加会压住最后一排条目）。
            Text {
                Layout.fillWidth: true
                visible: typeof library !== "undefined" && library.scanStatus !== ""
                text: typeof library !== "undefined" ? library.scanStatus : ""
                color: tokens.textFaint
                font.pixelSize: tokens.fontS
                elide: Text.ElideRight
            }

            // 内容区
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                MediaLibraryEmpty {
                    anchors.fill: parent
                    visible: !libraryPanel.hasItems && !(typeof library !== "undefined" && library.scanning)
                }

                BusyLabel {
                    anchors.centerIn: parent
                    visible: !libraryPanel.hasItems && typeof library !== "undefined" && library.scanning
                }

                // 海报墙
                GridView {
                    id: posterGrid
                    anchors.fill: parent
                    visible: libraryPanel.hasItems && libraryPanel.posterMode
                    clip: true
                    cellWidth: {
                        const columns = Math.max(2, Math.floor(width / (libraryPanel.railMode ? 156 : 168)))
                        return width / columns
                    }
                    cellHeight: cellWidth * 9 / 16 + 50
                    model: typeof library !== "undefined" ? library.items : []
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        width: posterGrid.cellWidth
                        height: posterGrid.cellHeight

                        Rectangle {
                            id: tile
                            anchors.fill: parent
                            anchors.margins: 4
                            anchors.bottomMargin: 48
                            radius: tokens.radiusM
                            color: hoverArea.containsMouse ? "#1fffffff" : "#12ffffff"
                            border.width: 1
                            border.color: hoverArea.containsMouse ? tokens.accent : tokens.border
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: modelData.poster !== undefined ? modelData.poster : ""
                                visible: source.toString() !== ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: false
                            }

                            // 无缩略图处理器时的占位：类型 + 首字。
                            Rectangle {
                                anchors.fill: parent
                                visible: modelData.poster === undefined || modelData.poster === ""
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#20242c" }
                                    GradientStop { position: 1.0; color: "#14161c" }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    text: (modelData.suffix || "?").toUpperCase()
                                    color: tokens.textFaint
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.margins: 6
                                height: 18
                                width: suffixLabel.implicitWidth + 12
                                radius: 4
                                color: "#cc0b0c10"
                                Text {
                                    id: suffixLabel
                                    anchors.centerIn: parent
                                    text: (modelData.suffix || "").toUpperCase()
                                    color: tokens.textMuted
                                    font.pixelSize: 9
                                }
                            }

                            MouseArea {
                                id: hoverArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: libraryPanel.fileSelected(modelData.path)
                            }
                        }

                        Text {
                            anchors.top: tile.bottom
                            anchors.topMargin: 8
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            text: modelData.title !== undefined ? modelData.title : ""
                            color: tokens.text
                            font.pixelSize: tokens.fontS
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        Text {
                            anchors.top: tile.bottom
                            anchors.topMargin: 23
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            text: (modelData.sizeText || "") + " · " + (modelData.modifiedText || "")
                            color: tokens.textFaint
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                }

                // 列表/表格
                Column {
                    anchors.fill: parent
                    visible: libraryPanel.hasItems && !libraryPanel.posterMode
                    spacing: 0

                    // 表头
                    Rectangle {
                        width: parent.width
                        height: 28
                        color: "#0affffff"
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 10
                            Text {
                                width: 96
                                text: qsTr("预览")
                                color: tokens.textFaint
                                font.pixelSize: tokens.fontS
                            }
                            Text {
                                width: parent.width - 96 - (libraryPanel.railMode ? 118 : 324)
                                text: qsTr("名称")
                                color: tokens.textFaint
                                font.pixelSize: tokens.fontS
                            }
                            Text {
                                width: 52
                                text: qsTr("格式")
                                color: tokens.textFaint
                                font.pixelSize: tokens.fontS
                            }
                            Text {
                                width: 76
                                text: qsTr("大小")
                                color: tokens.textFaint
                                font.pixelSize: tokens.fontS
                            }
                            Text {
                                visible: !libraryPanel.railMode
                                width: 92
                                text: qsTr("修改日期")
                                color: tokens.textFaint
                                font.pixelSize: tokens.fontS
                            }
                            Text {
                                visible: !libraryPanel.railMode
                                width: 100
                                text: qsTr("目录")
                                color: tokens.textFaint
                                font.pixelSize: tokens.fontS
                            }
                        }
                    }

                    ListView {
                        width: parent.width
                        height: parent.height - 28
                        clip: true
                        model: typeof library !== "undefined" ? library.items : []
                        boundsBehavior: Flickable.StopAtBounds
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 56
                            color: rowHover.containsMouse ? "#14ffffff" : (index % 2 === 0 ? "transparent" : "#08ffffff")
                            radius: 4

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 10

                                Rectangle {
                                    width: 96
                                    height: 54
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 4
                                    color: "#14161c"
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: modelData.poster !== undefined ? modelData.poster : ""
                                        visible: source.toString() !== ""
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: false
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: modelData.poster === undefined || modelData.poster === ""
                                        text: (modelData.suffix || "?").toUpperCase()
                                        color: tokens.textFaint
                                        font.pixelSize: 11
                                    }
                                }

                                Column {
                                    width: parent.width - 96 - (libraryPanel.railMode ? 118 : 324)
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Text {
                                        width: parent.width
                                        text: modelData.title !== undefined ? modelData.title : ""
                                        color: tokens.text
                                        font.pixelSize: tokens.fontM
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.fileName !== undefined ? modelData.fileName : ""
                                        color: tokens.textFaint
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    width: 52
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (modelData.suffix || "").toUpperCase()
                                    color: tokens.textMuted
                                    font.pixelSize: tokens.fontS
                                }
                                Text {
                                    width: 76
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.sizeText || ""
                                    color: tokens.textMuted
                                    font.pixelSize: tokens.fontS
                                }
                                Text {
                                    visible: !libraryPanel.railMode
                                    width: 92
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.modifiedText || ""
                                    color: tokens.textMuted
                                    font.pixelSize: tokens.fontS
                                }
                                Text {
                                    visible: !libraryPanel.railMode
                                    width: 100
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: {
                                        const folder = String(modelData.folder || "").replace(/\\/g, "/")
                                        const parts = folder.split("/").filter(p => p.length > 0)
                                        return parts.length > 0 ? parts[parts.length - 1] : ""
                                    }
                                    color: tokens.textFaint
                                    font.pixelSize: tokens.fontS
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: rowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: libraryPanel.fileSelected(modelData.path)
                            }
                        }
                    }
                }
            }
        }
    }
}
