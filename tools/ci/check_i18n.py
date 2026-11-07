#!/usr/bin/env python3
"""i18n 完整性检查：QML 的 qsTr 源串都能在英文翻译里找到且已完成。"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from xml.etree import ElementTree

ROOT = Path(__file__).resolve().parents[2]
TS_FILE = ROOT / "i18n" / "palune_en_US.ts"
QM_FILE = ROOT / "i18n" / "palune_en_US.qm"
LITERAL = re.compile(r'qsTr\(("(?:[^"\\]|\\.)*")\)')


def unescape(raw: str) -> str:
    def repl(match: re.Match[str]) -> str:
        ch = match.group(1)
        return {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}.get(ch, ch)
    return re.sub(r"\\(.)", repl, raw)


def main() -> int:
    if not TS_FILE.exists():
        print(f"缺少翻译源文件：{TS_FILE.relative_to(ROOT)}")
        return 1
    if not QM_FILE.exists():
        print(f"缺少编译后的翻译：{QM_FILE.relative_to(ROOT)}（先运行 lrelease）")
        return 1

    tree = ElementTree.parse(TS_FILE)
    translated: dict[tuple[str, str], str] = {}
    unfinished: list[str] = []
    for context in tree.getroot().findall("context"):
        name = context.findtext("name", default="")
        for message in context.findall("message"):
            source = message.findtext("source", default="")
            node = message.find("translation")
            if node is None:
                unfinished.append(f"{name}: {source}")
                continue
            if node.get("type") == "unfinished" or not (node.text or "").strip():
                unfinished.append(f"{name}: {source}")
            translated[(name, source)] = node.text or ""

    missing: list[str] = []
    total = 0
    for path in sorted((ROOT / "qml").rglob("*.qml")):
        context = path.stem
        text = path.read_text(encoding="utf-8")
        for line in text.splitlines():
            if line.lstrip().startswith("//"):
                continue
            for match in LITERAL.finditer(line):
                source = unescape(match.group(1)[1:-1])
                total += 1
                if (context, source) not in translated:
                    missing.append(f"{context}: {source}")

    print(f"QML qsTr 引用：{total}，TS 条目：{len(translated)}")
    if unfinished:
        print(f"未完成翻译 {len(unfinished)} 处：")
        for item in unfinished[:20]:
            print("  " + item)
        return 1
    if missing:
        print(f"缺少翻译 {len(missing)} 处：")
        for item in missing[:20]:
            print("  " + item)
        return 1
    print("i18n 完整性检查通过：0 缺失，0 未完成")
    return 0


if __name__ == "__main__":
    sys.exit(main())
