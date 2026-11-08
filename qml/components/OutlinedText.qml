import QtQuick

// 带可调描边的文本：Qt 的 Text.Outline 描边宽度固定 1px，无法反映 sub.outline 设置，
// 因此用 8 个方向偏移的底层文本模拟真实描边宽度。宽度为 0 时不绘制底层。
Item {
    id: root

    property string text: ""
    property color textColor: "#ffffff"
    property int pixelSize: 15
    property real outline: 1.0
    property int horizontalAlignment: Text.AlignHCenter
    property int wrapMode: Text.WordWrap

    readonly property real outlineWidth: Math.max(0, Math.min(6, outline))

    implicitWidth: measure.implicitWidth
    implicitHeight: foreground.height

    // 仅用于在未显式设置宽度时给出自然宽度，不参与换行高度计算。
    Text {
        id: measure
        visible: false
        text: root.text
        font.pixelSize: root.pixelSize
        wrapMode: Text.NoWrap
    }

    Repeater {
        model: root.outlineWidth > 0.01 ? 8 : 0
        delegate: Text {
            required property int index
            readonly property real angle: index * Math.PI / 4
            x: foreground.x + Math.cos(angle) * root.outlineWidth
            y: foreground.y + Math.sin(angle) * root.outlineWidth
            width: root.width
            horizontalAlignment: root.horizontalAlignment
            wrapMode: root.wrapMode
            text: root.text
            color: "#000000"
            font.pixelSize: root.pixelSize
        }
    }

    Text {
        id: foreground
        width: root.width
        horizontalAlignment: root.horizontalAlignment
        wrapMode: root.wrapMode
        text: root.text
        color: root.textColor
        font.pixelSize: root.pixelSize
    }
}
