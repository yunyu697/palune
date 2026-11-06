#!/usr/bin/env python3
"""Palune 版本号规则校验（CI 与本地共用）。

规则（硬约束）：
- 开发期版本为 1.0.0-alphaN；
- 除第一段外任何数值段 <= 10，禁止出现 11（alpha11 / 0.11.x 都不允许）；
- alpha 到 10 之后转为正式版 1.0.0；
- VERSION 文件是唯一来源，构建配置、安装包命名、关于页与更新清单必须与之一致。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATTERN = re.compile(r"^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-alpha(?P<alpha>[1-9]\d*))?$")
FORBIDDEN = re.compile(r"alpha(?:[0-9]{2,}|11)|\.(?:11|1[1-9]|2\d+)\.")


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    sys.exit(1)


def main() -> None:
    version_file = ROOT / "VERSION"
    if not version_file.exists():
        fail("缺少 VERSION 文件")
    version = version_file.read_text(encoding="utf-8").strip()
    m = PATTERN.match(version)
    if not m:
        fail(f"VERSION 格式不合法：{version}（应为 MAJOR.MINOR.PATCH-alphaN）")

    major, minor, patch = int(m["major"]), int(m["minor"]), int(m["patch"])
    alpha = int(m["alpha"]) if m["alpha"] else None

    if minor > 10:
        fail(f"MINOR 段不得超过 10：{minor}")
    if patch > 10:
        fail(f"PATCH 段不得超过 10：{patch}")
    if alpha is not None and not (1 <= alpha <= 10):
        fail(f"alpha 编号必须在 1..10：{alpha}")
    if alpha is None and version != "1.0.0":
        fail(f"仅正式版 1.0.0 可不带 alpha 后缀：{version}")

    # 全仓扫描：禁止任何 alpha11 / 0.11.x 之类的越界版本字面量。
    offenders = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if rel.as_posix() == "tools/version/check_version.py":
            continue
        if any(part in {"build", ".git", "third_party", "qa", "tests", "tools"} for part in rel.parts):
            continue
        if path.suffix.lower() not in {".txt", ".md", ".json", ".yml", ".yaml", ".cmake", ".py", ".cpp", ".h", ".qml"}:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for m2 in re.finditer(r"alpha(\d+)", text):
            if int(m2.group(1)) > 10:
                offenders.append(f"{rel}: {m2.group(0)}")
        for m2 in re.finditer(r"\b([0-9]+)\.([0-9]+)\.([0-9]+)", text):
            if int(m2.group(2)) > 10 or int(m2.group(3)) > 10:
                offenders.append(f"{rel}: {m2.group(0)}")
    if offenders:
        fail("发现越界版本号字面量：\n  " + "\n  ".join(offenders[:10]))

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "VERSION" not in cmake or "PALUNE_VERSION" not in cmake:
        fail("CMakeLists.txt 未从 VERSION 文件读取版本号")

    print(f"[OK] 版本号合规：{version}")
    print(f"     下一开发版应递增为 alpha{(alpha or 0) + 1}；alpha10 之后为 1.0.0")


if __name__ == "__main__":
    main()
