import QtQuick

// L0：设计令牌（主题无关的品牌层）。布局与组件只读这里，不硬编码颜色/间距。
QtObject {
    readonly property color bg: "#0b0c10"
    readonly property color bgElevated: "#12141a"
    readonly property color glass: "#cc101216"
    readonly property color glassSoft: "#9910141a"
    readonly property color accent: "#e7b27c"
    readonly property color accentSoft: "#33e7b27c"
    readonly property color text: "#e8eaee"
    readonly property color textMuted: "#9aa0a6"
    readonly property color textFaint: "#6b7076"
    readonly property color border: "#26ffffff"
    readonly property color borderStrong: "#40ffffff"

    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 18
    readonly property int spaceS: 6
    readonly property int spaceM: 12
    readonly property int spaceL: 20

    readonly property int fontS: 11
    readonly property int fontM: 12
    readonly property int fontL: 20

    readonly property int topBarHeight: 56
    readonly property int transportHeight: 92
    readonly property int capsuleHeight: 56

    readonly property int durationFast: 120
    readonly property int durationNormal: 220
}
