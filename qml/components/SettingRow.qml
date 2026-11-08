import QtQuick
import QtQuick.Layouts
import "../tokens"

// 设置项行：左侧标签，右侧控件槽（默认属性），下方可选说明。
ColumnLayout {
    id: row

    property string label: ""
    property string hint: ""
    default property alias content: contentRow.data

    Layout.fillWidth: true
    spacing: 3

    Tokens { id: tokens }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: row.label
            color: tokens.text
            font.pixelSize: tokens.fontM
            elide: Text.ElideRight
        }

        Row {
            id: contentRow
            spacing: 6
        }
    }

    Text {
        visible: row.hint !== ""
        Layout.fillWidth: true
        text: row.hint
        color: tokens.textMuted
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
