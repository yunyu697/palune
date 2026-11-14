# 发行与更新链路（单仓：yunyu697/palune）

更新时间：2026-11-14
适用版本：`1.0.0-RC1` 起（2026-11-14 RC 序号化；后缀规则见 §11）

## 1. 仓库关系（两仓 → 单仓）

| 仓库 | 可见性 | 内容 | 当前角色 |
|---|---|---|---|
| `yunyu697/palune` | **公开** | 源码、构建脚本、CI、文档、提交历史，以及全部发行物（安装包、`latest.json`、`SHA256SUMS`、安装验证证据） | **唯一发布仓** |
| `yunyu697/palune-releases` | 公开 / **已归档**（2026-11-08） | 历史发行物（`1.0.0-alpha6`、`1.0.0-RC`、`1.0.0-alpha8/9/10`） | 只作取证保留；**不再作为发布源**，不再产生新 Release |

```
yunyu697/palune ──(构建)──> 三平台产物 ──(GITHUB_TOKEN 写本仓)──> 本仓 Release
                                                                     │
客户端 ──匿名 GET latest.json（固定 URL）────────────────────────────┘
```

客户端更新源（唯一主链）：

```
https://github.com/yunyu697/palune/releases/latest/download/latest.json
```

- 该 URL 不需要任何凭据，客户端不带 `Authorization` 头。
- **分界时点：2026-11-08**。自该时点起清单只在源码仓生成与发布；旧仓地址不再更新（详见 §7）。
- **预发布口径**：带序号的 `-alphaN` / `-betaN` / `-RCn` tag 一律 `prerelease=true`，`releases/latest` 只指向正式版；裸后缀（`-alpha` / `-beta` / `-RC`）在 CI 直接失败（见 §11）。代价是预发布清单不在固定主链上，需走 tag 定点 URL：`https://github.com/yunyu697/palune/releases/download/<tag>/latest.json`。
- 当前本仓全部 Release 均为预发布，因此 **`releases/latest` 目前是 404**（实测），要等首个正式版（`1.0.0`）发布后才有主链。旧仓 `1.0.0-alpha6` 的 `Latest` 标记随归档保留在旧仓，不参与本仓主链。

## 2. 发布凭据

发布 job 写的是**本仓自身**，使用 GitHub Actions 自带的 `GITHUB_TOKEN`：

```yaml
permissions:
  contents: write
```

- 不再需要 `RELEASE_REPO_TOKEN`，也不再需要任何跨仓 PAT 或 GitHub App token。
- 历史遗留的 `RELEASE_REPO_TOKEN` Secret 已无任何引用点；如仍在仓库 Secrets 中，可安全删除（`gh secret delete RELEASE_REPO_TOKEN -R yunyu697/palune`）。
- 原先的"把 `RELEASE_REPO_TOKEN` 轮换为细粒度 PAT"待办随之作废。

更新签名密钥（Ed25519，签名 `latest.json` 与安装包，未变化）：

| Secret 名 | 说明 |
|---|---|
| `PALUNE_UPDATE_PRIVATE_KEY` | PEM 或其 base64，或 32 字节种子 base64。当前 alpha 阶段使用与 `resources/trust/update.pub` 配对的测试私钥（`tools/update/test-key.pem`，未入库） |
| `PALUNE_UPDATE_KEY_ID` | 清单签名的 `keyId`，当前 `palune-test` |

正式发布前必须换成离线保管的生产密钥，并同步替换 `resources/trust/update.pub`；轮换窗口内旧公钥仍需保留，否则已安装客户端无法验证新清单。

## 3. 发布流水线

`.github/workflows/release.yml`：

1. `version`：校验 `VERSION` 规则，取出通道；**按 tag 后缀**判定 `prerelease`（`-alphaN` / `-betaN` / `-RCn` 为预发布，裸后缀直接失败）。
2. `windows` / `macos` / `linux`：各自构建、跑相关单测、打包；Windows 额外做「安装 → 启动 → 卸载」验证。
3. `manifest`：生成 `latest.json` 与 `SHA256SUMS`，`--base-url` 指向本仓 `https://github.com/yunyu697/palune/releases/download/<tag>`；**断言**清单签名与每个产物签名都存在，未签名直接失败。
4. `publish`：用 `GITHUB_TOKEN` 把顶层资产发到**本仓** Release。`prerelease=true` 时带 `--prerelease`，不接管 `releases/latest`；只有正式 tag 才发普通 Release。
5. `assert-public-feed`：**不带任何凭据** GET 本仓清单（正式版走 `releases/latest/download/latest.json`，预发布走 `releases/download/<tag>/latest.json`）：
   - HTTP 必须 200，401/404 直接让流水线失败；
   - 必须能取到 `payload.version`，且等于本次发布版本；
   - 清单必须已签名；
   - 对清单里每个安装包 URL 做匿名 HEAD，必须 200。
   原始响应头会打印在日志里作为证据。

触发：推送 tag（`refs/tags/*`）。`workflow_dispatch` 只构建与生成清单，不发布。

## 4. 更新源、镜像与回退

### 4.1 清单（多源）

1. **直连**：`https://github.com/yunyu697/palune/releases/latest/download/latest.json`（beta 为 `latest-beta.json`）。
2. **内置镜像**：`https://ghfast.top/` + 原始清单 URL（仅 GitHub 主链启用）。候选来自 2026-11-08 实测：ghfast.top 返回 200 且清单验签通过；ghproxy.net / gh-proxy.com 在该出口仍 504，gh-proxy.net 返回 JS 挑战页，其余候选 404/429/525/证书过期/不可达，均未纳入。
3. **配置镜像**：环境变量 `PALUNE_UPDATE_MANIFEST_MIRRORS`，完整清单 URL，分号或逗号分隔；设置后**替代**内置镜像，供测试与自建源使用。

- **超时**：单源 15 s（ghfast 冷启动实测约 13 s），整轮 45 s；单源超时即换下一个。
- **逐源验签**：每个来源都必须用内置 `resources/trust/update.pub` 对 payload 做 Ed25519 验签；验签失败立即丢弃该来源并换下一个，全部失败才报 `UPD-004`。解析失败 `UPD-003`，HTTP 非 200 `UPD-002`，网络错误/超时 `UPD-001`。
- 每次尝试记录来源名、URL、HTTP 状态、耗时、响应字节数与 SHA-256（`manifestAttempts`），随 update-check 报告落盘。
- 取证：`docs/evidence/release-1.0.0-rc/manifest-multisource-probe.json`（直连注入失败 → 签名错误镜像被丢弃 → 下一镜像验签成功）。

### 4.2 安装包

- **下载通道顺序**：直连 → 清单 `mirrors[]`（可选，`gen_manifest.py --mirrors-file` 注入）→ 内置实测加速（同一代理前缀 + 原始资产 URL）。
- **失败自动切换**：网络错误/HTTP 错误或内容校验不通过时，丢弃当前通道、自动切到下一条；全部失败才报 `UPD-006/007`。
- **镜像内容三重验证**：任何通道下载到的包都要通过
  1) `SHA-256` 与清单一致；
  2) 清单 Ed25519 签名复核；
  3) 安装包条目自身的 Ed25519 签名。
  任一不通过即整包删除并切换通道，防止反代投毒。
- 镜像未配置且不适用内置加速时，客户端只有直连通道。

## 5. 发布失败的回退

| 故障 | 处置 |
|---|---|
| 某平台构建失败 | 修复后重跑整个 workflow；已成功的平台产物由 artifact 复用，不需要重发 |
| 资产上传中断 | `gh release upload <tag> <文件> --repo yunyu697/palune --clobber` 补传 |
| `latest.json` 内容错误（版本/哈希/签名） | 重新生成并 `--clobber` 覆盖；不要删除 Release |
| 新版本本身有问题，需要回退 | 把客户端更新源临时指向上一版本 tag 的固定 URL：`https://github.com/yunyu697/palune/releases/download/<上一个tag>/latest.json`；旧 Release 与资产不删除，已安装客户端仍能取到可用清单 |
| 匿名断言失败（401/404） | 先确认本仓仍是 Public、资产已上传；`releases/latest` 只在存在正式版时有效，预发布改为核对 `releases/download/<tag>/latest.json`；修复后重跑 `assert-public-feed` job |
| 发布凭据问题 | 本仓发布只用 `GITHUB_TOKEN`；检查 `permissions: contents: write` 与仓库 Actions 设置，无需任何外部 PAT |

回退原则：**只追加和覆盖，不删除历史 Release**；任何一次发布都必须能被「上一版可用清单」兜住。

## 6. 为什么客户端不能内嵌 token

1. 桌面客户端是可反编译的二进制；内嵌的只读 token 等价于公开 token。
2. 该 token 无法按用户吊销、无法限流、无法审计，一旦泄露可被用于抓取或滥用 API 配额。
3. 旧方案需要"私有源码仓 + 公开发行仓"两仓拆分；现在源码仓本身已公开，客户端不需要任何凭据即可匿名读取本仓 Release。
4. 正确边界：**需要凭据的写操作只发生在 CI**（使用作用域限于本仓的 `GITHUB_TOKEN`）；**客户端只做匿名只读**。

## 7. 旧仓归档与失效范围（2026-11-08）

旧仓 `yunyu697/palune-releases` 已通过 `PATCH /repos/{owner}/{repo}` 置 `archived=true`，**不删除**，作为发布历史取证保留。

归档后的实际状态（2026-11-08 匿名实测）：

| 链接 / 能力 | 结果 | 说明 |
|---|---|---|
| 旧仓 API `https://api.github.com/repos/yunyu697/palune-releases` | 200 | 仓库仍可读，但只读 |
| 旧仓 `releases/latest/download/latest.json` | 200（1,120 B，`version=1.0.0-alpha6`） | 停在归档时点，**不再更新** |
| 旧仓 `releases/download/1.0.0-RC/latest.json` | 200（1,297 B） | 历史资产仍可下载 |
| 旧仓 `releases/download/1.0.0-RC/Palune-1.0.0-win-x64.exe`（HEAD） | 200 | 历史资产仍可下载 |
| 新发布 / 新清单写入旧仓 | 不可能 | 归档 = 只读，且发布 job 已改为写本仓 |

因此"旧仓地址失效"的准确含义是：

- **作为发布源失效**：旧仓不会再产生任何新 Release、新清单或新安装包；
- **不是 404**：历史 Release 与资产在 GitHub 上仍可匿名下载（取证需要）；
- 仍指向旧仓清单地址的客户端只会一直读到 `1.0.0-alpha6`，永远看不到 `1.0.0-RC` 及之后的版本，表现为"更新不上"。

**本轮明确不做过渡**：不在旧仓保留指向新地址的签名清单，不做兼容跳转，也不改旧仓内容。老客户端（`1.0.0-alpha6` 及更早）的处置方式是**重装一次**新包；新构建的客户端默认清单地址已切到本仓。

## 8. 存量 Release 迁移记录（2026-11-08）

`1.0.0-RC` 与 `1.0.0-alpha8/9/10` 已在本仓按原样重建：资产名、字节、SHA-256、`prerelease=true` 与旧仓一致；先逐字节复核哈希，再归档旧仓。

| Release | 资产 | 字节数 | SHA-256 | GitHub `digest` 核对 |
|---|---|---|---|---|
| `1.0.0-RC` | `Palune-1.0.0-win-x64.exe` | 59,649,289 | `4f1cb5cadf4aa304ab2c53c7404ec984b38cdc532d57af8c6a16bde43917b82c` | 一致 |
| `1.0.0-RC` | `latest.json` | 1,297 | `b2d33ee7e16602a1ca75ca4efaeae7125054eb1c6372904b2211ef9b61d538da` | 一致 |
| `1.0.0-RC` | `SHA256SUMS` | 92 | `8ca3753cf388c7969c078a5201b2f44a97b464a807fe650993396f35dc9aaa7b` | 一致 |
| `1.0.0-RC` | `windows-install-evidence.json` | 1,991 | `2d255fd39e1ec6cc1333d9814e28c315b66d069c6a087e1dc19eba0ea111541f` | 一致 |
| `1.0.0-RC` | `install-launch.json` | 424 | `1a6de6ff731abe7692ca6ea63187fdfbf7f163903115636253dc7233936ff340` | 一致 |
| `1.0.0-alpha8` | `Palune-1.0.0-alpha8-win-x64.exe` | 59,218,560 | `f763585fa65613728dd15a5f11d18af1b9cac2dcb4e0c8d77a1d9bc1bf349d67` | 一致 |
| `1.0.0-alpha8` | `SHA256SUMS-1.0.0-alpha8` | 98 | `db58c0c2d5fdb312b1eb39b366b5d069f83621ca7c235887f05e8125f64649a6` | 一致 |
| `1.0.0-alpha9` | `Palune-1.0.0-alpha9-win-x64.exe` | 59,601,594 | `6e68fbd4f883d98d20d99bc92c6bb2f2472811c6532d0807a06a61f2fc66ca6f` | 一致 |
| `1.0.0-alpha9` | `SHA256SUMS-1.0.0-alpha9` | 98 | `3d05b44f3ee12209a683153da051a132e11ef2a4f44e0166a23b88df6af6c2c8` | 一致 |
| `1.0.0-alpha10` | `Palune-1.0.0-alpha10-win-x64.exe` | 59,604,798 | `d7b64b8cd76bbadf6fa0e8f2720b686d593a8d41431863310d9621ec1d326465` | 一致 |
| `1.0.0-alpha10` | `SHA256SUMS-1.0.0-alpha10` | 99 | `c043357385070db34fec515777dae26e686b651908957cf86a6236e03fa5a0b0` | 一致 |

迁移细节：

- 安装包取自迁移前的本地产物原件（与旧仓资产逐字节相同），小文件（清单/校验值/证据）从旧仓原资产取得；上传后 GitHub 侧 `digest` 与本地 SHA-256、旧仓 `digest` 三方一致。
- `1.0.0-RC` 的 tag 原先只存在于旧仓，本仓由 `gh release create --target main` 在 `bb90158` 上创建（迁移期间临时停用 Release 工作流，避免 tag 触发重建覆盖原资产）。
- 迁移期间 `1.0.0-alpha8/9/10` 使用本仓已有 tag，未新增 tag。
- 原始核对记录：`docs/evidence/release-migration-20261108.json`。

## 9. 1.0.0-RC 发布记录（原发布 + 迁移）

| 项 | 值 |
|---|---|
| Release | `1.0.0-RC`，`prerelease=true`，未接管 `releases/latest` |
| 清单 | `version=1.0.0`、`channel=stable`、`keyId=palune-test`；`--base-url .../releases/download/1.0.0-RC`（旧仓地址，随资产原样迁入；资产 URL 字段仍指向旧仓，历史清单不改写） |
| 验签 | `tools/release/verify_manifest.py` 用 `resources/trust/update.pub` 独立复算 SHA-256 并验 Ed25519，`PASS` |
| 发布方式 | 旧仓由本地 `gh release create` 发布；2026-11-08 迁入本仓 |
| 内容寻址 | GitHub 侧资产 `digest` 与本地 SHA-256 一致（exe `4f1cb5ca…b82c`） |
| 匿名可达（本仓） | `1.0.0-RC/latest.json` 与安装包匿名可达（200） |
| 未提供 | macOS/Linux 产物、MSI、生产签名 |

## 10. 源码仓 tag 现状（2026-11-08）

- 远端已补齐 `1.0.0-alpha8` / `alpha9` / `alpha10`；`1.0.0-RC` 已按 RC 序号规则改名为 `1.0.0-RC1`（旧 tag 已删除，旧链接 404）；`1.0.0` 暂不推送，等 RC 收口、P0 全闭合后按正式通道处理。
- **推送前必须先确认 tag 指向提交里的工作流版本**：GitHub 对 push 事件使用被推提交里的工作流文件。工作流修正只对之后的新提交生效。
- 本轮迁移时 `gh workflow disable Release` → 创建 Release → 核对无新 run → `gh workflow enable Release`；未触发任何重建。
- 2026-11-08 起 `publish` / `assert-public-feed` 均指向本仓，但仍**没有一次真实 tag 触发的端到端记录**；等 `1.0.0` 正式通道开启时一并验证。

## 11. 版本号规则（alpha / beta / RC）

唯一来源是仓库根目录的 `VERSION`：`PALUNE_VERSION` 宏、安装包文件名、应用内「关于」、`latest.json` 的 `payload.version`、`build-record.json` 与 README 必须与它逐字一致。

| 阶段 | 写法 | 编号范围 | 上限 | 推进方式 |
|---|---|---|---|---|
| 开发期 | `MAJOR.MINOR.PATCH-alphaN` | N = 1..10 | **10** | 只递增 N；`alpha10` 之后转 `beta1` |
| 测试期 | `MAJOR.MINOR.PATCH-betaN` | N = 1..10 | **10** | 只递增 N；`beta10` 之后转 `RC1` |
| 候选期 | `MAJOR.MINOR.PATCH-RCn` | n = 1, 2, 3 … **无上限** | **无上限** | 每次迭代递增 n（RC10 之后可继续 RC11、RC12…），通过后直接发正式版 |
| 正式版 | `MAJOR.MINOR.PATCH` | — | — | 当前只允许 `1.0.0` |

硬约束：

- **预发布必须带序号**：`1.0.0-RC`、`1.0.0-alpha`、`1.0.0-beta` 这类裸后缀一律不合规，不能用同一个 tag 反复顶替新一轮 RC；每次 RC 迭代都必须新 tag（`-RC2`、`-RC3`…）。
- **「除首位外每段 ≤ 10、不出现 11」只管 major.minor.patch**（`0.11.x` 这类越界段不允许）；这条**不约束 RC 计数**，RC 序号只要求是从 1 开始的正整数（无前导零）、无上限。
- **alpha / beta 仍是 1..10**，不随 RC 放开。
- semver 序：`1.0.0-RC1 < 1.0.0-RC2 < … < 1.0.0`。
- 预发布一律 `prerelease=true`，不接管 `releases/latest`；`-RCn` 不会被当成正式版。

执行点：

- `python tools/version/check_version.py`：裸后缀、alpha/beta 越界、版本段 >10、CI 里 tag 与 VERSION 不一致，都会 `[FAIL]` 并退出 1；`ci.yml` 与 `release.yml` 第一步都跑它。
- `release.yml` 的 `prerelease` 由 tag 后缀 `-alphaN` / `-betaN` / `-RCn` 判定；裸后缀 tag 直接 `::error::` 失败并退出。

### 11.1 `1.0.0-RC` → `1.0.0-RC1`（2026-11-14）

已发布的 `1.0.0-RC` 视作 RC1：tag 与 Release 改名为 `1.0.0-RC1`，安装包按 `VERSION=1.0.0-RC1` 重新构建（版本字符串进入 exe 文件名与 `latest.json`）。

**旧链接直接失效，不做兼容或跳转**：

- `releases/tag/1.0.0-RC`、`releases/download/1.0.0-RC/...` 均为 404；
- 旧 `latest.json`（`version=1.0.0`）里的资产 URL 指向旧 tag，随之失效；
- 已安装 `1.0.0-RC` 的用户重装 `1.0.0-RC1` 即可；
- 已归档旧仓 `yunyu697/palune-releases` 里的历史 `1.0.0-RC` Release 不受影响，仅作历史取证。

