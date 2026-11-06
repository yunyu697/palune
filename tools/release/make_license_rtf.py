"""把 packaging/LICENSE.txt 转成 NSIS/WiX 都能正确显示的 RTF。

中文不能直接写进 RTF（RTF 规范里没有 UTF-8），必须用 \\uN? 转义；
NSIS 的许可页与 WiX 的 RTF 控件都按 Unicode 码位渲染，因此中文不会再乱码。
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "packaging" / "LICENSE.txt"
DST = ROOT / "packaging" / "LICENSE.rtf"


def escape_ascii(ch: str) -> str:
    if ch in "\\{}":
        return "\\" + ch
    return ch


def to_rtf(text: str) -> str:
    out = [
        r"{\rtf1\ansi\ansicpg936\deff0"
        r"{\fonttbl{\f0\fnil\fcharset134 Microsoft YaHei;}}",
        r"\f0\fs20 ",
    ]
    for ch in text:
        if ch == "\n":
            out.append(r"\par" + "\n")
        elif ch == "\r":
            continue
        elif ch == "\t":
            out.append(r"\tab ")
        elif ord(ch) < 128:
            out.append(escape_ascii(ch))
        else:
            code = ord(ch)
            if code > 0xFFFF:
                code -= 0x10000
                hi = 0xD800 + (code >> 10)
                lo = 0xDC00 + (code & 0x3FF)
                out.append(f"\\u{hi}?\\u{lo}?")
            elif code >= 0x8000:
                out.append(f"\\u{code - 0x10000}?")
            else:
                out.append(f"\\u{code}?")
    out.append("}\n")
    return "".join(out)


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    DST.write_text(to_rtf(text), encoding="ascii")
    print(f"已生成 {DST}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
