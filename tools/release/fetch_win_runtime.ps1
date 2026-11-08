# 下载 Windows 运行时依赖：libmpv-2.dll（shinchiro 的 mpv-dev 构建）与 libsodium.dll。
#
# 为什么用固定 URL 而不是 api.github.com/repos/.../releases/latest：
#   CI 从共享 runner IP 匿名调 GitHub REST API 会撞上未认证限流（60 次/小时/IP），
#   2026-11-08 那次失败就是 API 返回了 "We couldn't respond to your request in time"
#   的 HTML 错误页，导致 Invoke-RestMethod 解析失败、整条 Windows 矩阵红。
#   换成 release 资产的直链后不再经过 API，也就不受限流影响；版本同时被钉死，
#   构建可复现（同一 tag 的资产内容不会变）。
#
# 升级方式：改下面三个常量（tag / 资产名 / sha256），CI 缓存 key 跟着脚本内容变，
# 旧缓存自动失效。sha256 取自 GitHub Release 资产页的 digest 字段，下载后逐字节校验。
param(
    [string]$Root = "",
    # 只下载并校验，不写 third_party（预检用）。
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
if (-not $Root) { $Root = (Get-Location).Path }

# ---- 钉死的版本（升级时同步改这里）----
# 20261106 是本地 1.0.0 / 1.0.0-RC 安装包实际使用的 mpv-dev 构建，
# 钉在这里保证 CI 打包与已发布产物用同一份运行时。
$MpvTag = "20261106"
$MpvAsset = "mpv-dev-x86_64-20261106-git-9028187538.7z"
$MpvSha256 = "b5a3cc91b5dabf88ad5bb21d000834da4fd0bbda30f4584d39c082127bd62414"
$SodiumAsset = "libsodium-1.0.22-msvc.zip"
$SodiumSha256 = "3e03a726fac4bc09cb61d8f29d658ef7a5eca0811de59082130414f7ca2e4279"
# 压缩包里同时有 Win32/x64、Debug/Release、v142/v143 多份 libsodium.dll，
# 按路径或顺序取第一份会拿到 Debug 版（1.25MB，与已发布包里的 Release 版 349KB 不同）。
# 直接按 DLL 的 SHA256 挑，保证 CI 与本地包逐字节一致。
$SodiumDllSha256 = "f1b56eb55a86edee37a2614c8bd38272ac325ec6fa6fa7a4088a4d1a42815c4c"

$MpvUrl = "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/$MpvTag/$MpvAsset"
$SodiumUrl = "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/$SodiumAsset"

$mpvDir = Join-Path $Root "third_party\libmpv"
$sodiumDir = Join-Path $Root "third_party\libsodium"

function Assert-7z {
    if (-not (Get-Command 7z -ErrorAction SilentlyContinue)) {
        # windows-latest 自带 7-Zip；本地缺的话给出明确提示而不是让解压步骤莫名失败。
        throw "未找到 7z（7-Zip）。Windows runner 自带；本地请先装 7-Zip 并加入 PATH。"
    }
}

# 单次下载 + SHA256 校验，最多 3 次；指数退避。全部失败才抛出。
function Get-PinnedFile {
    param(
        [Parameter(Mandatory)] [string] $Uri,
        [Parameter(Mandatory)] [string] $OutFile,
        [Parameter(Mandatory)] [string] $Sha256,
        [Parameter(Mandatory)] [string] $Label,
        [int] $Attempts = 3,
        [int] $TimeoutSec = 300
    )
    $expected = $Sha256.ToLowerInvariant()
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        $tmp = "$OutFile.part"
        Remove-Item -Force $tmp -ErrorAction SilentlyContinue
        try {
            Write-Output "  [$Label] 下载（$attempt/$Attempts）：$Uri"
            # 重试逻辑全部在这一层：Invoke-WebRequest 的 -MaximumRetryCount 只在 PS 7.4+
            # 可用，本地跑旧版 PowerShell 会直接报参数不存在。
            Invoke-WebRequest -Uri $Uri -OutFile $tmp -TimeoutSec $TimeoutSec `
                -Headers @{ "User-Agent" = "palune-release-ci" }
            $actual = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $expected) {
                throw "SHA256 不符：期望 $expected，实际 $actual"
            }
            Move-Item -Force $tmp $OutFile
            Write-Output "  [$Label] 校验通过（sha256=$expected）"
            return
        } catch {
            Write-Warning "  [$Label] 第 $attempt 次失败：$($_.Exception.Message)"
            if ($attempt -eq $Attempts) {
                throw "[$Label] 下载/校验失败 $Attempts 次，放弃：$Uri"
            }
            Start-Sleep -Seconds (5 * $attempt)
        } finally {
            Remove-Item -Force $tmp -ErrorAction SilentlyContinue
        }
    }
}

New-Item -ItemType Directory -Force -Path $mpvDir, $sodiumDir | Out-Null

# ---- libmpv ----
if (Test-Path (Join-Path $mpvDir "libmpv-2.dll")) {
    Write-Output "libmpv-2.dll 已存在，跳过（如需更新请先删除 third_party/libmpv/libmpv-2.dll）"
} else {
    Assert-7z
    $archive = Join-Path $env:RUNNER_TEMP $MpvAsset
    Get-PinnedFile -Uri $MpvUrl -OutFile $archive -Sha256 $MpvSha256 -Label "libmpv"
    $extract = Join-Path $env:RUNNER_TEMP "mpv-dev"
    Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    & 7z x -y "-o$extract" $archive | Out-Null
    $dll = Get-ChildItem -Recurse -Filter "libmpv-2.dll" $extract | Select-Object -First 1
    if (-not $dll) { throw "mpv-dev 包里没有 libmpv-2.dll（$MpvAsset）" }
    if ($VerifyOnly) {
        Write-Output "预检通过：libmpv-2.dll 来自 $MpvAsset"
    } else {
        Copy-Item $dll.FullName (Join-Path $mpvDir "libmpv-2.dll") -Force
        Write-Output "已就绪 libmpv-2.dll（$MpvAsset）"
    }
}

# ---- libsodium ----
if (Test-Path (Join-Path $sodiumDir "libsodium.dll")) {
    Write-Output "libsodium.dll 已存在，跳过"
} else {
    Assert-7z
    $zip = Join-Path $env:RUNNER_TEMP $SodiumAsset
    Get-PinnedFile -Uri $SodiumUrl -OutFile $zip -Sha256 $SodiumSha256 -Label "libsodium"
    $extract = Join-Path $env:RUNNER_TEMP "libsodium"
    Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    & 7z x -y "-o$extract" $zip | Out-Null
    $expectedDll = $SodiumDllSha256.ToLowerInvariant()
    $dll = Get-ChildItem -Recurse -Filter "libsodium.dll" $extract |
        Where-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedDll } |
        Select-Object -First 1
    if (-not $dll) {
        throw "libsodium 压缩包里没有 sha256=$expectedDll 的 libsodium.dll（$SodiumAsset）"
    }
    if ($VerifyOnly) {
        Write-Output "预检通过：libsodium.dll 来自 $SodiumAsset"
    } else {
        Copy-Item $dll.FullName (Join-Path $sodiumDir "libsodium.dll") -Force
        Write-Output "已就绪 libsodium.dll（$SodiumAsset）"
    }
}

Write-Output "依赖就绪：$mpvDir / $sodiumDir"
