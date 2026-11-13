# Plex 适配器：独立一轮（PIN 起点 + 资源发现鉴权）

日期：2026-11-13
版本口径：`1.0.0-RC`

## 结论先行

- **能证明**：Plex 的 PIN 创建、PIN 轮询、资源发现三个端点在本机真实可达；响应字段与适配器解析路径一致；无效 token 被 401 拒绝；缺少 `X-Plex-Client-Identifier` 被 400 拒绝，而适配器始终携带该头。
- **不能证明**：真实账号授权完成后的 `authToken`、服务器资源列表内容、媒体库 XML、播放与 `/:/timeline` / `/:/scrobble`。这些都需要一个真实 Plex 账号 + 真实 Plex Media Server，本机没有。
- 因此 Plex **不计入完成**，只记录到「PIN 起点已实测、播放链路全部未验证」。

## 实测记录

环境：Windows，Anaconda `curl.exe 8.x`，网络出口广州。

### 1) PIN 创建（真实请求）

```powershell
curl.exe -X POST "https://plex.tv/api/v2/pins?strong=true" `
  -H "X-Plex-Product: Palune" -H "X-Plex-Version: 1.0.0" `
  -H "X-Plex-Client-Identifier: palune-desktop-1" `
  -H "X-Plex-Platform: Windows" -H "X-Plex-Device: Desktop" `
  -H "Accept: application/json"
```

结果：`HTTP 201`，关键字段 `id=1334938708`、`code=<redacted>`、`expiresIn=1800`、`authToken=null`。
落盘：`plex-pin.json`。

对应代码：`PlexAdapter::createPin()`（`src/media/PlexAdapter.cpp`），解析 `id` / `code`，生成 `https://app.plex.tv/auth#?clientID=...&code=...`。

### 2) PIN 轮询（未授权状态）

```powershell
curl.exe "https://plex.tv/api/v2/pins/1334938708?X-Plex-Token=<code>" <同组头>
```

结果：`HTTP 200`，`authToken` 仍为 `null`，与 `pollPin()` 的「尚未在浏览器完成授权」分支一致。
落盘：`plex-pin.json` 的 `poll` 字段。

### 3) 资源发现鉴权（无效 token）

```powershell
curl.exe "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1" <同组头 + X-Plex-Token: invalid>
```

结果：`HTTP 401`，`{"code":1001,"message":"User could not be authenticated"}`。
对照：去掉 `X-Plex-Client-Identifier` 后为 `HTTP 400`，`code=1000 X-Plex-Client-Identifier is missing`。
落盘：`plex-resources-unauth.json`。

对应代码：`PlexAdapter::listServers()`，`plexHeaders()` 已带 `X-Plex-Client-Identifier: palune-desktop-1`，失败走 `resp.ok()` 的 error 分支。

## Plex Pass 限制（必须写清楚）

- **本地直连播放**：不要求服务器端 Plex Pass。
- **远端 / 中继（Relay）播放**：要求**服务器端**拥有 Plex Pass，并且发起账号有对应权限；否则能拉到媒体库、能出海报，但点开会播不动或很快 401/403。
- 适配器在 `remoteOnly` 时把该限制写进 `capabilities` 语义（`src/media/PlexAdapter.h` 头注释），UI 需据此提示，不能把「库能拉」当成「能播」。
- 这一条无法在本机验证：没有 Plex 服务器，也没有可用 Plex Pass。

## 未验证清单

| 项 | 状态 | 原因 |
| --- | --- | --- |
| 浏览器授权后的 `authToken` | 未验证 | 需要真实 Plex 账号完成 `plex.tv/link` 授权 |
| `/api/v2/resources` 返回的服务器列表 | 未验证 | 需要上述 `authToken` |
| `/library/sections` 与 `/library/sections/{key}/all` XML 解析 | 未验证 | 需要真实 Plex Media Server |
| `/:/timeline`、`/:/scrobble` 回写 | 未验证 | 同上；远端播放另受 Plex Pass 限制 |
| 远端/中继被 Plex Pass 拦截时的 UI 提示 | 未实现验证 | 依赖真实受限服务器复现 |

## 复现入口

见 `jellyfin-live.md`「复现入口」；Plex 部分为上面的三条 `curl.exe` 命令，响应已落盘到本目录。
