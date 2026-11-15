import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "tokens"

ColumnLayout {
    id: root
    spacing: tokens.spaceL

    Tokens { id: tokens }

    readonly property color accent: tokens.accent
    readonly property color textColor: tokens.text
    readonly property color mutedColor: tokens.textMuted
    readonly property color lineColor: tokens.border

    // 常驻：当前版本 + 检查更新 + 打开主页
    RowLayout {
        Layout.fillWidth: true
        spacing: tokens.spaceM

        Text {
            text: qsTr("当前版本 ") + updater.currentVersion
            color: root.textColor
            font.pixelSize: tokens.fontBody
        }

        Text {
            text: qsTr("上次检查：") + updater.lastCheckedText
            color: root.mutedColor
            font.pixelSize: tokens.fontSmall
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
        color: updater.state === "error" ? tokens.danger : root.mutedColor
        font.pixelSize: tokens.fontBody
        wrapMode: Text.WordWrap
    }

    // 发现新版本的详细信息
    Rectangle {
        visible: updater.updateAvailable
        Layout.fillWidth: true
        implicitHeight: infoColumn.implicitHeight + 28
        radius: tokens.radiusM
        color: tokens.stateRest
        border.width: tokens.stroke
        border.color: root.lineColor

        ColumnLayout {
            id: infoColumn
            anchors.fill: parent
            anchors.margins: tokens.spaceL
            spacing: tokens.spaceM

            RowLayout {
                spacing: tokens.spaceL
                Text {
                    text: qsTr("新版本 ") + updater.availableVersion
                    color: root.accent
                    font.pixelSize: tokens.fontSubtitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("发布 ") + updater.publishedAt.substring(0, 10) + " · " + updater.sizeText
                    color: root.mutedColor
                    font.pixelSize: tokens.fontSmall
                }
            }

            Text {
                Layout.fillWidth: true
                text: updater.notes
                color: tokens.textMuted
                font.pixelSize: tokens.fontBody
                wrapMode: Text.WordWrap
            }
        }
    }

    // 镜像列表（仅作为下载通道，完整性校验与镜像无关）
    ColumnLayout {
        visible: updater.updateAvailable
        Layout.fillWidth: true
        spacing: tokens.spaceS

        Text {
            text: qsTr("下载通道 · 当前：") + updater.mirrorName
            color: root.mutedColor
            font.pixelSize: tokens.fontSmall
        }
        Text {
            text: qsTr("镜像只影响下载速度；SHA-256 与 Ed25519 签名校验始终基于内置公钥，不依赖镜像。")
            color: tokens.textFaint
            font.pixelSize: tokens.fontCaption
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
                radius: tokens.radiusS
                color: index === updater.mirrorIndex ? tokens.accentFaint : tokens.stateRest
                border.width: tokens.stroke
                border.color: index === updater.mirrorIndex ? root.accent : root.lineColor

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: tokens.spaceL
                    anchors.rightMargin: tokens.spaceL
                    spacing: tokens.spaceM

                    Rectangle {
                        width: 8
                        height: 8
                        radius: tokens.radiusXS
                        color: mirrorRow.index === updater.mirrorIndex ? root.accent : tokens.textFaint
                    }
                    Text {
                        text: mirrorRow.modelData.name
                        color: root.textColor
                        font.pixelSize: tokens.fontBody
                    }
                    Text {
                        text: mirrorRow.modelData.url
                        color: root.mutedColor
                        font.pixelSize: tokens.fontCaption
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: mirrorRow.index === updater.mirrorIndex
                        text: qsTr("使用中")
                        color: root.accent
                        font.pixelSize: tokens.fontCaption
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
        spacing: tokens.spaceS

        Rectangle {
            Layout.fillWidth: true
            height: 7
            radius: tokens.radiusXS
            color: tokens.stateRest
            Rectangle {
                height: parent.height
                radius: tokens.radiusXS
                width: parent.width * Math.max(0, Math.min(100, updater.progress)) / 100
                color: root.accent
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: updater.progress + "%"
                color: root.textColor
                font.pixelSize: tokens.fontSmall
            }
            Item { Layout.fillWidth: true }
            Text {
                text: updater.speedText + "   " + updater.etaText
                color: root.mutedColor
                font.pixelSize: tokens.fontSmall
            }
        }
    }

    // 操作按钮：只有发现新版本时才出现「更新」与「仅下载」
    RowLayout {
        Layout.fillWidth: true
        spacing: tokens.spaceM

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
        radius: tokens.radiusS
        opacity: enabled ? 1 : 0.55
        color: primary ? (hover.containsMouse ? tokens.accentHover : tokens.accent)
                       : (hover.containsMouse ? tokens.stateHover : tokens.stateRest)
        border.width: primary ? 0 : tokens.stroke
        border.color: tokens.border

        Text {
            id: buttonLabel
            anchors.centerIn: parent
            text: button.label
            color: button.primary ? tokens.accentOn : tokens.text
            font.pixelSize: tokens.fontBody
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
