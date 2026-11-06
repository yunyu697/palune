import QtQuick
import "../tokens"

// L1：胶囊按钮。只发 clicked，不碰播放器。
// actionId 用于跨主题焦点恢复：重建布局后按同一个动作 ID 找等价控件。
Rectangle {
    id: btn

    property string label: ""
    property bool highlighted: false
    property string actionId: ""
    signal clicked()

    readonly property bool restorable: actionId !== ""
    readonly property bool compact: typeof compactMode !== "undefined" && compactMode.compact

    implicitWidth: labelText.implicitWidth + (compact ? 18 : 26)
    implicitHeight: compact ? 26 : 30
    radius: tokens.radiusS
    color: highlighted
           ? tokens.accentSoft
           : (hoverArea.containsMouse ? "#26ffffff" : "#14ffffff")
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? tokens.accent
                              : (highlighted ? tokens.accent : tokens.border)
    activeFocusOnTab: restorable

    Tokens { id: tokens }

    Text {
        id: labelText
        anchors.centerIn: parent
        text: btn.label
        color: btn.highlighted ? tokens.accent : tokens.text
        font.pixelSize: tokens.fontM
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            btn.forceActiveFocus()
            btn.clicked()
        }
    }

    onActiveFocusChanged: {
        if (btn.activeFocus && btn.restorable && typeof uiState !== "undefined")
            uiState.focusActionId = btn.actionId
    }

    Component.onCompleted: {
        // 布局重建后按等价 ActionId 恢复焦点。
        if (btn.restorable && btn.visible && typeof uiState !== "undefined"
                && uiState.focusActionId === btn.actionId)
            Qt.callLater(function () { btn.forceActiveFocus() })
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                || event.key === Qt.Key_Space) {
            btn.clicked()
            event.accepted = true
        }
    }
}
