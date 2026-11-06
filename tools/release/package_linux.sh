#!/usr/bin/env bash
# Linux 打包：AppImage + deb + rpm（三份产物都来自同一个 AppDir，避免重复部署 Qt 运行时）。
#
# 需要外部工具：linuxdeploy + linuxdeploy-plugin-qt（脚本内下载，带缓存目录）、
# dpkg-deb、rpmbuild（CI 里用 apt 安装）。
#
# 用法：OUT=dist bash tools/release/package_linux.sh
set -euo pipefail

VERSION="$(tr -d '\r\n' < VERSION)"
BUILD_DIR="${BUILD_DIR:-build}"
OUT="${OUT:-dist}"
TOOLS="${TOOLS:-${BUILD_DIR}/release-tools}"
APPDIR="${APPDIR:-${BUILD_DIR}/AppDir}"
ARCH=x86_64

mkdir -p "$OUT" "$TOOLS"

# ---------- 1. 组装 AppDir ----------
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/palune"
cp "${BUILD_DIR}/Palune" "$APPDIR/usr/bin/palune"
cp packaging/palune.desktop "$APPDIR/usr/share/applications/"
cp packaging/palune-256.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/palune.png"
cp README.md NOTICE.md VERSION "$APPDIR/usr/share/palune/"
if [ -f third_party/libmpv/libmpv.so.2 ]; then
    cp third_party/libmpv/libmpv.so.2 "$APPDIR/usr/lib/"
fi

# ---------- 2. 下载 linuxdeploy ----------
fetch_appimage() {
    local url="$1" dest="$2"
    if [ ! -x "$dest" ]; then
        curl -fsSL --retry 3 -o "$dest" "$url"
        chmod +x "$dest"
    fi
}
fetch_appimage "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
               "$TOOLS/linuxdeploy-x86_64.AppImage"
fetch_appimage "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
               "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
ln -sf "$(basename "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage")" "$TOOLS/linuxdeploy-plugin-qt"

export ARCH
export PATH="$PWD/$TOOLS:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1
# OUTPUT 必须是最终文件路径；指向目录会被 mksquashfs 当成非法目标。
export OUTPUT="$PWD/$OUT/Palune-${VERSION}-linux-x86_64.AppImage"

LD_ARGS=(--appdir "$APPDIR" --plugin qt --output appimage)
if [ -f third_party/libmpv/libmpv.so.2 ]; then
    # libmpv 是 dlopen 加载，linuxdeploy 探测不到，必须显式声明。
    LD_ARGS=(--appdir "$APPDIR" -l "$PWD/third_party/libmpv/libmpv.so.2" --plugin qt --output appimage)
fi
"$TOOLS/linuxdeploy-x86_64.AppImage" "${LD_ARGS[@]}"

# linuxdeploy 会把产物直接写到当前目录，统一改名后收进 dist。
shopt -s nullglob
for f in *.AppImage; do
    mv "$f" "$OUT/Palune-${VERSION}-linux-x86_64.AppImage"
done
shopt -u nullglob
if [ ! -f "$OUT/Palune-${VERSION}-linux-x86_64.AppImage" ]; then
    echo "[FAIL] 未生成 AppImage" >&2
    exit 1
fi

# ---------- 3. 从 AppDir 组装 deb / rpm ----------
PKGROOT="${BUILD_DIR}/pkgroot"
rm -rf "$PKGROOT"
mkdir -p "$PKGROOT/opt/palune" "$PKGROOT/usr/bin" \
         "$PKGROOT/usr/share/applications" "$PKGROOT/usr/share/icons/hicolor/256x256/apps"
for sub in bin lib plugins qml share translations resources; do
    [ -d "$APPDIR/usr/$sub" ] && cp -a "$APPDIR/usr/$sub" "$PKGROOT/opt/palune/" || true
done
install -m 0755 packaging/linux/palune-launcher.sh "$PKGROOT/usr/bin/palune"
cp packaging/palune.desktop "$PKGROOT/usr/share/applications/"
cp packaging/palune-256.png "$PKGROOT/usr/share/icons/hicolor/256x256/apps/palune.png"

DEB_ROOT="${BUILD_DIR}/debroot"
rm -rf "$DEB_ROOT"
cp -a "$PKGROOT" "$DEB_ROOT"
mkdir -p "$DEB_ROOT/DEBIAN"
INSTALLED_KB="$(du -sk "$DEB_ROOT" | cut -f1)"
cat > "$DEB_ROOT/DEBIAN/control" <<EOF
Package: palune
Version: ${VERSION}
Section: video
Priority: optional
Architecture: amd64
Installed-Size: ${INSTALLED_KB}
Maintainer: Palune <support@palune.app>
Description: Palune 媒体播放器
 基于 Qt/QML 与 libmpv 的媒体播放器，动态链接 libmpv。
EOF
dpkg-deb --build --root-owner-group "$DEB_ROOT" "$OUT/Palune-${VERSION}-linux-x86_64.deb"

if command -v rpmbuild >/dev/null 2>&1; then
    RPM_TOP="${BUILD_DIR}/rpmbuild"
    rm -rf "$RPM_TOP"
    mkdir -p "$RPM_TOP"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    cat > "$RPM_TOP/SPECS/palune.spec" <<EOF
Name: palune
Version: 1.0.0
Release: 0.palune${VERSION#1.0.0}
Summary: Palune media player
License: LGPL-3.0-or-later
BuildArch: x86_64
AutoReqProv: no

%description
Palune 媒体播放器（Qt/QML + 动态链接 libmpv）。

%install
cp -a "${PWD}/${PKGROOT}/." "%{buildroot}/"

%files
/opt/palune
/usr/bin/palune
/usr/share/applications/palune.desktop
/usr/share/icons/hicolor/256x256/apps/palune.png
EOF
    rpmbuild -bb --quiet --define "_topdir ${PWD}/${RPM_TOP}" \
             --define "_binary_payload w9.gzdio" "$RPM_TOP/SPECS/palune.spec"
    find "$RPM_TOP/RPMS" -name '*.rpm' -exec cp {} "$OUT/Palune-${VERSION}-linux-x86_64.rpm" \;
else
    echo "[WARN] 未安装 rpmbuild，跳过 rpm" >&2
fi

echo "[OK] Linux 产物："
ls -lh "$OUT"
