#!/usr/bin/env python3
"""分层约束静态检查（M1 判据 9、11）。

规则：
- qml/components/ 不得 import qml/layouts/（L1 组件不能依赖 L2 布局）
- qml/tokens/     不得 import components/ 或 layouts/（L0 令牌零依赖）
- qml/layouts/    不得出现播放器调用（player.xxx(...)）、mpv 或 VideoHost
- qml/            全量不得直接调用播放器后端（player.xxx(...)）或引用 mpv API（mpv.* / MpvClient），
                  一律经 PlayerFacade 读属性、经 win.action 下发动作
- src/media/      不得 include mpv/ 或任何 Qt Quick/QML 头文件（内容源抽象与 UI/内核解耦）

违规即退出码 1，供 CI 直接判红。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QML = ROOT / "qml"
SRC = ROOT / "src"

IMPORT_RE = re.compile(r"^\s*import\s+(.+)$", re.MULTILINE)
PLAYER_CALL_RE = re.compile(r"\bplayer\.\w+\s*\(")
MPV_RE = re.compile(r"\bmpv\b|MpvClient", re.IGNORECASE)
# 只匹配真正的后端 API 引用（mpv.xxx / MpvClient），避免把「libmpv 动态链接」这类
# 用户可见文案算作越层；文案里的品牌披露由 check_brand_terms 与文档规范约束。
MPV_API_RE = re.compile(r"\bmpv\.\w+|MpvClient")
VIDEO_HOST_RE = re.compile(r"VideoHost")
CINCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

failures: list[str] = []


def qml_files(folder: Path) -> list[Path]:
    return sorted(folder.rglob("*.qml")) if folder.is_dir() else []


def main() -> int:
    tokens = qml_files(QML / "tokens")
    components = qml_files(QML / "components")
    layouts = qml_files(QML / "layouts")

    for path in components:
        text = path.read_text(encoding="utf-8")
        for imp in IMPORT_RE.findall(text):
            if "layouts" in imp:
                failures.append(f"{path.relative_to(ROOT)}：L1 组件不得 import 布局（{imp.strip()}）")

    for path in tokens:
        text = path.read_text(encoding="utf-8")
        for imp in IMPORT_RE.findall(text):
            if "components" in imp or "layouts" in imp:
                failures.append(f"{path.relative_to(ROOT)}：L0 令牌不得 import 组件/布局（{imp.strip()}）")

    for path in layouts:
        text = path.read_text(encoding="utf-8")
        if PLAYER_CALL_RE.search(text):
            failures.append(f"{path.relative_to(ROOT)}：布局中出现播放器方法调用")
        if MPV_RE.search(text):
            failures.append(f"{path.relative_to(ROOT)}：布局中出现 mpv 引用")
        if VIDEO_HOST_RE.search(text):
            failures.append(f"{path.relative_to(ROOT)}：VideoHost 必须在布局树外")

    # 布局之外的全量 QML 再扫一遍，防止同类越层在面板/组件里重新冒出来：
    # 读播放器状态只能读 PlayerFacade 属性，动作只能经 win.action 下发。
    all_qml = qml_files(QML)
    for path in all_qml:
        if path.is_relative_to(QML / "layouts"):
            continue
        text = path.read_text(encoding="utf-8")
        if PLAYER_CALL_RE.search(text):
            failures.append(
                f"{path.relative_to(ROOT)}：QML 直接调用播放器后端方法（改走 win.action）"
            )
        if MPV_API_RE.search(text):
            failures.append(
                f"{path.relative_to(ROOT)}：QML 直接引用 mpv 后端 API（应经 PlayerFacade/VideoHost）"
            )

    media_sources = sorted((SRC / "media").rglob("*")) if (SRC / "media").is_dir() else []
    for path in media_sources:
        if not path.is_file() or path.suffix.lower() not in {".h", ".hpp", ".cpp", ".cc"}:
            continue
        text = path.read_text(encoding="utf-8")
        for inc in CINCLUDE_RE.findall(text):
            low = inc.lower()
            if low.startswith("mpv/") or low in {"mpv/client.h"}:
                failures.append(f"{path.relative_to(ROOT)}：media/ 不得依赖 mpv（{inc}）")
            elif low.startswith(("qtquick", "qtqml", "qml")):
                failures.append(f"{path.relative_to(ROOT)}：media/ 不得依赖 QML（{inc}）")

    if failures:
        print("[FAIL] 分层约束检查未通过：")
        for item in failures:
            print(f"  - {item}")
        return 1
    print(f"[OK] 分层约束通过：tokens={len(tokens)} components={len(components)} "
          f"layouts={len(layouts)} qml={len(all_qml)} media 源文件已检查")
    return 0


if __name__ == "__main__":
    sys.exit(main())
