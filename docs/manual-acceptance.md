# 人工验收清单（无法自动断言 / 必须真人确认的项）

日期：2026-11-13
适用版本：`1.0.0-RC`

> 进入本清单只代表“自动化无法在不打扰用户的前提下给出结论”，不代表已通过。
> 每项须由验收人勾选并写明日期与机器环境；未勾选项不得计入已完成。

## 1. 外部程序与系统集成

- [ ] **打开许可文件（`openLicense`）**：设置 → 关于 → 打开许可，系统默认查看器成功打开安装目录的 `NOTICE.md`，内容为 Palune 许可与开源声明。
  自动化只能断言到“路径解析正确、进程发起了打开请求、日志有记录”；**外部查看器是否真的拉起、是否显示正确内容，无法在不弹出窗口的前提下断言**，故列为人工验收。
- [ ] **外部字幕文件关联打开**：双击/右键用 Palune 打开 `.srt`/`.ass`，应用加载对应字幕轨道。
- [ ] **托盘行为**：开启“关闭/最小化到托盘”后，托盘图标出现、菜单“退出”能真正结束进程。

## 2. 媒体服务器（真机与真实账号）

- [ ] **真实 Emby 服务器**：连接 → 认证 → 拉库 → 播放 → 进度回写。自动化只在真实 Jellyfin 上换 `Kind` 验证了同一协议路径。
- [ ] **真实极空间设备**：类型选「其他 Jellyfin」，完成连接与播放。
- [ ] **真实飞牛设备**：类型选「其他 Jellyfin」，完成连接与播放。
- [ ] **Plex 全链路（保持“未完成”）**：需要真实 Plex 账号 + Plex 服务器；验证 PIN 授权完成后的 `authToken`、资源列表、媒体库 XML、`/:/timeline` 与 `/:/scrobble`。
      远端/中继播放还需服务器端 **Plex Pass**；无 Plex Pass 时只能验证本地直连。
- [ ] **大量媒体库分页**：>2000 条目时海报墙滚动、缩略图缓存上限淘汰表现。

## 3. 系统凭据存储（跨平台）

- [ ] **macOS Keychain**：保存媒体服务器连接后，`security find-generic-password -s Palune -a media-server` 能读到条目，`settings.toml` 无 token。
- [ ] **Linux libsecret**：`secret-tool search service Palune` 能读到条目，`settings.toml` 无 token。
- 说明：本轮只在 Windows Credential Manager 上做了真机取证（见 `docs/evidence/media-server/server-connect-probe.json`）；macOS/Linux 代码路径已实现但未在本机验证。

## 4. 视频画面与播放

- [ ] **应用内视频画面截图**：取证机会话锁屏 + GPU 渲染 surface，`PrintWindow` 只能得到黑帧；需在正常桌面会话下人工确认画面、HDR 映射与字幕渲染。
- [ ] **退出时 stop 事件**：探针 `WM_CLOSE` 未能让进程退出，需人工关闭窗口后确认服务端收到 `/Sessions/Playing/Stopped` 且进度落库。

## 5. 安装包与失败态

- [ ] **真实安装目录缺 `platforms/qwindows.dll` 的提示**：装完后人为把 `bin\platforms` 改名/删除再启动，确认是中文提示（含“安全软件信任安装目录”），且 `palune.log` 有 `[ERROR] 启动自检失败`；自动化已在等价部署树上验证，见 `docs/evidence/dialogs/`。
- [ ] **安全软件拦截安装**：在开启安全软件（360/火绒/Defender 受控文件夹访问等）的干净机器上安装，确认安装结束时若关键文件缺失会弹中文提示并生成 `install-check.log`，补信任后重装可恢复。自动化无法安全制造隔离，故列为人工验收。
- [ ] **安装 → 启动 → 卸载**：在干净 Windows 用户环境复演（已有自动化记录，但属机器相关）。
- [ ] **构建树直接运行**：直接双击 `build\Release\Palune.exe`（不自带 PATH 里的 Qt）应正常启动——本轮已用 POST_BUILD windeployqt 让构建树自带 `platforms\`，需在另一台没装 Qt 的开发机复演一次。
