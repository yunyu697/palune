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

    implicitWidth: tokens.stepperW
    implicitHeight: tokens.ctrlH

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
        radius: tokens.radiusS
        color: tokens.stateRest
        border.width: tokens.stroke
        border.color: tokens.border
    }

    Row {
        anchors.fill: parent

        Item {
            width: tokens.stepperBtnW
            height: parent.height
            Text {
                anchors.centerIn: parent
                text: "−"
                color: minusArea.containsMouse ? tokens.accent : tokens.text
                font.pixelSize: tokens.fontBodyL
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
            width: parent.width - tokens.stepperBtnW * 2
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: stepper.formatted()
            color: tokens.text
            font.pixelSize: tokens.fontSmall
        }

        Item {
            width: tokens.stepperBtnW
            height: parent.height
            Text {
                anchors.centerIn: parent
                text: "+"
                color: plusArea.containsMouse ? tokens.accent : tokens.text
                font.pixelSize: tokens.fontBodyL
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
