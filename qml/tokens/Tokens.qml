import QtQuick

// L0：设计令牌（单例对象）。所有间距/字号/行高/圆角/描边/控件高度/动效/断点只在这里取值，
// 组件与布局里不写裸数值。规范见 docs/DESIGN-SYSTEM.md。
// 三套主题（A/B/C）的结构规则完全一致，差异只能体现在本文件的取值上。
QtObject {
    // ---------- 颜色：品牌与层次 ----------
    readonly property color bg: "#0b0c10"
    readonly property color bgElevated: "#12141a"
    readonly property color glass: "#cc101216"
    readonly property color glassSoft: "#9910141a"
    readonly property color accent: "#e7b27c"
    readonly property color accentSoft: "#33e7b27c"
    readonly property color accentFaint: "#22e7b27c"
    readonly property color accentOn: "#15110c"
    readonly property color text: "#e8eaee"
    readonly property color textMuted: "#9aa0a6"
    readonly property color textFaint: "#6b7076"
    readonly property color border: "#26ffffff"
    readonly property color borderStrong: "#40ffffff"
    readonly property color danger: "#ef8f7a"
    readonly property color dangerStrong: "#ff8a80"
    readonly property color dangerHover: "#cc8a3b3b"
    readonly property color dangerHoverText: "#ffd9d9"
    readonly property color switchOffKnob: "#aaeeeff2"
    readonly property color track: "#33ffffff"

    // ---------- 颜色：层次/遮罩（阴影用透明层模拟，不引 DropShadow） ----------
    readonly property color scrim: "#99000000"
    readonly property color layerCard: "#f20d0e11"
    readonly property color layerSolid: "#ff0d0e11"
    readonly property color layerPopover: "#f210141a"
    readonly property color layerSunken: "#0b0c10"
    readonly property color layerOverlay: "#cc0d0f14"

    // ---------- 颜色：交互状态 ----------
    readonly property color stateRest: "#14ffffff"
    readonly property color stateHover: "#26ffffff"
    readonly property color statePressed: "#33ffffff"
    readonly property color stateRow: "#22ffffff"
    readonly property color stateZebra: "#08ffffff"
    readonly property color stateHeader: "#0affffff"

    // ---------- 描边/分隔线颜色与网格 ----------
    readonly property color accentBorder: "#66e7b27c"
    readonly property color accentHover: "#f0c394"
    readonly property color gridLine: "#3affffff"
    readonly property color layerDanger: "#f2160f12"
    readonly property color layerSolidAlt: "#ff0c0e12"
    readonly property color badgeBg: "#cc0b0c10"
    readonly property color posterBg: "#14161c"
    readonly property color posterGradTop: "#20242c"
    readonly property color dangerDim: "#e08a8a"

    // ---------- 间距标尺（4 基） ----------
    readonly property int spaceHair: 2
    readonly property int spaceXS: 4
    readonly property int spaceS: 6
    readonly property int spaceM: 8
    readonly property int spaceL: 12
    readonly property int spaceXL: 16
    readonly property int spaceXXL: 20
    readonly property int space3XL: 24

    // ---------- 圆角 ----------
    readonly property int radiusXS: 4
    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 18

    // ---------- 描边/分隔线 ----------
    readonly property int stroke: 1
    readonly property int strokeFocus: 2

    // ---------- 字号阶梯 ----------
    readonly property int fontCaption: 10
    readonly property int fontSmall: 11
    readonly property int fontBody: 12
    readonly property int fontBodyL: 13
    readonly property int fontSubtitle: 14
    readonly property int fontTitle: 16
    readonly property int fontDisplay: 20
    // 兼容旧调用点（等价于上面的阶梯）
    readonly property int fontS: fontSmall
    readonly property int fontM: fontBody
    readonly property int fontL: fontDisplay

    // ---------- 行高（与字号成对使用） ----------
    readonly property int lineCaption: 14
    readonly property int lineBody: 18
    readonly property int lineTitle: 22
    // 多行文本用比例行高：lineHeight: tokens.lineRatioBody
    readonly property real lineRatioBody: 1.4
    readonly property real lineRatioTitle: 1.25

    // ---------- 字距 / 等宽数字 ----------
    readonly property int trackingS: 2
    readonly property int trackingM: 3
    readonly property int trackingL: 4
    readonly property string fontMono: "Consolas"

    // ---------- 控件高度与命中区 ----------
    readonly property int minHit: 26
    readonly property int hitPad: 4
    readonly property int ctrlHS: 22
    readonly property int ctrlH: 26
    readonly property int ctrlHL: 30
    readonly property int ctrlHXL: 32
    readonly property int ctrlHXXL: 36
    readonly property int rowH: 30
    readonly property int rowHL: 34
    readonly property int rowHXL: 56
    readonly property int switchW: 38
    readonly property int switchH: 20
    readonly property int switchKnob: 14
    readonly property int stepperW: 112
    readonly property int stepperBtnW: 26
    readonly property int stepperMinW: 72
    readonly property int trackThickness: 2
    readonly property int knobSize: 9
    readonly property int seekH: 24
    readonly property int seekLabelW: 52
    readonly property int volumeW: 120
    readonly property int trackHitH: 18
    readonly property int badgeH: 18
    readonly property int chipH: 22

    // ---------- 图标阶梯 ----------
    readonly property int iconXS: 6
    readonly property int iconS: 10
    readonly property int iconM: 14
    readonly property int iconL: 20
    readonly property int iconXL: 24

    // ---------- 布局骨架（设计稿 7.1 / 8.2） ----------
    readonly property int topBarHeight: 84
    readonly property int dragStripA: 94
    readonly property int transportHeight: 86
    readonly property int capsuleHeight: 62
    readonly property int topBarB: 52
    readonly property int techBarB: 34
    readonly property int controlBarB: 82
    readonly property int compactBarH: 64
    readonly property int compactCapsuleH: 44
    readonly property int dockBW: 344
    readonly property int dockBCompactW: 224
    readonly property int libraryRailW: 352
    readonly property int libraryDockReserve: 368
    readonly property int libraryPanelW: 1120
    readonly property int libraryPanelH: 640
    readonly property int settingsCardW: 1020
    readonly property int settingsCardWB: 1080
    readonly property int settingsCardH: 660
    readonly property int settingsCardCW: 560
    readonly property int settingsCardCH: 452
    readonly property int settingsGutter: 72
    readonly property int themeCardW: 108
    readonly property int themeCardH: 66
    readonly property int themeCardSlotH: 88
    readonly property int subtitlePreviewH: 84
    readonly property int subtitlePreviewHL: 150
    readonly property int toastInset: 110
    readonly property int windowButtonsReserve: 112
    readonly property int emptyCardW: 460
    readonly property int emptyCardH: 168

    // ---------- 动效 ----------
    readonly property int durationFast: 120
    readonly property int durationNormal: 220
    readonly property int durationSlow: 320
    readonly property int chromeHideDelay: 2500
    readonly property int toastDuration: 2600
    readonly property int easingStandard: Easing.OutCubic

    // ---------- 响应式断点 ----------
    readonly property int bpNarrow: 360
    readonly property int bpShort: 520
    readonly property int bpCompact: 720
    readonly property int bpWide: 900
    readonly property int bpTiny: 320
}
