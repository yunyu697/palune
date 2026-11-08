import QtQuick
import "../tokens"

// 开关：比较小、可键盘操作（Tab 聚焦 + 空格切换）。
Item {
    id: toggle

    property bool checked: false
    signal toggled(bool value)

    implicitWidth: 38
    implicitHeight: 20
    activeFocusOnTab: true

    Tokens { id: tokens }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: toggle.checked ? tokens.accentSoft : "#1affffff"
        border.width: toggle.activeFocus ? 2 : 1
        border.color: toggle.checked ? tokens.accent : tokens.border

        Rectangle {
            id: knob
            width: 14
            height: 14
            radius: 7
            y: 3
            x: toggle.checked ? parent.width - width - 3 : 3
            color: toggle.checked ? tokens.accent : "#aaeeeff2"
            Behavior on x { NumberAnimation { duration: 120 } }
        }

        MouseArea {
            anchors.fill: parent
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
