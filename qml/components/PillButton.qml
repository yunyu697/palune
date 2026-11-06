import QtQuick
import "../tokens"

// L1：胶囊按钮。只发 clicked，不碰播放器。
Rectangle {
    id: btn

    property string label: ""
    property bool highlighted: false
    signal clicked()

    implicitWidth: labelText.implicitWidth + 26
    implicitHeight: 30
    radius: tokens.radiusS
    color: highlighted
           ? tokens.accentSoft
           : (hoverArea.containsMouse ? "#26ffffff" : "#14ffffff")
    border.width: 1
    border.color: highlighted ? tokens.accent : tokens.border

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
        onClicked: btn.clicked()
    }
}
