#!/usr/bin/env python3
"""生成 256×256 应用图标 PNG（纯标准库，避免引入额外依赖）。

用法：python tools/release/gen_icon.py packaging/palune-256.png
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

SIZE = 256
BG = (16, 18, 24)          # 深色底，与 QML 主题 #0b0c10 接近
ACCENT = (94, 234, 212)    # 青绿色强调色
RADIUS = 44


def rounded_mask(x: int, y: int) -> bool:
    """圆角矩形遮罩，越界返回 False。"""
    inner = SIZE - 1 - x
    inner_y = SIZE - 1 - y
    cx = min(x, inner)
    cy = min(y, inner_y)
    if cx < RADIUS and cy < RADIUS:
        dx, dy = RADIUS - cx, RADIUS - cy
        return dx * dx + dy * dy <= RADIUS * RADIUS
    return True


def inside_triangle(x: int, y: int) -> bool:
    """居中的播放三角：顶点 (100,70)-(100,186)-(196,128)。"""
    def side(ax, ay, bx, by, px, py):
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax)
    x1, y1 = 100, 70
    x2, y2 = 100, 186
    x3, y3 = 196, 128
    d1 = side(x1, y1, x2, y2, x, y)
    d2 = side(x2, y2, x3, y3, x, y)
    d3 = side(x3, y3, x1, y1, x, y)
    return (d1 >= 0 and d2 >= 0 and d3 >= 0) or (d1 <= 0 and d2 <= 0 and d3 <= 0)


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "packaging/palune-256.png")
    out.parent.mkdir(parents=True, exist_ok=True)

    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)  # 过滤类型 None
        for x in range(SIZE):
            if not rounded_mask(x, y):
                raw.extend((0, 0, 0, 0))
            elif inside_triangle(x, y):
                raw.extend((*ACCENT, 255))
            else:
                raw.extend((*BG, 255))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    out.write_bytes(png)
    print(f"[OK] 已生成图标 {out} ({len(png)} 字节)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
