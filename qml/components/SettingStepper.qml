import QtQuick
import "../tokens"

// 数值步进器：- value +，支持 min/max/step/suffix/decimals。
Item {
    id: stepper

    property real value: 0
    property real minimum: 0
    property real maximum: 100
    property real step: 1
    property int decimals: 0
    property string suffix: ""
    signal changed(real value)

    implicitWidth: 112
    implicitHeight: 26

    Tokens { id: tokens }

    function formatted() {
        return value.toFixed(decimals) + suffix
    }

    function apply(delta) {
        var next = Math.min(maximum, Math.max(minimum, value + delta))
        if (Math.abs(next - value) < 1e-9)
            return
        stepper.value = next
        stepper.changed(next)
    }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "#14ffffff"
        border.width: 1
        border.color: tokens.border
    }

    Row {
        anchors.fill: parent

        Item {
            width: 26
            height: parent.height
            Text {
                anchors.centerIn: parent
                text: "−"
                color: minusArea.containsMouse ? tokens.accent : tokens.text
                font.pixelSize: 13
            }
            MouseArea {
                id: minusArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: stepper.apply(-stepper.step)
            }
        }

        Text {
            width: parent.width - 52
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: stepper.formatted()
            color: tokens.text
            font.pixelSize: 11
        }

        Item {
            width: 26
            height: parent.height
            Text {
                anchors.centerIn: parent
                text: "+"
                color: plusArea.containsMouse ? tokens.accent : tokens.text
                font.pixelSize: 13
            }
            MouseArea {
                id: plusArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: stepper.apply(stepper.step)
            }
        }
    }
}
