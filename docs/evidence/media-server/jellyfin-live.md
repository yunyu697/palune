# 媒体服务器适配器实机验证（Jellyfin 协议族）

日期：2026-11-13
版本口径：`1.0.0-RC`

## 结论先行

- **Jellyfin**：真机跑通「连接 → 认证 → 媒体库列表 → 海报 → 直链播放 → 进度回写」全链路，证据见下。
- **Emby / 极空间 / 飞牛**：与 Jellyfin 共用 `JellyfinAdapter` 的同一套协议路径，已用同一台真实 Jellyfin 换 `Kind` 验证（`otherJellyfinKinds`），**没有**在真实 Emby / 极空间 / 飞牛设备上验证。
- **Plex**：适配器代码已落地（PIN 登录、资源发现、媒体库、时间线、scrobble），**本轮没有真实 Plex 服务器**，所以 PIN 与播放链路均未实机验证；Plex Pass 限制写在代码注释与本文末尾。
- 视频画面本身没有截图：取证机桌面会话处于锁屏（前台窗口 pid=0，`WindowFromPoint` 命中 `LockScreenBackstopFrame`），mpv 的视频 surface 走 GPU 渲染，`PrintWindow` 只能得到黑帧。播放事实改由**服务端会话与 UserData** 证明，不依赖截图。

## 服务端

- 版本：Jellyfin `10.11.11`（Windows x64 便携包 `jellyfin_10.11.11-amd64.zip`，167,773,527 B）
- 地址：`http://127.0.0.1:8096`
- 账号：`palune`（本地启动向导创建）
- 媒体库：Movies，真实文件由 ffmpeg 生成并放在 `_scratch/jellyfin/media/Movies`：
  - `Palune Long Movie (2026).mp4`，180 s（低于续播门槛，用于对照）
  - `Palune Resume Movie (2026).mp4`，360 s（高于续播门槛）
  - `Palune Test Movie (2026).mp4`，20 s
  - 每个条目带 `poster.jpg`，由 Jellyfin 本地图片提供方识别

复现步骤（PowerShell）：

```powershell
# 1) 启动（会话由流量较小的背景 shell 持有）
jellyfin.exe --datadir <data> --webdir <jellyfin-web>
# 2) 首次向导
POST /Startup/Configuration  {"UICulture":"en-US","MetadataCountryCode":"US","PreferredMetadataLanguage":"en"}
POST /Startup/User           {"Name":"palune","Password":"palune123"}
POST /Startup/Complete
# 3) 认证
POST /Users/AuthenticateByName
     X-Emby-Authorization: MediaBrowser Client="Palune", Device="Windows", DeviceId="palune-desktop-1", Version="1.0.0"
# 4) 建库并扫描
POST /Library/VirtualFolders?name=Movies&collectionType=movies&refreshLibrary=true&paths=<urlencoded>
POST /Library/Refresh
```

## 已断言的事实

### 1) 适配器层（可重复执行）

命令：

```powershell
$env:PALUNE_LIVE_JELLYFIN_URL="http://127.0.0.1:8096"
$env:PALUNE_LIVE_JELLYFIN_USER="palune"
$env:PALUNE_LIVE_JELLYFIN_PASS="palune123"
build\Release\tst_adapter_live.exe
```

结果（完整输出见 `live-adapter-test.txt`）：

```
PASS   : TstAdapterLive::jellyfinEndToEnd()
PASS   : TstAdapterLive::otherJellyfinKinds()
Totals: 4 passed, 0 failed, 0 skipped
```

`jellyfinEndToEnd` 断言链：

| 步骤 | 断言 |
| --- | --- |
| 认证 | `AuthResult.ok`，token / userId 非空，`/System/Info` 探到版本 |
| 媒体库 | `fetchItems` 无错误且至少 1 项，时长 > 0 |
| 海报 | `GET /Items/{id}/Images/Primary` 返回 200，字节 > 1 KB，JPEG/PNG magic |
| 播放 | `playbackUrl` 返回 `/Videos/{id}/stream...`，`PlaybackMode::DirectPlay`，Range GET 拿得到媒体字节 |
| 进度 | 上报 60 s 后，服务端 `UserData.PlaybackPositionTicks` ≈ 60 s，`LastPlayedDate` 非空 |

未设环境变量时该测试 `QSKIP`，不影响 CI。

### 2) 应用内（真实 UI 链路）

`qa/runtime/probe_media_server.py`：写入已认证会话 → `Palune.exe --ui-panel library` → 截图海报墙 → 点第二张卡片播放 → 等 20 s → 读服务端会话与 UserData。

- 海报墙：`app-library-posters.png`，3 个真实服务器条目，标题、时长、海报都由 Jellyfin 返回。
- 点开播放：窗口标题变为 `Palune — stream?static=true&api_key=...&MediaSourceId=...`，日志 `开始播放：http://127.0.0.1:8096/Videos/.../stream?...`
- 进度上报（应用日志）：
  - `播放进度回写：3.2s（progress）`
  - `播放进度回写：13.2s（progress）`
  - `播放进度回写：23.5s（progress）`
- 服务端会话（`GET /Sessions`）读到：
  ```json
  {"client":"Palune","item":"Palune Resume Movie (2026)",
   "itemId":"1689938400b922d7cfb5a3ff3ac697c9",
   "positionTicks":235000000,"playMethod":"DirectPlay"}
  ```
- 服务端 UserData（`GET /UserItems/{id}/UserData`）：
  ```json
  {"PlaybackPositionTicks":235000000,"PlayedPercentage":6.53,
   "Played":false,"LastPlayedDate":"2026-11-13T07:51:30Z"}
  ```
  23.5 s 与日志最后一条完全一致，证明位置确实写回了服务器，而不是本地状态。

## 过程中修正的两个真问题

1. **URL 被当成 Windows 路径**：`MpvClient::openFile` 原先对所有输入做 `QFileInfo(path).absoluteFilePath()`，把 `http://127.0.0.1:8096/Videos/...` 改成 `http:\127.0.0.1\...`，mpv 报「打不开」。现在 `contains("://")` 的地址原样交给 mpv。
2. **Jellyfin 只把进度落到「已开始」的会话**：只发 `/Sessions/Playing/Progress` 时，位置只进内存 playstate，不进 UserData。适配器现在对每个 `sessionId` 先发一次 `/Sessions/Playing`，再发 Progress；停止时发 `/Sessions/Playing/Stopped`。

另一个实测到的服务端约束：Jellyfin 默认 `MinResumeDurationSeconds=300`，短于 5 分钟的条目不会保存续播位置，只记「已看」。真机样例片因此使用 360 s 素材，测试里也显式断言时长 ≥ 300 s，避免把服务端策略误判成客户端缺陷。

## 尚未验证 / 无法验证

| 项 | 状态 | 原因 |
| --- | --- | --- |
| 真实 Emby 服务器 | 未验证 | 只有 Jellyfin 实例；Emby 类型走同一 `JellyfinAdapter`，仅验证了换 `Kind` 后认证与拉库仍通 |
| 真实极空间 / 飞牛设备 | 未验证 | 手上没有设备；按「其他 Jellyfin」复用同一路径，协议差异（若厂商改动接口）未覆盖 |
| 应用内视频画面截图 | 取不到 | 会话锁屏 + GPU 渲染 surface，`PrintWindow` 只能得到黑帧；改用服务端会话/位置取证 |
| 退出时 stop 事件 | 未验证 | 探针最后 `WM_CLOSE` 未能让进程退出（`closed=false`），随后被强制结束；进度仍由最后一次 progress 落库（23.5 s） |
| 设置页连接表单 | **已实现（真机取证）** | 设置 →「媒体库」页含地址、类型（Jellyfin / Emby / 其他 Jellyfin，注明极空间与飞牛选后者）、用户名密码或 API Key、测试连接、保存、删除、状态行；证据见下节 |
| token 安全存储 | **已实现（真机取证）** | token 只进系统凭据存储（Windows Credential Manager / macOS Keychain / libsecret）；旧明文 token 自动迁移并从 `settings.toml` 删除；证据见下节 |
| Plex 全链路 | 未验证 | 无 Plex 服务器与 Plex 账号；PIN 起点已实测，见 `plex-probe.md` |

## 设置页「连接媒体服务器」表单（2026-11-13）

入口：设置 →「媒体库」页。控件：地址、类型（Jellyfin / Emby / 其他 Jellyfin）、登录方式（用户名密码 / API Key）、测试连接、保存、删除、状态行（已连接 / 认证失败 / 不可达）。

实机取证脚本：`qa/runtime/probe_server_connect_ui.py`（窗口不抢焦点、移到屏幕外，`PrintWindow` 回读）。一次批处理完成三段：

| 段 | 断言 | 截图 |
| --- | --- | --- |
| A 成功链路 | 空表单 → 填真实 Jellyfin → 测试连接 → 保存 → `settings.toml` 写入 `kind/url/userId/user`，**无明文 token**，Credential Manager 出现 `Palune/media-server`，日志出现拉库记录 | `server-form-empty.png`、`server-form-filled.png`、`server-test-ok.png`、`server-saved.png` |
| B 重启 + 删除 + 两个失败态 | 重启后表单回填且再次拉库；删除后配置与凭据同时清除；地址错 → `状态：无法连接（不可达）：Connection refused`；密码错 → `状态：认证失败（HTTP 401）：Host requires authentication` | `server-restart-prefilled.png`、`server-deleted.png`、`server-fail-unreachable.png`、`server-fail-auth.png` |
| C 明文 token 迁移 | 预置旧配置 `media.server.token` → 启动 → 配置文件中该键被删除、凭据中出现 token、日志记录迁移、媒体库正常拉取 | `server-migrated-library.png` |

机器可读结果：`server-connect-probe.json`。要点：

- `save.configHasPlaintextToken=false`，`save.credentialPresent=true`，`save.logFetch=["媒体服务器媒体库：3 项（jellyfin）"]`
- `restart` 段两条拉库日志，`configHasPlaintextToken=false`（重启仍生效）
- `delete.configHasServer=false`、`delete.credentialPresent=false`
- 迁移：`beforeHasPlaintextToken=true` → `afterHasPlaintextToken=false`、`migrationLogged=true`、`libraryLoaded=true`

## Plex 本轮状态与限制

代码：`src/media/PlexAdapter.h/.cpp`，已编译进 `palune_core`。

- PIN 登录：`POST https://plex.tv/api/v2/pins?strong=true` 取 `id/code`，轮询 `GET /api/v2/pins/{id}?X-Plex-Token={code}` 取 `authToken`。
- 资源发现：`GET /api/v2/resources?includeHttps=1&includeRelay=1`，优先本地连接；只有远端/中继时 `remoteOnly=true`。
- 媒体库：`/library/sections` → `/library/sections/{key}/all`，XML 解析 `Video/Part/Stream`。
- 进度：`/:/timeline`（playing）与 `/:/scrobble`（stop）。
- **Plex Pass 限制**：远端播放（含中继）需要服务器端 Plex Pass，且账号需有相应权限；本地直连不受此限。适配器在 `remoteOnly` 时把该限制写进 `capabilities` 的语义，UI 需据此提示，否则用户会看到「能拉库、点开播不动」。
- **已实测（无账号 / 无服务器）**：PIN 创建 `HTTP 201`（id / code / expiresIn=1800）、PIN 轮询未授权时 `authToken=null`、资源发现无效 token 被 `401/1001` 拒绝、缺 `X-Plex-Client-Identifier` 被 `400/1000` 拒绝。详见 `plex-probe.md` 与 `plex-pin.json`、`plex-resources-unauth.json`。
- **仍未验证**：授权完成后的 `authToken`、服务器资源列表、媒体库 XML、`/:/timeline` / `/:/scrobble`。没有真实 Plex 服务器，这一项不能计入已完成。

## 复现入口

```powershell
python qa\runtime\probe_media_server.py      # 应用内取证，输出本目录截图与 app-probe.json
build\Release\tst_adapter_live.exe           # 适配器断言（需 PALUNE_LIVE_JELLYFIN_*）
```
