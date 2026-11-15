#!/usr/bin/env python3
"""设计令牌静态检查（docs/DESIGN-SYSTEM.md §8）。

规则：
- D1 间距/字号/字距/行高/圆角/描边/内边距不得裸写数值，只能取 tokens.*
- D2 qml/components 内的控件尺寸（width/height/implicitWidth/implicitHeight）不得裸写数值
- D3 交互状态色不得裸写（#14ffffff / #26ffffff / ...），一律 tokens.state*
- D4 字号必须在阶梯内：font.pixelSize 只能引用 tokens.fontXxx（或组件自身属性/表达式）
- D5 令牌必须存在：用到的 tokens.X 必须在 qml/tokens/Tokens.qml 中声明
- D6 响应式断点不得裸写（900/720/520/360/320），一律 tokens.bp*
- D7 控件高度达标：qml/components 的 implicitHeight 令牌值 >= tokens.minHit，
     或在文件内用 anchors.margins: -tokens.hitPad 扩过命中区（滑杆/开关这类小控件）
- D8 窗口圆角只有一个来源：src/theme/ThemeRegistry.cpp 的 windowRadius 返回值必须在
     Tokens 圆角阶梯内；SettingsWindow 不得再维护第二张圆角表
- D9 同一组件文件内不得并存两档以上圆角（同一控件在不同状态下圆角跳档）
- D10 窗口圆角不得做偏移（windowRadius ± n）：窗口外框已交给系统

豁免写法：行尾或上一行写 `// token-exempt: 原因`。
违规即退出码 1，供 CI 直接判红。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QML = ROOT / "qml"
TOKENS_FILE = QML / "tokens" / "Tokens.qml"

TOKEN_DECL_RE = re.compile(r"readonly\s+property\s+\w+\s+(\w+)\s*:\s*([^/\n]+)")
TOKEN_REF_RE = re.compile(r"\btokens\.(\w+)")

SPACING_PROPS = [
    "spacing", "rowSpacing", "columnSpacing",
    "leftMargin", "rightMargin", "topMargin", "bottomMargin", "margins",
    "padding", "leftPadding", "rightPadding", "topPadding", "bottomPadding",
]
TYPE_PROPS = ["font.pixelSize", "font.letterSpacing", "font.lineHeight"]
SHAPE_PROPS = ["radius", "border.width"]
CONTROL_PROPS = ["width", "height", "implicitWidth", "implicitHeight"]

LITERAL_RE = re.compile(r"^\s*(-?\d+(?:\.\d+)?)\s*$")
STATE_COLORS = {
    "#08ffffff", "#0affffff", "#0dffffff", "#12ffffff", "#14ffffff",
    "#18ffffff", "#1affffff", "#1fffffff", "#22ffffff", "#26ffffff",
    "#33ffffff", "#99000000", "#f20d0e11", "#ff0d0e11", "#f210141a",
}
FONT_TOKENS = {f"font{n}" for n in
               ["Caption", "Small", "Body", "BodyL", "Subtitle", "Title", "Display", "S", "M", "L"]}
RADIUS_TOKENS = {"radiusXS", "radiusS", "radiusM", "radiusL"}
BP_LITERALS = {"900", "720", "520", "360", "320"}
HEIGHT_EXEMPT = {"trackHitH", "switchH", "seekH", "badgeH", "chipH"}


def load_tokens() -> dict[str, str]:
    if not TOKENS_FILE.is_file():
        return {}
    table = {}
    for name, value in TOKEN_DECL_RE.findall(TOKENS_FILE.read_text(encoding="utf-8")):
        table[name] = value.strip()
    return table


def exempted(lines: list[str], idx: int) -> bool:
    if "token-exempt" in lines[idx]:
        return True
    return idx > 0 and "token-exempt" in lines[idx - 1]


def prop_value(line: str, prop: str):
    """返回 (裸字面量, 原始表达式)；非该属性返回 (None, None)。"""
    m = re.search(
        r"(?<![\w.])(?:(?:anchors|Layout)\.)?" + re.escape(prop) + r"\s*:\s*([^;}\n]+)", line
    )
    if not m:
        return None, None
    raw = m.group(1).strip()
    raw = re.sub(r"//.*$", "", raw).strip()
    lit = LITERAL_RE.match(raw)
    return (lit.group(1) if lit else None), raw


def main() -> int:
    tokens = load_tokens()
    failures: list[str] = []
    checked = 0

    for path in sorted(QML.rglob("*.qml")):
        if path == TOKENS_FILE:
            continue
        checked += 1
        rel = path.relative_to(ROOT)
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        is_component = path.parent.name == "components"
        has_hitpad = "tokens.hitPad" in text

        for i, line in enumerate(lines):
            if exempted(lines, i) or line.strip().startswith("//"):
                continue

            for prop in SPACING_PROPS + TYPE_PROPS + SHAPE_PROPS:
                lit, raw = prop_value(line, prop)
                if raw is None:
                    continue
                branch = [v for v in re.findall(r"[?:]\s*(-?\d+(?:\.\d+)?)", raw) if float(v) != 0]
                if branch:
                    failures.append(f"{rel}:{i + 1} D1 {prop} 三元分支裸写数值（{raw}）")
                if lit is None:
                    continue
                if lit in {"0", "0.0"}:
                    continue
                failures.append(f"{rel}:{i + 1} D1 {prop} 裸写 {lit}（应为 tokens.space*/font*/radius*/stroke）")
                if prop == "font.pixelSize" and raw not in FONT_TOKENS:
                    failures.append(f"{rel}:{i + 1} D4 字号 {raw} 不在阶梯内")
                if prop == "radius" and raw.startswith("tokens.") and raw[7:] not in RADIUS_TOKENS:
                    failures.append(f"{rel}:{i + 1} D1 radius 取值 {raw} 不在圆角阶梯内")

            if is_component:
                for prop in CONTROL_PROPS:
                    lit, _ = prop_value(line, prop)
                    if lit is not None and lit not in {"0", "0.0"}:
                        failures.append(f"{rel}:{i + 1} D2 {prop} 裸写 {lit}（组件尺寸须用令牌）")

            for color in STATE_COLORS:
                if '"%s"' % color in line.lower():
                    failures.append(f"{rel}:{i + 1} D3 状态色裸写 {color}（应为 tokens.state*/scrim/layer*）")

            m = re.search(r"(?:width|height)\s*(?:>=|<=|>|<)\s*(\d+)(?![\w.])", line)
            if m and m.group(1) in BP_LITERALS:
                failures.append(f"{rel}:{i + 1} D6 断点裸写 {m.group(1)}（应为 tokens.bp*）")

            if re.search(r"windowRadius\s*[-+]", line):
                failures.append(
                    f"{rel}:{i + 1} D10 窗口圆角不得做偏移（窗口外框已交给系统，"
                    f"QML 不再自绘窗口级描边）"
                )

            # 形如 `property int panelRadius: 18` 的裸圆角默认值同样算裸值（0 为直角，允许）。
            radius_lit = re.search(
                r"\b\w*[Rr]adius\w*\s*:\s*(-?\d+(?:\.\d+)?)\s*(?://.*)?$",
                line.split("//")[0],
            )
            if radius_lit and radius_lit.group(1) not in {"0", "0.0"}:
                failures.append(
                    f"{rel}:{i + 1} D1 圆角属性裸写 {radius_lit.group(1)}（应为 tokens.radius*）"
                )

            for name in TOKEN_REF_RE.findall(line):
                if name not in tokens:
                    failures.append(f"{rel}:{i + 1} D5 令牌 tokens.{name} 未在 Tokens.qml 声明")

        if is_component:
            used_radii = {n for n in RADIUS_TOKENS if re.search(r"tokens\.%s\b" % n, text)}
            if len(used_radii) > 1 and "token-exempt" not in text:
                failures.append(
                    f"{rel}:1 D9 同一组件内并存 {sorted(used_radii)} 多档圆角"
                    f"（同一控件族只允许一档；确需例外写 // token-exempt: 原因）"
                )

            m = re.search(r"^\s*implicitHeight\s*:\s*tokens\.(\w+)", text, re.MULTILINE)
            if m and not has_hitpad:
                name = m.group(1)
                value = tokens.get(name, "")
                if name in HEIGHT_EXEMPT:
                    pass
                elif re.match(r"^\d+$", value) and int(value) < int(tokens.get("minHit", "26")):
                    failures.append(
                        f"{rel}:1 D7 控件高度 tokens.{name}={value} < minHit，"
                        f"且未用 anchors.margins: -tokens.hitPad 扩大命中区"
                    )

    # D8 窗口圆角只有一个来源（QML 令牌阶梯），三套主题只能在这个阶梯里选值。
    ladder = {v.strip() for v in (tokens.get(n, "") for n in RADIUS_TOKENS)}
    ladder = {v for v in ladder if re.match(r"^\d+$", v)}
    tr = ROOT / "src/theme/ThemeRegistry.cpp"
    if tr.is_file() and ladder:
        body = tr.read_text(encoding="utf-8")
        m = re.search(r"int ThemeRegistry::windowRadius\(.*?\n\}", body, re.S)
        if m:
            for v in re.findall(r"return\s+(\d+)\s*;", m.group(0)):
                if v not in ladder:
                    failures.append(
                        f"src/theme/ThemeRegistry.cpp D8 窗口圆角 {v} 不在 Token 阶梯 "
                        f"{sorted(ladder, key=int)} 内"
                    )
    sw = ROOT / "src/window/SettingsWindow.cpp"
    if sw.is_file() and "ThemeRegistry::windowRadius" not in sw.read_text(encoding="utf-8"):
        failures.append(
            "src/window/SettingsWindow.cpp D8 设置窗另维护了第二张圆角表"
            "（应与主窗共用 ThemeRegistry::windowRadius）"
        )

    print(f"check_design_tokens: scanned {checked} QML files, {len(tokens)} tokens")
    if failures:
        for f in failures:
            print("FAIL " + f)
        print(f"check_design_tokens: {len(failures)} violation(s)")
        return 1
    print("check_design_tokens: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
