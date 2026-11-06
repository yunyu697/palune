import QtQuick
import QtQuick.Controls.Basic
import "../tokens"

// L1：音量条。只读音量，改动结束发 volumeChanged。
Row {
    id: volume
    property real value: 0
    signal volumeSet(real value)

    spacing: 8
    implicitHeight: 26

    Tokens { id: tokens }

    Slider {
        id: slider
        width: 120
        anchors.verticalCenter: parent.verticalCenter
        from: 0
        to: 100
        Binding on value {
            when: !slider.pressed
            value: volume.value
        }
        onMoved: volume.volumeSet(value)
    }
}
