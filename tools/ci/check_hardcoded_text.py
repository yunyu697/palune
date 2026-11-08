#!/usr/bin/env python3
"""QML 硬编码文案检查：用户可见字符串必须包在 qsTr() 内。"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QML_DIRS = [ROOT / "qml"]
CJK = re.compile("[\u4e00-\u9fff]")
LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')
VISIBLE = re.compile(
    r'(text|label|title|placeholderText|tooltipText|displayText|description)\s*:\s*'
    r'("(?:[^"\\]|\\.)*")'
)

# 品牌名与纯数字/符号标签不参与翻译。
# 另含：非文本符号（窗口按钮、占位破折号、加减号、乘号、分隔点）；
# nits 为 HDR 亮度单位；UTF-8 / GB18030 是编码名；English 是语言名（惯例不译）。
WHITELIST = {
    "PALUNE", "Palune ", "Qt ", "-10s", "+10s", "-10", "+10", "x", "%", "",
    "—", "−", "\\u2014", "\\u2715", "×", "  · ",
    "600 nits", "800 nits", "1000 nits", "UTF-8", "GB18030", "English",
}
NEUTRAL = re.compile(r"^[0-9+\-.,:%/ ]*$")
COLOR = re.compile(r"^#[0-9a-fA-F]{3,8}$")


def qml_files() -> list[Path]:
    files: list[Path] = []
    for base in QML_DIRS:
        files.extend(sorted(base.rglob("*.qml")))
    return files


def main() -> int:
    hits: list[str] = []
    for path in qml_files():
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith("//"):
                continue
            for match in LITERAL.finditer(line):
                literal = match.group(0)
                if not CJK.search(literal):
                    continue
                if line[:match.start()].rstrip().endswith("qsTr("):
                    continue
                hits.append(f"{path.relative_to(ROOT)}:{lineno}: 未本地化：{literal[:60]}")
            for match in VISIBLE.finditer(line):
                literal = match.group(2)
                value = literal[1:-1]
                if line[:match.start(2)].rstrip().endswith("qsTr("):
                    continue
                if value in WHITELIST or NEUTRAL.match(value) or COLOR.match(value):
                    continue
                hits.append(f"{path.relative_to(ROOT)}:{lineno}: 未本地化：{literal[:60]}")

    print(f"已检查 QML 文件：{len(qml_files())}")
    if hits:
        print(f"命中 {len(hits)} 处硬编码用户可见文案：")
        for hit in hits:
            print("  " + hit)
        return 1
    print("命中 0 处：硬编码文案检查通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
