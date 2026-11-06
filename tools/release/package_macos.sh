#!/usr/bin/env bash
# macOS 打包：安装树 -> 修复 libmpv 依赖 -> 生成 dmg。
#
# 前置：third_party/libmpv/libmpv.dylib 已由工作流用 brew 的 mpv 准备好。
# 用法：OUT=dist bash tools/release/package_macos.sh
set -euo pipefail

VERSION="$(tr -d '\r\n' < VERSION)"
BUILD_DIR="${BUILD_DIR:-build}"
OUT="${OUT:-dist}"
STAGE="${BUILD_DIR}/stage"
mkdir -p "$OUT"

rm -rf "$STAGE"
cmake --install "$BUILD_DIR" --prefix "$STAGE" --config Release

APP="$STAGE/Palune.app"
if [ ! -d "$APP" ]; then
    echo "[FAIL] 未找到 Palune.app" >&2
    exit 1
fi

# libmpv 是 dlopen 加载，macdeployqt 不会处理它依赖的 ffmpeg 等动态库，
# 用 dylibbundler 把这些库拷进 .app 并改写 install_name。
if [ -f "$APP/Contents/MacOS/libmpv.dylib" ] && command -v dylibbundler >/dev/null 2>&1; then
    mkdir -p "$APP/Contents/Frameworks"
    dylibbundler -of -b -x "$APP/Contents/MacOS/libmpv.dylib" \
                 -d "$APP/Contents/Frameworks" -p "@executable_path/../Frameworks" >/dev/null
fi

codesign --force --deep --sign - "$APP" 2>/dev/null || echo "[WARN] 临时签名失败（未配置证书）" >&2

DMG="$OUT/Palune-${VERSION}-macos-arm64.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "Palune ${VERSION}" -srcfolder "$APP" \
               -fs HFS+ -format UDZO -ov "$DMG"

echo "[OK] macOS 产物："
ls -lh "$OUT"
