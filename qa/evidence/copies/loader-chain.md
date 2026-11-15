# 加载器缺失依赖对话框的受控复现

- 生成时间：2026-11-15 11:10:18
- 方法：从 `build\Release`（完整部署树）复制，仅删除指定 DLL，然后按「工作目录 = exe 目录」启动。

| 变体 | 删除的文件 | 退出码 | 对话框标题 | 对话框正文 | 截图 |
| --- | --- | --- | --- | --- | --- |
| none | Qt6Widgets.dll,Qt6Quick.dll,Qt6Gui.dll,Qt6Qml.dll,Qt6Network.dll,Qt6Core.dll | RUNNING |  |  |  |
| no-quick | Qt6Quick.dll | RUNNING |  |  |  |
| no-widgets | Qt6Widgets.dll | RUNNING |  |  |  |
