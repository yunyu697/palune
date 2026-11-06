import QtQuick
import QtQuick.Controls.Basic
import "../tokens"

// L1：进度条。位置与时长只读，拖动结束后发 seekRequested。
Item {
    id: seek
    property real position: 0
    property real duration: 0
    signal seekRequested(real value)

    implicitHeight: 30
    implicitWidth: 200

    Tokens { id: tokens }

    Text {
        id: currentText
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 52
        text: fmtTime(seek.position)
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

    Slider {
        id: slider
        anchors.left: currentText.right
        anchors.right: totalText.left
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        from: 0
        to: Math.max(1, seek.duration)

        property bool userSeeking: false

        Binding on value {
            when: !slider.userSeeking
            value: seek.position
        }

        onPressedChanged: {
            if (pressed) {
                userSeeking = true
            } else if (userSeeking) {
                userSeeking = false
                seek.seekRequested(value)
            }
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
