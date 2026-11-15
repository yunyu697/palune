#!/usr/bin/env python3
"""枚举构建树 / 打包暂存目录里的所有 exe，逐个检查「同目录依赖完整性」。

背景：用户双击过只带 exe、同目录没有 Qt DLL 的副本，Windows 加载器直接
0xC0000135（找不到 Qt6Gui.dll 之类），弹框由 csrss 持有、连我们自己都读不到正文。
这类目录光靠「windeployqt 跑过」不足以保证完整，所以在 CI 里做静态检查：

  1. 解析每个 PE 的导入表（不依赖 Windows API、不加载目标程序）；
  2. 非系统 DLL 必须在**该 exe 同目录**存在；
  3. 被同目录解析到的 DLL 继续递归检查它自己的导入（Qt6Quick -> Qt6Qml 这种）。

任何缺件 -> 退出码 1，CI 直接红。

用法：
  python tools/ci/check_exe_deps.py --root build/Release
  python tools/ci/check_exe_deps.py --root build/_CPack_Packages --recursive
  python tools/ci/check_exe_deps.py --exe build/Release/Palune.exe --json out.json
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path

# Windows 自带、不需要随包的 DLL。名单按「加载器一定能从系统目录找到」为准。
SYSTEM_DLLS = {
    "advapi32.dll", "authz.dll", "bcrypt.dll", "bcryptprimitives.dll", "cabinet.dll",
    "cfgmgr32.dll", "combase.dll", "comctl32.dll", "comdlg32.dll", "credui.dll",
    "crypt32.dll", "cryptbase.dll", "cryptui.dll", "d3d11.dll", "d3d12.dll",
    "d3d9.dll", "dbghelp.dll", "dcomp.dll", "dhcpcsvc.dll", "dnsapi.dll",
    "dsound.dll", "dwmapi.dll", "dwrite.dll", "dxgi.dll", "gdi32.dll",
    "gdi32full.dll", "glu32.dll", "hid.dll", "imm32.dll", "iphlpapi.dll",
    "kernel32.dll", "kernelbase.dll", "kernel.appcore.dll", "mpr.dll", "msacm32.dll",
    "msasn1.dll", "msimg32.dll", "msvcrt.dll", "mswsock.dll", "ncrypt.dll",
    "netapi32.dll", "normaliz.dll", "ntdll.dll", "ntoskrnl.exe", "ole32.dll",
    "oleaut32.dll", "opengl32.dll", "powrprof.dll", "profapi.dll", "propsys.dll",
    "psapi.dll", "rpcrt4.dll", "secur32.dll", "sechost.dll", "setupapi.dll",
    "shcore.dll", "shell32.dll", "shlwapi.dll", "sspicli.dll", "synchronization.dll",
    "urlmon.dll", "user32.dll", "userenv.dll", "usp10.dll", "uxtheme.dll",
    "version.dll", "wldap32.dll", "winhttp.dll", "wininet.dll", "winmm.dll",
    "winspool.drv", "wintrust.dll", "ws2_32.dll", "wtsapi32.dll", "ucrtbase.dll",
    "msvcp_win.dll", "textshaping.dll", "twinapi.appcore.dll", "windows.storage.dll",
}

SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-")


def _is_system(name: str) -> bool:
    low = name.lower()
    return low in SYSTEM_DLLS or low.startswith(SYSTEM_PREFIXES)


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


class PEImage:
    """最小 PE 解析：只取导入表需要的东西。"""

    def __init__(self, path: Path):
        self.path = path
        buf = path.read_bytes()
        if buf[:2] != b"MZ":
            raise ValueError("不是 PE 文件（缺 MZ）")
        pe = _u32(buf, 0x3C)
        if buf[pe:pe + 4] != b"PE\0\0":
            raise ValueError("不是 PE 文件（缺 PE 签名）")
        coff = pe + 4
        self.nsections = _u16(buf, coff + 2)
        opt_size = _u16(buf, coff + 16)
        opt = coff + 20
        magic = _u16(buf, opt)
        plus = magic == 0x20B
        dd_count = _u32(buf, opt + (108 if plus else 92))
        dd = opt + (112 if plus else 96)
        self.import_rva = _u32(buf, dd + 8) if dd_count > 1 else 0
        self.delay_rva = _u32(buf, dd + 8 * 13) if dd_count > 13 else 0
        self.sections = []
        sec = opt + opt_size
        for i in range(self.nsections):
            base = sec + i * 40
            vsize = _u32(buf, base + 8)
            vaddr = _u32(buf, base + 12)
            rawsize = _u32(buf, base + 16)
            rawptr = _u32(buf, base + 20)
            self.sections.append((vaddr, max(vsize, rawsize), rawptr))
        self.buf = buf

    def _off(self, rva: int) -> int | None:
        for vaddr, size, rawptr in self.sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        return None

    def _name_at(self, rva: int) -> str:
        off = self._off(rva)
        if off is None:
            return ""
        end = self.buf.find(b"\0", off)
        if end < 0:
            return ""
        return self.buf[off:end].decode("ascii", "replace")

    def imports(self) -> list[str]:
        names: list[str] = []
        # 常规导入表：IMAGE_IMPORT_DESCRIPTOR 每项 20 字节，全零结束；Name 在 +12
        if self.import_rva:
            off = self._off(self.import_rva)
            while off is not None and off + 20 <= len(self.buf):
                desc = self.buf[off:off + 20]
                if desc == b"\0" * 20:
                    break
                name_rva = _u32(desc, 12)
                if name_rva:
                    n = self._name_at(name_rva)
                    if n:
                        names.append(n)
                off += 20
        # 延迟导入表：32 字节一项，Name 在 +4；GR_ATTRS bit0 = 字段是 RVA
        if self.delay_rva:
            off = self._off(self.delay_rva)
            while off is not None and off + 32 <= len(self.buf):
                desc = self.buf[off:off + 32]
                if desc == b"\0" * 32:
                    break
                attrs = _u32(desc, 0)
                if attrs & 1:
                    name_rva = _u32(desc, 4)
                    if name_rva:
                        n = self._name_at(name_rva)
                        if n:
                            names.append(n)
                off += 32
        return names


def check_one(exe: Path, verbose: bool = False) -> dict:
    """检查单个 exe；返回 {exe, missing:[{name, requiredBy}], checked}"""
    local_dir = exe.parent
    local_files = {}
    for p in local_dir.iterdir():
        if p.is_file():
            local_files.setdefault(p.name.lower(), p)

    result = {"exe": str(exe), "missing": [], "checked": []}
    visited: set[str] = set()
    queue: list[tuple[Path, str]] = [(exe, str(exe))]

    while queue:
        binary, required_by = queue.pop()
        key = str(binary).lower()
        if key in visited:
            continue
        visited.add(key)
        try:
            imports = PEImage(binary).imports()
        except Exception as exc:  # 损坏文件也要报出来，而不是静默跳过
            result["missing"].append(
                {"name": "<无法解析 PE>", "requiredBy": required_by, "error": str(exc)}
            )
            continue
        result["checked"].append(binary.name)
        if verbose:
            print(f"  检查 {binary.name}（{len(imports)} 个导入）")
        for name in imports:
            if _is_system(name):
                continue
            hit = local_files.get(name.lower())
            if hit is None:
                result["missing"].append(
                    {"name": name, "requiredBy": os.path.basename(required_by)}
                )
                continue
            # 同目录解析到了 DLL：继续检查它自己的导入
            if hit.suffix.lower() == ".dll" and str(hit).lower() not in visited:
                queue.append((hit, str(hit)))
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="检查 exe 同目录依赖完整性")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--root", help="目录：扫描其中的 exe（默认不递归）")
    g.add_argument("--exe", help="单个 exe")
    ap.add_argument("--recursive", action="store_true", help="--root 时递归所有子目录")
    ap.add_argument("--json", help="把结果写成 JSON")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if args.exe:
        exes = [Path(args.exe)]
    else:
        root = Path(args.root)
        if not root.is_dir():
            print(f"[FAIL] 目录不存在：{root}", file=sys.stderr)
            return 2
        pattern = "**/*.exe" if args.recursive else "*.exe"
        exes = sorted(p for p in root.glob(pattern) if p.is_file())

    if not exes:
        print("[FAIL] 没有找到任何 exe（检查路径/递归参数）", file=sys.stderr)
        return 2

    problems = 0
    report = []
    print(f"== 检查 {len(exes)} 个 exe 的同目录依赖完整性 ==")
    for exe in exes:
        r = check_one(exe, args.verbose)
        report.append(r)
        if r["missing"]:
            problems += 1
            print(f"[MISS] {exe}")
            for m in r["missing"]:
                if m.get("error"):
                    print(f"        解析失败：{m['error']}")
                else:
                    print(f"        缺 {m['name']}（{m['requiredBy']} 需要）")
        else:
            print(f"[ OK ] {exe}  （递归检查 {len(r['checked'])} 个同目录二进制）")

    if args.json:
        out = Path(args.json)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )

    if problems:
        print(f"\n[FAIL] {problems}/{len(exes)} 个 exe 的同目录依赖不完整：这些目录一旦被双击，"
              f"Windows 加载器会直接 0xC0000135，弹框由 csrss 持有，用户只看到系统错误。",
              file=sys.stderr)
        return 1
    print(f"\n[ OK ] {len(exes)} 个 exe 的同目录依赖均完整")
    return 0


if __name__ == "__main__":
    sys.exit(main())
