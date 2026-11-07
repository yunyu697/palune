import QtQuick
import "../tokens"

// L1：音量条。只读音量，改动结束发 volumeSet。
// 与 SeekBar 同一套视觉：2px 轨道 + 9px 圆点；不再用 Controls Slider 的大滑块。
Item {
    id: volume
    property real value: 0
    signal volumeSet(real value)

    implicitWidth: 120
    implicitHeight: 20

    readonly property real ratio: Math.min(1, Math.max(0, volume.value / 100))

    Tokens { id: tokens }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 2
        radius: 1
        color: "#33ffffff"
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, track.width * volume.ratio)
        height: 2
        radius: 1
        color: tokens.accent
    }
    Rectangle {
        width: 9
        height: 9
        radius: 4.5
        color: tokens.accent
        anchors.verticalCenter: parent.verticalCenter
        x: Math.min(track.width - width, Math.max(0, track.width * volume.ratio - width / 2))
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: function (mouse) { emitFrom(mouse.x) }
        onPositionChanged: function (mouse) { if (pressed) emitFrom(mouse.x) }
        function emitFrom(x) {
            volume.volumeSet(Math.round(Math.max(0, Math.min(1, x / track.width)) * 100))
        }
    }
}
