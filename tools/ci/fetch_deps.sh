#!/usr/bin/env bash
# macOS / Linux 依赖准备：安装 libmpv 与 libsodium 的运行时库。
# 编译只需要入库的 mpv 头文件，本脚本用于让程序真的能加载动态库。
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "工程目录：$root"

if [[ "$(uname -s)" == "Darwin" ]]; then
    command -v brew >/dev/null || { echo "未找到 Homebrew"; exit 1; }
    brew install mpv libsodium
    echo "macOS：mpv / libsodium 已通过 Homebrew 安装（动态链接，库在 /opt/homebrew 或 /usr/local/lib）"
else
    if command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install -y libmpv-dev libsodium23
    elif command -v dnf >/dev/null; then
        sudo dnf install -y mpv-libs libsodium
    elif command -v pacman >/dev/null; then
        sudo pacman -Syu --needed mpv libsodium
    else
        echo "未识别的包管理器，请手动安装 libmpv 与 libsodium" >&2
        exit 1
    fi
    echo "Linux：libmpv / libsodium 已安装（动态链接，qlibrary 会按 libsodium.so.23/26 查找）"
fi
