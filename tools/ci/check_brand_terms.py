#!/usr/bin/env python3
"""品牌卫生检查：仓库内不得出现外部播放器品牌/产品名（大小写与常见变体）。

长期约束：源码、注释、文档、证据、配置、安装脚本、许可证与第三方说明、
提交信息与 PR 描述都不允许依赖竞品表述。这里把规则固化进 CI，避免回归。
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# 关键词拆开拼接，避免扫描器自身成为命中项。
BANNED = [
    "pot" + "player",
    "pot " + "player",
    "pot-" + "player",
    "pot_" + "player",
    "da" + "um",
    "iin" + "a",
]
PATTERN = re.compile("|".join(re.escape(word) for word in BANNED), re.IGNORECASE)

SKIP_DIRS = {".git", "build", "build-cov", "node_modules", "__pycache__"}
# qa/runtime 是本地生成目录（已在 .gitignore 中忽略），存放冒烟报告、临时配置
# 与第三方应用设置样本（例如编辑器 globalStorage 副本）。它不是仓库内容，
# 不参与品牌扫描，也避免把样本数据里的键名误判成竞品表述。
SKIP_PATHS = {(ROOT / "qa" / "runtime").resolve()}
SKIP_EXT = {
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".icns",
    ".dll", ".so", ".dylib", ".a", ".lib", ".exe", ".pdb", ".obj",
    ".mp4", ".mkv", ".mov", ".webm", ".mp3", ".flac", ".wav", ".ogg",
    ".zip", ".7z", ".gz", ".bz2", ".pdf",
}


def text_files() -> list[Path]:
    files: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [
            name for name in dirnames
            if name not in SKIP_DIRS and not name.startswith("build-")
            and (Path(dirpath) / name).resolve() not in SKIP_PATHS
        ]
        for name in filenames:
            path = Path(dirpath) / name
            if path.suffix.lower() in SKIP_EXT:
                continue
            try:
                if path.stat().st_size > 4 * 1024 * 1024:
                    continue
            except OSError:
                continue
            files.append(path)
    return files


def main() -> int:
    hits = []
    scanned = 0
    for path in text_files():
        try:
            content = path.read_text(encoding="utf-8", errors="strict")
        except (UnicodeDecodeError, OSError):
            try:
                content = path.read_text(encoding="latin-1")
            except OSError:
                continue
        scanned += 1
        for lineno, line in enumerate(content.splitlines(), start=1):
            if PATTERN.search(line):
                hits.append((path.relative_to(ROOT), lineno, line.strip()))

    print(f"已扫描文本文件：{scanned}")
    if hits:
        print(f"命中 {len(hits)} 处：")
        for rel, lineno, line in hits:
            print(f"  {rel}:{lineno}: {line[:160]}")
        return 1
    print("命中 0 处：品牌卫生检查通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
