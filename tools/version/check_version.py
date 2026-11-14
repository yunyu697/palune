#!/usr/bin/env python3
"""Palune 版本号规则校验（CI 与本地共用）。

规则（硬约束）：
- 正式版为 MAJOR.MINOR.PATCH；开发期与候选期必须带 `<stage><n>` 后缀：
  `-alpha<n>`、`-beta<n>`、`-RC<n>`；
- **裸后缀不合法**：`1.0.0-RC`、`1.0.0-alpha`、`1.0.0-beta` 一律判不合规，
  后缀必须跟从 1 开始的正整数序号（无前导零）；
- 除第一段外任何版本段 <= 10，禁止出现 11（alpha11 / 0.11.x 都不允许）；
- alpha / beta 编号上限为 10；RC 编号**不设上限**，可一直递增到 RC11、RC12…；
- 阶段推进：alpha1..alpha10 → beta1..beta10 → RC1..RCn（n 无上限）→ 正式版 1.0.0；
- VERSION 文件是唯一来源，构建配置、安装包命名、关于页与更新清单必须与之一致；
- CI 里 tag 必须与 VERSION 逐字一致（否则下载链接与清单 base-url 会指向错的 tag）。
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATTERN = re.compile(
    r"^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)"
    r"(?:-(?P<stage>alpha|beta|RC)(?P<num>[1-9]\d*))?$"
)
# 裸后缀（没有序号）：单独识别，给出明确的修复提示。
BARE_SUFFIX = re.compile(r"^(?P<base>\d+\.\d+\.\d+)-(?P<stage>alpha|beta|RC|rc)$")


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    sys.exit(1)


def next_hint(stage: str | None, num: int | None) -> str:
    if stage is None:
        return "当前为正式版；下一版本 cycle 从 alpha1 开始。"
    if stage == "alpha":
        return f"下一版应为 alpha{num + 1}；" if num < 10 else "alpha10 之后进入 beta1。"
    if stage == "beta":
        return f"下一版应为 beta{num + 1}；" if num < 10 else "beta10 之后进入 RC1。"
    return f"下一版应为 RC{num + 1}（RC 序号不设上限）；候选通过后直接发正式版 1.0.0。"


def main() -> None:
    version_file = ROOT / "VERSION"
    if not version_file.exists():
        fail("缺少 VERSION 文件")
    version = version_file.read_text(encoding="utf-8").strip()

    bare = BARE_SUFFIX.match(version)
    if bare:
        stage = "RC" if bare["stage"].lower() == "rc" else bare["stage"]
        fail(
            f"裸后缀不合法：{version}；{stage} 阶段必须带序号，"
            f"写成 {bare['base']}-{stage}1（序号从 1 开始，不能用裸的 -{stage} 反复发）"
        )

    m = PATTERN.match(version)
    if not m:
        fail(f"VERSION 格式不合法：{version}（应为 MAJOR.MINOR.PATCH 或 MAJOR.MINOR.PATCH-alphaN/betaN/RCn）")

    major, minor, patch = int(m["major"]), int(m["minor"]), int(m["patch"])
    stage = m["stage"]
    num = int(m["num"]) if m["num"] else None

    if minor > 10:
        fail(f"MINOR 段不得超过 10：{minor}")
    if patch > 10:
        fail(f"PATCH 段不得超过 10：{patch}")
    if stage in ("alpha", "beta") and not (1 <= num <= 10):
        fail(f"{stage} 编号必须在 1..10：{num}")
    # RC 序号不设上限：由正则保证是 >= 1 的正整数，这里不再限制。
    if stage is None and version != "1.0.0":
        fail(f"仅正式版 1.0.0 可不带后缀：{version}")

    # CI 里 tag 与 VERSION 必须一致：否则 Release 资产 URL / 清单 base-url 会落到别的 tag。
    tag = os.environ.get("GITHUB_REF_NAME", "").strip()
    ref_type = os.environ.get("GITHUB_REF_TYPE", "").strip()
    if tag and ref_type == "tag" and tag != version:
        fail(f"tag 与 VERSION 不一致：tag={tag} VERSION={version}")

    # 全仓扫描：禁止任何 alpha11 / beta11 / 0.11.x 之类的越界版本字面量。
    offenders = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if rel.as_posix() == "tools/version/check_version.py":
            continue
        # 证据目录是历史输出（含第三方软件/服务端版本号，如 Jellyfin 10.11.11），不是版本来源。
        if rel.as_posix().startswith("docs/evidence/"):
            continue
        # 本地构建目录（build*、dist）与依赖目录不是版本号来源，跳过；
        # 否则 CMakeCache 里的编译器版本会被当成越界版本字面量。
        skip_parts = {".git", "third_party", "qa", "tests", "tools", "dist", "_scratch"}
        if any(part in skip_parts or part.startswith("build") for part in rel.parts):
            continue
        if path.suffix.lower() not in {".txt", ".md", ".json", ".yml", ".yaml", ".cmake", ".py", ".cpp", ".h", ".qml"}:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        # 先屏蔽 IPv4 地址（如 192.168.1.10）：否则会被下面的三段版本号正则误判成越界版本。
        scan_text = re.sub(r"(?<![\d.])\d{1,3}(?:\.\d{1,3}){3}(?![\d.])", " ", text)
        for stage_name in ("alpha", "beta"):
            for m2 in re.finditer(rf"{stage_name}(\d+)", scan_text):
                if int(m2.group(1)) > 10:
                    offenders.append(f"{rel}: {m2.group(0)}")
        for m2 in re.finditer(r"\b([0-9]+)\.([0-9]+)\.([0-9]+)", scan_text):
            if int(m2.group(2)) > 10 or int(m2.group(3)) > 10:
                offenders.append(f"{rel}: {m2.group(0)}")
    if offenders:
        fail("发现越界版本号字面量：\n  " + "\n  ".join(offenders[:10]))

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "VERSION" not in cmake or "PALUNE_VERSION" not in cmake:
        fail("CMakeLists.txt 未从 VERSION 文件读取版本号")

    print(f"[OK] 版本号合规：{version}")
    if stage:
        print(f"     {stage}{num} · {next_hint(stage, num)}")
    else:
        print(f"     {next_hint(stage, num)}")


if __name__ == "__main__":
    main()
