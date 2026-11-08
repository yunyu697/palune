import QtQuick
import "../tokens"

// 选项循环器：点击在 options 里循环切换。
// options 为 [{ label: "...", value: ... }]；current 保存当前 value。
Item {
    id: cycle

    property var options: []
    property var current: undefined
    signal picked(var value)

    readonly property var currentOption: {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === current)
                return options[i]
        }
        return options.length > 0 ? options[0] : null
    }

    implicitWidth: Math.max(72, labelText.implicitWidth + 24)
    implicitHeight: 26
    activeFocusOnTab: true

    Tokens { id: tokens }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: hoverArea.containsMouse ? "#26ffffff" : "#14ffffff"
        border.width: cycle.activeFocus ? 2 : 1
        border.color: cycle.activeFocus ? tokens.accent : tokens.border

        Text {
            id: labelText
            anchors.centerIn: parent
            text: cycle.currentOption ? cycle.currentOption.label : "—"
            color: tokens.text
            font.pixelSize: 11
        }

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                cycle.forceActiveFocus()
                cycle.next()
            }
        }
    }

    function next() {
        if (options.length === 0)
            return
        var index = 0
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === current) {
                index = i
                break
            }
        }
        var nextOption = options[(index + 1) % options.length]
        cycle.picked(nextOption.value)
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter || event.key === Qt.Key_Right) {
            cycle.next()
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            if (options.length > 0) {
                var index = 0
                for (var i = 0; i < options.length; ++i) {
                    if (options[i].value === current) { index = i; break }
                }
                var prev = options[(index - 1 + options.length) % options.length]
                cycle.picked(prev.value)
            }
            event.accepted = true
        }
    }
}
