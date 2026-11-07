#!/usr/bin/env python3
"""生成 Palune 应用图标（方向 A「折角 Fold」，纯标准库实现）。

设计依据：temp_20261106_04/docs/DESIGN.md §9.4。
- 满幅底板 + 右上角折起；折面提亮，折痕与折厚承担识别。
- 不使用字母、播放三角、圆环、胶片齿孔、喇叭与投影幕。
- Windows / Linux 满幅直角；不内建投影。

用法：
    python tools/release/gen_icon.py packaging/palune-256.png
同时会在同目录输出 packaging/palune.ico（16/24/32/48/64/256）。
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

BOARD = (231, 178, 124)   # #E7B27C 折角基底（Cinema 琥珀）
FOLD = (246, 211, 174)    # #F6D3AE 折面（提亮）
SLOT = (12, 14, 17)       # #0C0E11 缺角处露出的深色底（与设计稿 a-512-win-dark 一致）
ICO_SIZES = (16, 24, 32, 48, 64, 256)


def render(size: int) -> bytes:
    """返回 size×size RGBA 像素（行优先，不带宽高头）。

几何照 DESIGN.md §9.4 方向 A：46% 折角，右上角整块填深色底（SLOT），
折面是 46% 见方的亮三角，折线由 (W-46%, 0) 连到 (W, 46%)。
缺角不能留透明：Windows 满幅直角图标在浅色任务栏上会露出底板缺口，
设计稿 a-512-win-dark 也是填 #0C0E11 的。
"""
    px = bytearray(size * size * 4)
    fold = size * 0.46
    x0 = size - fold                  # 折面左边界

    def put(x: int, y: int, rgb: tuple[int, ...]) -> None:
        i = (y * size + x) * 4
        px[i:i + 4] = bytes(rgb)

    for y in range(size):
        for x in range(size):
            if x >= x0 and y <= fold:
                # 折面：折线 y = x - x0 的左上侧；右下侧是缺角，填深色底。
                if y >= x - x0:
                    put(x, y, (*FOLD, 255))
                else:
                    put(x, y, (*SLOT, 255))
            else:
                put(x, y, (*BOARD, 255))
    return bytes(px)


def png_bytes(size: int) -> bytes:
    raw = bytearray()
    data = render(size)
    stride = size * 4
    for y in range(size):
        raw.append(0)  # 过滤类型 None
        raw.extend(data[y * stride:(y + 1) * stride])

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def dib_bytes(size: int) -> bytes:
    """ICO 条目用的 32bpp DIB（rc.exe 不接受 PNG 压缩条目）。"""
    data = render(size)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(size - 1, -1, -1):          # DIB 自下而上
        row = data[y * size * 4:(y + 1) * size * 4]
        for x in range(size):
            r, g, b, a = row[x * 4:x * 4 + 4]
            xor.extend((b, g, r, a))
    mask_stride = ((size + 31) // 32) * 4
    and_mask = bytes(mask_stride * size)        # 全不透明
    return header + bytes(xor) + and_mask


def ico_bytes() -> bytes:
    entries = []
    blobs = []
    offset = 6 + 16 * len(ICO_SIZES)
    for size in ICO_SIZES:
        blob = dib_bytes(size)
        dim = 0 if size >= 256 else size
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset))
        offset += len(blob)
        blobs.append(blob)
    header = struct.pack("<HHH", 0, 1, len(ICO_SIZES))
    return header + b"".join(entries) + b"".join(blobs)


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "packaging/palune-256.png")
    out.parent.mkdir(parents=True, exist_ok=True)
    png = png_bytes(256)
    out.write_bytes(png)
    ico = out.with_name("palune.ico")
    ico.write_bytes(ico_bytes())
    print(f"[OK] 图标已生成：{out} ({len(png)} 字节)、{ico}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
