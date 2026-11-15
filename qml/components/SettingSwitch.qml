import QtQuick
import "../tokens"

// 开关：比较小、可键盘操作（Tab 聚焦 + 空格切换）。
Item {
    id: toggle

    property bool checked: false
    signal toggled(bool value)

    implicitWidth: tokens.switchW
    implicitHeight: tokens.switchH
    activeFocusOnTab: true

    Tokens { id: tokens }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: toggle.checked ? tokens.accentSoft : tokens.stateRest
        border.width: toggle.activeFocus ? tokens.strokeFocus : tokens.stroke
        border.color: toggle.checked ? tokens.accent : tokens.border

        Rectangle {
            id: knob
            width: tokens.switchKnob
            height: tokens.switchKnob
            radius: width / 2
            y: (parent.height - height) / 2
            x: toggle.checked ? parent.width - width - (parent.height - height) / 2
                              : (parent.height - height) / 2
            color: toggle.checked ? tokens.accent : tokens.switchOffKnob
            Behavior on x {
                NumberAnimation { duration: tokens.durationFast; easing: tokens.easingStandard }
            }
        }

        MouseArea {
            // 视觉 20px，命中区补到 26px（命中区最小值见 DESIGN-SYSTEM §3）。
            anchors.fill: parent
            anchors.margins: -tokens.hitPad
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                toggle.forceActiveFocus()
                toggle.checked = !toggle.checked
                toggle.toggled(toggle.checked)
            }
        }
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            toggle.checked = !toggle.checked
            toggle.toggled(toggle.checked)
            event.accepted = true
        }
    }
}
