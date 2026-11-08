#!/usr/bin/env python3
"""从唯一图标源 SVG 导出各平台位图与对照图。

唯一源文件：resources/brand/wave-clay-dark.svg（形状与配色不得改动）。
本脚本只做三件事：
1. 用 Qt Svg 栅格化（tools/brand/svg_raster.cpp 编译出的 svg_raster）出各尺寸 PNG；
2. 打包 Windows .ico（16/24/32/48/64/128/256 全部 DIB 条目，rc.exe 不接受 PNG 条目）
   与 macOS .icns（16/32/128/256/512 的 1x 与 2x）；
3. 出 16px 实际像素放大图与三尺寸对照图，供深色任务栏/浅色背景可辨性核对。

16/24/32 三个小尺寸允许最小限度的对齐与线宽补偿（否则 0.5 单位描边在 16px 下
只有 0.08 像素，外框整条丢失；条带上下边又会落在半像素上糊成一片）：
- 外框描边改成正好 1 像素（含内嵌偏移，外形轮廓线位置不变）；
- 条带的上下边吸附到像素栅格（skewX 不改变水平边方向，吸附真实有效）。
除此之外不动任何几何与配色。

用法：
    python tools/brand/build_icons.py --rasterizer build/brand/Release/svg_raster.exe
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SVG_NS = "http://www.w3.org/2000/svg"
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
ICNS_SIZES = (16, 32, 128, 256, 512)
PNG_SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
# 需要做小尺寸补偿的尺寸：放大到 32 以上时半像素误差已不可见。
SMALL_SIZES = (16, 24, 32)

# ICNS 类型码：1x / 2x（retina）成对出现，覆盖 16/32/128/256/512 的两倍图。
ICNS_TYPES = {
    16: b"icp4",
    32: b"icp5",
    128: b"ic07",
    256: b"ic08",
    512: b"ic09",
    1024: b"ic10",
}
ICNS_RETINA_TYPES = {
    32: b"ic11",   # 16 @2x
    64: b"ic12",   # 32 @2x
    256: b"ic13",  # 128 @2x
    512: b"ic14",  # 256 @2x
}


def svg_variant(source: Path, size: int, work: Path) -> Path:
    """返回该尺寸应使用的 SVG：大于 32 直接复用源文件，小尺寸做上述对齐/线宽补偿。"""
    if size not in SMALL_SIZES:
        return source

    ET.register_namespace("", SVG_NS)
    tree = ET.parse(source)
    root = tree.getroot()
    rects = [el for el in root.iter(f"{{{SVG_NS}}}rect")]
    if len(rects) != 4:
        raise SystemExit(f"图标源结构变化（期望 4 个 rect，实得 {len(rects)}），请人工确认")

    step = 100.0 / size  # 1 像素对应多少文档单位
    scale = size / 100.0

    def fmt(value: float) -> str:
        return f"{value:.3f}".rstrip("0").rstrip(".")

    def snap(value: float) -> float:
        return round(value / step) * step

    # 底板：描边改成正好 1 像素并内嵌，外轮廓仍精确落在 0..100。
    back = rects[0]
    back.set("x", fmt(step / 2))
    back.set("y", fmt(step / 2))
    back.set("width", fmt(100 - step))
    back.set("height", fmt(100 - step))
    back.set("stroke-width", fmt(step))

    # 三条条带：上下边吸附像素栅格（skewX 不改变水平边方向，吸附真实有效）。
    for bar in rects[1:]:
        y = snap(float(bar.get("y")))
        height = snap(float(bar.get("y")) + float(bar.get("height"))) - y
        bar.set("y", fmt(y))
        bar.set("height", fmt(height))

    # 条带宽度/间距做成整数像素：16px 下原宽 1.76px、间距 0.96px，直接缩会糊成一团。
    # 按「条带整数宽 + 间隙整数宽 + 整体保持居中」重排，只改线宽与横向位置，不改造型。
    bars = rects[1:]
    bar_w = round(float(bars[0].get("width")) * scale)
    gap = round(6 * scale)  # 源里相邻条带间距 6 单位
    bar_w = max(1, bar_w)
    gap = max(1, gap)
    total = bar_w * len(bars) + gap * (len(bars) - 1)
    start = math.floor(100 * scale / 2 - total / 2 + 0.5)  # 以画布中心为基准取整
    tan8 = math.tan(math.radians(8))  # 源里 skewX(-8)
    for index, bar in enumerate(bars):
        screen_x = start + index * (bar_w + gap)
        y_mid = (float(bar.get("y")) + float(bar.get("height")) / 2) * scale
        # 反解 skewX(-8) + translate(7 0)：x_local = x_screen - 7 + tan8 * y
        x_local = (screen_x / scale) - 7 + tan8 * (y_mid / scale)
        bar.set("x", fmt(x_local))
        bar.set("width", fmt(bar_w / scale))

    variant = work / f"variant-{size}.svg"
    tree.write(variant, encoding="utf-8", xml_declaration=True)
    return variant


def rasterize(rasterizer: Path, svg: Path, out_png: Path, size: int) -> None:
    out_png.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [str(rasterizer), str(svg), str(out_png), str(size)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"栅格化失败 {size}px：{result.stderr.strip()}")


def dib_bytes(image: Image.Image) -> bytes:
    """ICO 条目用的 32bpp DIB：自下而上 BGRA，后接 1bpp AND 掩码（1=透明）。"""
    rgba = image.convert("RGBA")
    size = rgba.size[0]
    pixels = rgba.load()

    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = pixels[x, y]
            xor.extend((b, g, r, a))

    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)
    for y in range(size):
        row = (size - 1 - y) * mask_stride
        for x in range(size):
            if pixels[x, y][3] < 8:
                mask[row + (x // 8)] |= 0x80 >> (x % 8)
    return header + bytes(xor) + bytes(mask)


def write_ico(images: dict[int, Image.Image], out: Path) -> None:
    entries = []
    blobs = []
    offset = 6 + 16 * len(ICO_SIZES)
    for size in ICO_SIZES:
        blob = dib_bytes(images[size])
        dim = 0 if size >= 256 else size
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset))
        offset += len(blob)
        blobs.append(blob)
    out.write_bytes(struct.pack("<HHH", 0, 1, len(ICO_SIZES)) + b"".join(entries) + b"".join(blobs))


def write_icns(pngs: dict[int, Path], out: Path) -> None:
    chunks = []
    for size, kind in ICNS_TYPES.items():
        chunks.append(kind + struct.pack(">I", 8 + len(pngs[size].read_bytes())) + pngs[size].read_bytes())
    for size, kind in ICNS_RETINA_TYPES.items():
        chunks.append(kind + struct.pack(">I", 8 + len(pngs[size].read_bytes())) + pngs[size].read_bytes())
    body = b"".join(chunks)
    out.write_bytes(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def font(size: int):
    for name in ("msyh.ttc", "segoeui.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def zoom(image: Image.Image, factor: int) -> Image.Image:
    return image.resize((image.width * factor, image.height * factor), Image.NEAREST)


BG_DARK = "#202020"
BG_LIGHT = "#F3F3F3"


def paste_on_tile(sheet: Image.Image, image: Image.Image, x: int, y: int, color: str,
                  padding: int = 8) -> None:
    """把图标贴到指定底色方块上，方块比图标外扩 padding。"""
    draw = ImageDraw.Draw(sheet)
    draw.rounded_rectangle([x - padding, y - padding, x + image.width + padding,
                            y + image.height + padding],
                           radius=6, fill=color, outline="#4A4A4A")
    sheet.paste(image, (x, y), image)


def compose_16px_zoom(png16: Path, out: Path) -> None:
    """16px 实际像素按 16 倍最近邻放大，深色任务栏与浅色背景各一组。"""
    icon = Image.open(png16).convert("RGBA")
    factor = 16
    big = zoom(icon, factor)
    pad = 16
    panel_w = big.width + pad * 2
    panel_h = big.height + pad * 2 + 52
    width = panel_w * 2 + pad * 3
    height = panel_h + 116
    sheet = Image.new("RGBA", (width, height), (12, 14, 17, 255))
    draw = ImageDraw.Draw(sheet)
    title = font(22)
    sub = font(15)

    for index, (color, text) in enumerate(((BG_DARK, "深色任务栏 #202020"),
                                          (BG_LIGHT, "浅色背景 #F3F3F3"))):
        x = pad + index * (panel_w + pad)
        draw.rounded_rectangle([x, pad, x + panel_w, pad + panel_h], radius=10,
                               fill="#171A1E", outline="#2E3338")
        paste_on_tile(sheet, big, x + pad, pad + pad, color)
        # 同一底色上再放一枚真实 1:1 大小的图标，避免把「放大图」误读成实际像素。
        real_y = pad + panel_h - 44
        paste_on_tile(sheet, icon, x + pad + 4, real_y, color, 4)
        draw.text((x + pad + 32, real_y + 2), "1:1 真实大小", font=sub, fill="#9A9A9A")
        draw.text((x + 4, pad + panel_h + 12), text, font=title, fill="#E6E6E6")
        draw.text((x + 4, pad + panel_h + 42), "主图为 16×16 逐像素放大 16 倍", font=sub,
                  fill="#9A9A9A")
        draw.text((x + 4, pad + panel_h + 64), "面板下方小图为真实 1:1 大小", font=sub,
                  fill="#9A9A9A")

    sheet.convert("RGB").save(out)


def compose_sizes(pngs: dict[int, Path], out: Path) -> None:
    """16/24/32 三尺寸对照 + 48/64 过渡 + 512/1024 预览，深色与浅色各一行。"""
    factors = {16: 12, 24: 10, 32: 8, 48: 6, 64: 5}
    pad = 16
    label_h = 26
    cells = []
    for size in (16, 24, 32, 48, 64):
        image = Image.open(pngs[size]).convert("RGBA")
        big = zoom(image, factors[size])
        cells.append((size, big))

    cell_w = max(big.width for _, big in cells) + pad * 2
    row_h = max(big.height for _, big in cells) + pad * 2
    width = cell_w * len(cells) + pad * 2
    height = label_h + row_h * 2 + pad * 3 + 340
    sheet = Image.new("RGBA", (width, height), (12, 14, 17, 255))
    draw = ImageDraw.Draw(sheet)
    title = font(20)
    sub = font(14)

    for index, (size, big) in enumerate(cells):
        x = pad + index * cell_w
        draw.text((x, pad), f"{size}px · 放大 {factors[size]}×", font=title, fill="#E6E6E6")
        y = pad + label_h
        paste_on_tile(sheet, big, x + pad, y + pad, BG_DARK)
        paste_on_tile(sheet, big, x + pad, y + row_h + pad, BG_LIGHT)

    preview_y = pad + label_h + row_h * 2 + pad * 2
    draw.text((pad, preview_y), "512 / 1024 备用 PNG 预览（等比缩放显示）", font=title,
              fill="#E6E6E6")
    preview_x = pad
    for size in (512, 1024):
        image = Image.open(pngs[size]).convert("RGBA").resize((256, 256), Image.LANCZOS)
        paste_on_tile(sheet, image, preview_x + pad, preview_y + 30 + pad, BG_DARK)
        draw.text((preview_x + pad, preview_y + 30 + 256 + pad * 2), f"palune-{size}.png",
                  font=sub, fill="#9A9A9A")
        preview_x += 256 + pad * 2

    draw.text((preview_x + pad, preview_y + 40),
              "16/24/32 已做补偿：外框描边正好 1 像素、条带上下边吸附像素栅格、\n"
              "条带宽度与间隙取整像素（整体仍居中）；造型与配色未改动。",
              font=sub, fill="#9A9A9A", spacing=6)
    sheet.convert("RGB").save(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="从品牌 SVG 导出各平台图标")
    parser.add_argument("--rasterizer", required=True, help="svg_raster 可执行文件路径")
    parser.add_argument("--root", default=".", help="仓库根目录")
    parser.add_argument("--qt-bin", default="", help="Qt bin 目录（提供 Qt6Svg.dll 等）")
    parser.add_argument("--no-evidence", action="store_true", help="跳过对照图")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    brand = root / "resources" / "brand"
    source = brand / "wave-clay-dark.svg"
    if not source.is_file():
        raise SystemExit(f"缺少唯一图标源：{source}")

    rasterizer = Path(args.rasterizer).resolve()
    work = brand / ".build"
    work.mkdir(parents=True, exist_ok=True)

    env_path = str(rasterizer.parent)
    if args.qt_bin:
        import os
        env_path = args.qt_bin + ";" + env_path
    import os
    os.environ["PATH"] = env_path + ";" + os.environ.get("PATH", "")

    png_dir = brand / "png"
    png_paths: dict[int, Path] = {}
    for size in PNG_SIZES:
        variant = svg_variant(source, size, work)
        out_png = png_dir / f"palune-{size}.png"
        rasterize(rasterizer, variant, out_png, size)
        png_paths[size] = out_png

    images = {size: Image.open(png_paths[size]) for size in ICO_SIZES}
    write_ico(images, brand / "palune.ico")
    write_icns(png_paths, brand / "palune.icns")

    # Windows 资源脚本与 Qt 窗口图标沿用 packaging 下的固定路径。
    packaging = root / "packaging"
    shutil.copyfile(brand / "palune.ico", packaging / "palune.ico")
    shutil.copyfile(png_paths[256], packaging / "palune-256.png")

    if not args.no_evidence:
        shots = root / "docs" / "evidence" / "shots" / "alpha9-icon"
        shots.mkdir(parents=True, exist_ok=True)
        compose_16px_zoom(png_paths[16], shots / "icon-16px-zoom.png")
        compose_sizes(png_paths, shots / "icon-sizes-compare.png")

    shutil.rmtree(work, ignore_errors=True)
    ico = (brand / "palune.ico").stat().st_size
    icns = (brand / "palune.icns").stat().st_size
    print(f"[OK] PNG {len(PNG_SIZES)} 个，ICO {ico} 字节，ICNS {icns} 字节")
    return 0


if __name__ == "__main__":
    sys.exit(main())
