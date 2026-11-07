# 发行与更新链路（私有源码 + 公开发行仓库）

更新时间：2026-11-07
适用版本：`1.0.0-alpha6` 起

## 1. 两个仓库及其关系

| 仓库 | 可见性 | 内容 | 谁可以访问 |
|---|---|---|---|
| `yunyu697/palune` | **私有** | 全部源码、构建脚本、CI、文档、提交历史 | 仅仓库协作者 |
| `yunyu697/palune-releases` | **公开** | 只有发行物：安装包、`latest.json`、`SHA256SUMS`、安装验证证据；**不含源码** | 所有人，匿名只读 |

关系：构建发生在私有源码仓库；发布 job 把产物**推到**公开发行仓库的 Release。发行仓库不参与构建，也不保存任何源码或密钥。

```
私有 palune ──(构建)──> 三平台产物 ──(带凭据跨仓推送)──> 公开 palune-releases
                                                             │
客户端 ──匿名 GET latest.json（固定 URL）───────────────────┘
```

客户端更新源（唯一主链）：

```
https://github.com/yunyu697/palune-releases/releases/latest/download/latest.json
```

- 该 URL 不需要任何凭据，客户端不带 `Authorization` 头。
- GitHub 的 `releases/latest` 只指向**非预发布**的最新 Release；因此 alpha 版本在发行仓库也以普通 Release 发布（不加 `--prerelease`），否则该 URL 会 404。
- 发行仓库的 Release 标题/说明会写明「源码在私有仓库」，不暴露源码地址细节以外的任何东西。

## 2. 跨仓库发布凭据

`.github/workflows/release.yml` 的 `publish` job 需要写入**另一个仓库**。GitHub Actions 自带的 `GITHUB_TOKEN` 作用域仅限当前仓库，**无法**跨仓库创建 Release，所以必须使用一个外部凭据，存放在私有源码仓库的 Actions Secret 中：

| Secret 名 | 类型 | 权限范围 | 存放位置 |
|---|---|---|---|
| `RELEASE_REPO_TOKEN` | 细粒度 PAT（Fine-grained personal access token）或 GitHub App installation token | 仅 `yunyu697/palune-releases` 一个仓库的 **Contents: Read and write**；不授予 Actions、Secrets、Administration 等任何其他权限；建议设置过期时间 | 私有仓库 `yunyu697/palune` 的 Settings → Secrets and variables → Actions |

配置命令（在私有仓库目录执行，token 从标准输入读入，不落盘、不进历史）：

```powershell
gh secret set RELEASE_REPO_TOKEN -R yunyu697/palune
```

> 该 Secret 只用于 CI 跨仓发布，**不会**进入源码、安装包或客户端。

更新签名密钥（Ed25519，签名 `latest.json` 与安装包）：

| Secret 名 | 说明 |
|---|---|
| `PALUNE_UPDATE_PRIVATE_KEY` | PEM 或其 base64，或 32 字节种子 base64。当前 alpha 阶段使用与 `resources/trust/update.pub` 配对的测试私钥（`tools/update/test-key.pem`，未入库） |
| `PALUNE_UPDATE_KEY_ID` | 清单签名的 `keyId`，当前 `palune-test` |

正式发布前必须换成离线保管的生产密钥，并同步替换 `resources/trust/update.pub`；轮换窗口内旧公钥仍需保留，否则已安装客户端无法验证新清单。

## 3. 发布流水线

`.github/workflows/release.yml`：

1. `version`：校验 `VERSION` 规则，取出通道。
2. `windows` / `macos` / `linux`：各自构建、跑相关单测、打包；Windows 额外做「安装 → 启动 → 卸载」验证。
3. `manifest`：生成 `latest.json` 与 `SHA256SUMS`；**断言**清单签名与每个产物签名都存在，未签名直接失败（避免再出现 `signature=null` 却被发布的情况）。
4. `publish`：用 `RELEASE_REPO_TOKEN` 把顶层资产推到 `palune-releases` 的 Release；缺 Secret 直接失败并给出明确提示。
5. `assert-public-feed`：**不带任何凭据** GET `releases/latest/download/latest.json`：
   - HTTP 必须 200，401/404 直接让流水线失败；
   - 必须能取到 `payload.version`，且等于本次发布版本；
   - 清单必须已签名；
   - 对清单里每个安装包 URL 做匿名 HEAD，必须 200。
   原始响应头会打印在日志里作为证据。

触发：推送 tag（`refs/tags/*`）。`workflow_dispatch` 只构建与生成清单，不发布。

## 4. 更新源与镜像

- **清单与签名始终走主链**：`latest.json` 只从发行仓库的 `releases/latest/download/` 取，镜像不参与。
- **镜像只用于安装包下载**：`latest.json` 的 `mirrors[]`（可选，用 `gen_manifest.py --mirrors-file` 注入）给出安装包下载前缀。
- **失败自动切换**：下载请求失败（网络错误/HTTP 错误）或内容校验不通过时，丢弃当前通道、自动切到下一条，顺序为 直连 → 镜像 1 → 镜像 2；全部失败才报 `UPD-006/007`。
- **镜像内容双验证**：任何通道下载到的包都要通过
  1) `SHA-256` 与清单一致；
  2) 清单 Ed25519 签名复核；
  3) 安装包条目自身的 Ed25519 签名。
  任一不通过即整包删除并切换通道，防止反代投毒。
- 镜像未配置时不发出 `mirrors` 字段，客户端只有直连通道。

## 5. 发布失败的回退

| 故障 | 处置 |
|---|---|
| 某平台构建失败 | 修复后重跑整个 workflow；已成功的平台产物由 artifact 复用，不需要重发 |
| 资产上传中断 | `gh release upload <tag> <文件> --repo yunyu697/palune-releases --clobber` 补传 |
| `latest.json` 内容错误（版本/哈希/签名） | 重新生成并 `--clobber` 覆盖；不要删除 Release |
| 新版本本身有问题，需要回退 | 把客户端更新源临时指向上一版本 tag 的固定 URL：`.../releases/download/<上一个tag>/latest.json`；旧 Release 与资产不删除，已安装客户端仍能取到可用清单 |
| 匿名断言失败（401/404） | 先确认发行仓库仍是 Public、Release 不是 prerelease、资产已上传；修复后重跑 `assert-public-feed` job |
| 跨仓凭据失效/过期 | 重新签发只含 `contents:write` 的 PAT，更新 `RELEASE_REPO_TOKEN`，重跑 `publish` 与 `assert-public-feed` |

回退原则：**只追加和覆盖，不删除历史 Release**；任何一次发布都必须能被「上一版可用清单」兜住。

## 6. 为什么客户端不能内嵌 token

1. 桌面客户端是可反编译的二进制；内嵌的只读 token 等价于公开 token。
2. 该 token 无法按用户吊销、无法限流、无法审计，一旦泄露可被用于抓取或滥用 API 配额。
3. 私有仓库的 Release 资产即使带 token 下载，也需要对每个用户分发同一份长期凭据，与「私有」初衷矛盾。
4. 正确边界：**需要凭据的写操作只发生在 CI**（`RELEASE_REPO_TOKEN` 只存在于私有仓库 Secret）；**客户端只访问匿名可读的公开发行仓库**。发行仓库公开只是「二进制公开」，源码仍在私有仓库。

## 7. 已知边界

- 当前 alpha 阶段签名使用测试密钥，仅用于验证链路；生产密钥需离线保管并轮换公钥。
- 发行仓库公开意味着任何人对安装包可匿名下载，这符合预期；源码、CI 配置、密钥材料均不在其中。
