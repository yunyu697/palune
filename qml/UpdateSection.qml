import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 12

    readonly property string accent: "#e7b27c"
    readonly property string textColor: "#e8eaee"
    readonly property string mutedColor: "#9aa0a6"
    readonly property string lineColor: "#26ffffff"

    // 常驻：当前版本 + 检查更新 + 打开主页
    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        Text {
            text: qsTr("当前版本 ") + updater.currentVersion
            color: root.textColor
            font.pixelSize: 12
        }

        Text {
            text: qsTr("上次检查：") + updater.lastCheckedText
            color: root.mutedColor
            font.pixelSize: 11
        }

        Item { Layout.fillWidth: true }

        UButton {
            label: updater.busy ? qsTr("检查中…") : qsTr("检查更新")
            enabled: !updater.busy
            onClicked: updater.checkForUpdates()
        }

        UButton {
            label: qsTr("打开主页")
            onClicked: updater.openHomePage()
        }
    }

    Text {
        Layout.fillWidth: true
        text: updater.statusText
        color: updater.state === "error" ? "#ef8f7a" : root.mutedColor
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // 发现新版本的详细信息
    Rectangle {
        visible: updater.updateAvailable
        Layout.fillWidth: true
        implicitHeight: infoColumn.implicitHeight + 28
        radius: 12
        color: "#12ffffff"
        border.width: 1
        border.color: root.lineColor

        ColumnLayout {
            id: infoColumn
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            RowLayout {
                spacing: 12
                Text {
                    text: qsTr("新版本 ") + updater.availableVersion
                    color: root.accent
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("发布 ") + updater.publishedAt.substring(0, 10) + " · " + updater.sizeText
                    color: root.mutedColor
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                text: updater.notes
                color: "#c9ccd1"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    // 镜像列表（仅作为下载通道，完整性校验与镜像无关）
    ColumnLayout {
        visible: updater.updateAvailable
        Layout.fillWidth: true
        spacing: 6

        Text {
            text: qsTr("下载通道 · 当前：") + updater.mirrorName
            color: root.mutedColor
            font.pixelSize: 11
        }
        Text {
            text: qsTr("镜像只影响下载速度；SHA-256 与 Ed25519 签名校验始终基于内置公钥，不依赖镜像。")
            color: "#6f757b"
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Repeater {
            model: updater.mirrors
            delegate: Rectangle {
                id: mirrorRow
                required property int index
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 36
                radius: 9
                color: index === updater.mirrorIndex ? "#22e7b27c" : "#0dffffff"
                border.width: 1
                border.color: index === updater.mirrorIndex ? root.accent : root.lineColor

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: mirrorRow.index === updater.mirrorIndex ? root.accent : "#5a6066"
                    }
                    Text {
                        text: mirrorRow.modelData.name
                        color: root.textColor
                        font.pixelSize: 12
                    }
                    Text {
                        text: mirrorRow.modelData.url
                        color: root.mutedColor
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: mirrorRow.index === updater.mirrorIndex
                        text: qsTr("使用中")
                        color: root.accent
                        font.pixelSize: 10
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: updater.selectMirror(mirrorRow.index)
                }
            }
        }
    }

    // 下载进度
    ColumnLayout {
        visible: updater.state === "downloading"
        Layout.fillWidth: true
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            height: 7
            radius: 4
            color: "#1affffff"
            Rectangle {
                height: parent.height
                radius: 4
                width: parent.width * Math.max(0, Math.min(100, updater.progress)) / 100
                color: root.accent
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: updater.progress + "%"
                color: root.textColor
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Text {
                text: updater.speedText + "   " + updater.etaText
                color: root.mutedColor
                font.pixelSize: 11
            }
        }
    }

    // 操作按钮：只有发现新版本时才出现「更新」与「仅下载」
    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        UButton {
            visible: updater.state === "available" || updater.state === "error"
            label: qsTr("更新")
            primary: true
            onClicked: updater.startUpdate()
        }
        UButton {
            visible: updater.state === "available" || updater.state === "error"
            label: qsTr("仅下载")
            onClicked: updater.startDownloadOnly()
        }
        UButton {
            visible: updater.state === "downloaded"
            label: qsTr("安装")
            primary: true
            onClicked: updater.installDownloaded()
        }
        UButton {
            visible: updater.state === "error"
            label: qsTr("重试")
            onClicked: updater.retry()
        }

        Item { Layout.fillWidth: true }
    }

    component UButton: Rectangle {
        id: button
        property string label: ""
        property bool primary: false
        property bool enabled: true
        signal clicked()
        implicitWidth: buttonLabel.implicitWidth + 30
        implicitHeight: 32
        radius: 9
        opacity: enabled ? 1 : 0.55
        color: primary ? (hover.containsMouse ? "#f0c394" : "#e7b27c")
                       : (hover.containsMouse ? "#26ffffff" : "#12ffffff")
        border.width: primary ? 0 : 1
        border.color: "#2affffff"

        Text {
            id: buttonLabel
            anchors.centerIn: parent
            text: button.label
            color: button.primary ? "#15110c" : "#e6e8ec"
            font.pixelSize: 12
            font.weight: button.primary ? Font.DemiBold : Font.Normal
        }

        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            enabled: button.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }
}
