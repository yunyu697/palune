# 更新链路验证（alpha6，公开发行仓库）

日期：2026-11-07
发行仓库：<https://github.com/yunyu697/palune-releases>
Release：<https://github.com/yunyu697/palune-releases/releases/tag/1.0.0-alpha6>

## 1. 结论

| 检查项 | 结果 | 证据文件 |
|---|---|---|
| 匿名 GET `releases/latest/download/latest.json` | **200**，`payload.version=1.0.0-alpha6`，已签名（keyId `palune-test`） | `update-chain-alpha6.json`、`assert-public-pass.log.txt` |
| 匿名下载安装包 | **200**，59,210,046 B | `update-chain-alpha6.json` |
| SHA-256 与清单一致 | **PASS**（`2d146581fa1b…`） | `update-chain-alpha6.json` |
| 清单 Ed25519 签名 | **PASS**（内置公钥 `resources/trust/update.pub`） | `update-chain-alpha6.json` |
| 安装包 Ed25519 签名 | **PASS** | `update-chain-alpha6.json` |
| 客户端真实检查（不带凭据） | `state=uptodate`，`availableVersion=1.0.0-alpha6`，无错误码 | `report-public-feed.json` |
| 安装 → 启动 → 卸载 | **PASS**（退出码 0，快捷方式 3 个，卸载后目录/快捷方式清空） | `windows-install-evidence.json`、`install-launch.json` |
| CI 断言的负向用例 | 私有仓库 URL 返回 **404** → 脚本 **exit 1** | `assert-private-fail.log.txt` |
| 镜像被投毒（哈希不符） | 自动切换：镜像 A → 镜像 B，最终包哈希 = 镜像 B | `report-mirror-poison.json` |
| 镜像 404 | 自动切换：镜像 A → 镜像 B | `report-mirror-404.json` |

## 2. 匿名请求的原始响应（latest.json）

请求：`GET https://github.com/yunyu697/palune-releases/releases/latest/download/latest.json`
请求头：仅 `User-Agent: Palune-Release-Audit/1.0`、`Cache-Control: no-cache`；**没有 `Authorization`**。

- 首跳：`302 Location: .../releases/download/1.0.0-alpha6/latest.json`（GitHub 的 latest 别名）
- 最终：**HTTP 200**，落到 `objects.githubusercontent.com`（Azure Blob）

响应头节选（完整列表见 `update-chain-alpha6.json` 的 `feed.headers`）：

```
HTTP/1.1 200 OK
Content-Length: 1120
Content-Type: application/octet-stream
Content-Disposition: attachment; filename=latest.json
Last-Modified: Sat, 07 Nov 2026 10:49:42 GMT
ETag: "0x8DF3CBCD42BEE00"
x-ms-blob-type: BlockBlob
x-ms-server-encrypted: true
Cache-Control: no-cache
X-GitHub-Request-Id: D44B:31947F:E95CDB:173041D:6AEF02CA
```

## 3. 安装包匿名下载与校验

| 项 | 值 |
|---|---|
| URL | `https://github.com/yunyu697/palune-releases/releases/download/1.0.0-alpha6/Palune-1.0.0-alpha6-win-x64.exe` |
| HTTP | 200（跟随重定向后返回 Blob，`Content-Length: 59210046`） |
| 实测 SHA-256 | `2d146581fa1b0519eb8a3c6ffb96e78c39f5be5fb602977ffa09dcc1133eb981` |
| 清单声明 SHA-256 | 同上，一致 |
| 清单签名 | `Ed25519`，含公钥验签通过 |
| 安装包签名 | `Ed25519`，对整包字节验签通过 |

发布包与本地验证包是**同一个文件**：`dist/Palune-1.0.0-alpha6-win-x64.exe` 直接上传，未重新打包；本地已对该文件完成「安装 → 启动 → 卸载」验证（`windows-install-evidence.json`，`verdict=PASS`）。

## 4. CI 匿名可访问断言（正向 + 负向）

`release.yml` 的 `assert-public-feed` job 与本地验证脚本同逻辑（`curl -sSL`，不带凭据）：

- 正向（公开发行仓库）：`HTTP 200` → 取到 `version=1.0.0-alpha6` → 已签名 → 安装包匿名 `HEAD 200` → **exit 0**（`assert-public-pass.log.txt`）
- 负向（私有源码仓库同路径）：`HTTP 404` → 打印 `::error::更新源必须匿名可访问：HTTP 404（401/404 均视为失败）` → **exit 1**（`assert-private-fail.log.txt`）

## 5. 镜像逻辑

前提：`latest.json` 与签名主链只走发行仓库直连，镜像只用于安装包下载。

**投毒用例**（`report-mirror-poison.json`）：
- 清单 `mirrors` = 镜像 A（内容被篡改）、镜像 B（正确内容）
- 客户端先走镜像 A，SHA-256 不符：
  `下载通道「镜像 A」失败（SHA-256 不匹配：期望 fef5bf93…，实际 bcef7fca…），自动切换到「镜像 B」`
- 镜像 B 下载完成、校验通过；最终文件哈希 `fef5bf93…`，与镜像 B 一致，被投毒内容未落盘。

**HTTP 失败用例**（`report-mirror-404.json`）：
- 镜像 A 返回 404：
  `下载通道「镜像 A」失败（... server replied: File not found），自动切换到「镜像 B」`
- 镜像 B 成功，`state=installing`。

两个用例都证明：请求失败与内容校验失败都会自动切换，且镜像内容必须同时过 SHA-256 与签名校验。

## 6. 客户端真实链路

用 alpha6 本机二进制执行：

```powershell
Palune.exe --update-check `
  --update-url https://github.com/yunyu697/palune-releases/releases/latest/download/latest.json `
  --report report-public-feed.json
```

结果：`state=uptodate`、`availableVersion=1.0.0-alpha6`、`errorCode` 为空、`mirror=直连`。若清单未签名或签名不符，客户端会返回 `UPD-004`；此处无错误码，说明匿名取清单与验签都通过。

## 7. 已知边界

- 本次 alpha6 为**本机 Qt 6.8.3 构建**，只发布了 Windows 安装包；macOS/Linux 与 MSI 需要在 `RELEASE_REPO_TOKEN` 配置后由 CI 三平台构建补齐。CI 构建（Qt 6.9.3）与本机产物字节不同，届时必须重新跑匿名断言并重新验证哈希。
- `RELEASE_REPO_TOKEN` 尚未配置，`publish` job 尚未在 CI 中真实执行；当前 release 由本机用已授权的 `gh` 凭据一次性发布，未创建任何新 token。
- 签名使用 alpha 测试密钥（公钥已内置于应用）；stable 前需轮换为离线生产密钥。
