import QtQuick
import QtQuick.Layouts
import "../tokens"

// L1：进度条。位置与时长只读，拖动结束后发 seekRequested。
// 设计稿 8.2：轨道 2px、滑块 9px；时间标签左右对齐，整行固定 24px，
// 不再依赖 Controls Slider 的隐式高度（旧版在 ColumnLayout 里高度为 0，滑块压到按钮上）。
Item {
    id: seek
    property real position: 0
    property real duration: 0
    // <0 表示当前不在拖动；>=0 时显示拖动中的预览位置。
    property real dragRatio: -1
    signal seekRequested(real value)

    implicitHeight: 24
    Layout.preferredHeight: 24

    readonly property real ratio: seek.duration > 0
        ? Math.min(1, Math.max(0, seek.position / seek.duration)) : 0
    readonly property real shownRatio: seek.dragRatio >= 0 ? seek.dragRatio : seek.ratio

    Tokens { id: tokens }

    Text {
        id: currentText
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 52
        text: fmtTime(seek.dragRatio >= 0 ? seek.dragRatio * seek.duration : seek.position)
        color: tokens.text
        font.pixelSize: tokens.fontM
        font.family: "Consolas"
    }

    Text {
        id: totalText
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 52
        horizontalAlignment: Text.AlignRight
        text: fmtTime(seek.duration)
        color: tokens.textMuted
        font.pixelSize: tokens.fontM
        font.family: "Consolas"
    }

    Item {
        id: trackArea
        anchors.left: currentText.right
        anchors.right: totalText.left
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        height: 18

        Rectangle {
            id: trackBg
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 2
            radius: 1
            color: "#33ffffff"
        }

        Rectangle {
            id: trackFill
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, trackBg.width * seek.shownRatio)
            height: 2
            radius: 1
            color: tokens.accent
        }

        Rectangle {
            id: knob
            width: 9
            height: 9
            radius: 4.5
            color: tokens.accent
            anchors.verticalCenter: parent.verticalCenter
            x: Math.min(trackBg.width - width, Math.max(0, trackBg.width * seek.shownRatio - width / 2))
        }

        // 命中区域比轨道高，方便点击；拖动时只更新预览，松手才 seek。
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onPressed: function (mouse) {
                seek.dragRatio = Math.max(0, Math.min(1, mouse.x / trackBg.width))
            }
            onPositionChanged: function (mouse) {
                if (pressed)
                    seek.dragRatio = Math.max(0, Math.min(1, mouse.x / trackBg.width))
            }
            onReleased: {
                const value = seek.dragRatio * seek.duration
                seek.dragRatio = -1
                seek.seekRequested(value)
            }
            onCanceled: seek.dragRatio = -1
        }
    }

    function fmtTime(seconds) {
        if (!seconds || seconds <= 0)
            return "00:00"
        var total = Math.floor(seconds)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : "" + v }
        return (h > 0 ? h + ":" : "") + pad(m) + ":" + pad(s)
    }
}
