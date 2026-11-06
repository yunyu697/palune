#!/bin/sh
# Palune Linux 启动器：把 Qt 运行时、QML 模块与 libmpv 的搜索路径固定到安装目录。
here=/opt/palune
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$here/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$here/plugins/platforms"
export QML_IMPORT_PATH="$here/qml"
export QML2_IMPORT_PATH="$here/qml"
exec "$here/bin/palune" "$@"
