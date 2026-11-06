import QtQuick

// 视频承载占位：真正的视频画面是覆盖层下方的原生窗口。
// 这个组件必须由 shell 持有，布局树里不允许出现（静态检查会拦住）。
Item {
    objectName: "videoHost"
    anchors.fill: parent
}
