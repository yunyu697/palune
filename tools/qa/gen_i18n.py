#!/usr/bin/env python3
"""把 QML 中的用户可见文案包进 qsTr()，并生成英文翻译资源。

设计：
- 源语言保持中文（默认语言），英文由 i18n/palune_en_US.qm 提供。
- 只处理含 CJK 的字符串字面量，注释行跳过；已包在 qsTr() 里的幂等跳过。
- 翻译表缺失时直接失败，避免漏译混进构建。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
QML_DIRS = [ROOT / "qml"]
CJK = re.compile("[\u4e00-\u9fff]")
LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')

TRANSLATIONS = {
    " · libmpv 动态链接 · 渲染路径：": " · libmpv dynamic linking · render path: ",
    " · 可用库：": " · available libraries: ",
    " · 字幕 ": " · subtitles ",
    " 原生窗口": " native window",
    "40 个 P0 动作的绑定表、启动冲突检测与解绑界面尚未完成（M2 待办）。当前 10 项为主窗口直接接线。":
        "Binding table, launch conflict detection, and unbind UI for the 40 P0 actions are "
        "not finished yet (M2 todo). The 10 wired actions below are direct connections in the "
        "main window.",
    "PALUNE · 工作台": "PALUNE · Workbench",
    "Tab 在可操作控件间移动焦点": "Tab moves focus between actionable controls",
    "\\n崩溃：": "\\nCrash: ",
    "\\n日志：": "\\nLog: ",
    "px · 当前：": "px · current: ",
    "px · 滞回 ±": "px · hysteresis ±",
    "s · 速度：": "s · speed: ",
    "← / → 后退/前进 10 秒 · ↑ / ↓ 音量 ±5 · Enter 激活焦点控件\\n":
        "← / → seek back/forward 10s · ↑ / ↓ volume ±5 · Enter activates the focused control\\n",
    "上次检查：": "Last checked: ",
    "下载通道 · 当前：": "Download channel · current: ",
    "主题": "Theme",
    "仅下载": "Download only",
    "使用中": "In use",
    "全屏": "Fullscreen",
    "关于": "About",
    "关闭": "Close",
    "切换布局": "Switch layout",
    "刷新": "Refresh",
    "发布 ": "Released ",
    "取消静音": "Unmute",
    "只提供 A / B / C 三套布局，不设皮肤或主题包入口。":
        "Only layouts A / B / C are provided; no skin or theme package entry points.",
    "外观": "Appearance",
    "媒体库": "Library",
    "字号 / 延迟 / 外挂字幕在 M3 接入；本页为轨道状态展示。":
        "Font size / delay / external subtitles arrive in M3; this page shows track status.",
    "字幕 ": "Subtitles ",
    "字幕": "Subtitles",
    "字幕轨道": "Subtitle tracks",
    "安装": "Install",
    "将视频拖到这里，或点击下方按钮": "Drop a video here, or use the button below",
    "已接线快捷键": "Wired shortcuts",
    "已注册内容源：": "Registered sources: ",
    "已静音": "Muted",
    "布局 ": "Layout ",
    "常规": "General",
    "当前播放": "Now playing",
    "当前文件没有内嵌字幕轨道。": "This file has no embedded subtitle tracks.",
    "当前文件没有音频轨道。": "This file has no audio tracks.",
    "当前版本 ": "Current version ",
    "快捷键": "Shortcuts",
    "快捷键系统进度": "Shortcut system progress",
    "打开": "Open",
    "打开主页": "Open homepage",
    "打开其他文件": "Open another file",
    "打开文件": "Open file",
    "播放": "Play",
    "播放中": "Playing",
    "播放失败": "Playback failed",
    "文件夹": "Folder",
    "文件：": "File: ",
    "断点在 settings.toml 的 [ui.layout.<主题>] compactBelow 中配置，非法值回退 560。":
        "The breakpoint is configured as compactBelow under [ui.layout.<theme>] in settings.toml; "
        "invalid values fall back to 560.",
    "断点：": "Breakpoint: ",
    "新版本 ": "New version ",
    "时长：": "Duration: ",
    "暂停": "Pause",
    "更新": "Update",
    "最近打开": "Recent",
    "未打开": "Not opened",
    "未打开文件": "No file",
    "标题：": "Title: ",
    "检查中…": "Checking…",
    "检查更新": "Check for updates",
    "渲染：libmpv（动态加载）· 输出：": "Rendering: libmpv (dynamic) · output: ",
    "状态：": "Status: ",
    "知道了": "Got it",
    "硬件解码、缓冲与 HDR 状态在 M3/M4 接入。":
        "Hardware decoding, buffering, and HDR status arrive in M3/M4.",
    "空格 播放/暂停 · F11 全屏 · Esc 退出面板/全屏 · O 打开文件\\n":
        "Space play/pause · F11 fullscreen · Esc exits panel/fullscreen · O opens a file\\n",
    "窗口": "Window",
    "紧凑": "Compact",
    "紧凑模式": "Compact mode",
    "解码与渲染": "Decoding & rendering",
    "设置": "Settings",
    "轨道 ": "Track ",
    "还原": "Restore",
    "退出全屏": "Exit fullscreen",
    "通用": "General",
    "配置：": "Config: ",
    "重试": "Retry",
    "镜像只影响下载速度；SHA-256 与 Ed25519 签名校验始终基于内置公钥，不依赖镜像。":
        "Mirrors only affect download speed; SHA-256 and Ed25519 verification always use the "
        "built-in public key and never depend on the mirror.",
    "静音": "Mute",
    "音轨 ": "Audio ",
    "音轨": "Audio",
    "音量": "Volume",
    "音频": "Audio",
    "音频轨道": "Audio tracks",
    "（静音）": " (muted)",
}

# C++ 侧用户可见文案（状态、错误、示例数据），统一用 "Palune" 上下文。
CPP_TRANSLATIONS = {
    "即将完成": "Finishing up",
    "剩余 %1 秒": "%1 s remaining",
    "直连": "Direct",
    "尚未检查": "Not checked",
    "尚未检查更新": "No update check yet",
    "正在检查更新…": "Checking for updates…",
    "已是最新版本（当前 %1）· 上次检查 %2": "Up to date (current %1) · last checked %2",
    "发现新版本 %1（发布于 %2，体积 %3）": "New version %1 (released %2, size %3)",
    "正在下载 %1 · %2%": "Downloading %1 · %2%",
    "已下载到 %1，可安装": "Downloaded %1, ready to install",
    "正在安装，完成后将自动重启": "Installing; the app will restart automatically",
    "安装": "Install",
    "更新": "Update",
    "清单架构 %1 / 当前 %2": "Manifest architecture %1 / current %2",
    "内置更新公钥不可用": "Built-in update public key unavailable",
    "直连（官方源）": "Direct (official source)",
    "尚未成功获取更新清单": "No update manifest has been fetched yet",
    "下载地址为空": "Download URL is empty",
    "下载文件句柄已丢失": "Download file handle lost",
    "无法重命名下载文件": "Could not rename the downloaded file",
    "SHA-256 不匹配：期望 %1，实际 %2": "SHA-256 mismatch: expected %1, got %2",
    "清单签名复核失败：%1": "Manifest signature re-check failed: %1",
    "安装包签名校验失败：%1": "Installer signature verification failed: %1",
    "下载文件不存在": "Downloaded file does not exist",
    "M0 技术验证版：完善全屏与 DPI 切换，修复窗口偏移。":
        "M0 technical preview: fullscreen and DPI switching improvements, window offset fixes.",
    "镜像 A · 华东": "Mirror A · East China",
    "镜像 B · 华南": "Mirror B · South China",
    "网络不可达或请求超时，请检查网络后重试":
        "Network unreachable or request timed out; check your connection and retry",
    "更新服务器返回异常状态": "Update server returned an unexpected status",
    "更新清单无法解析": "Update manifest could not be parsed",
    "更新清单签名校验失败，已拒绝使用":
        "Manifest signature verification failed; update rejected",
    "该更新包与当前平台或架构不匹配":
        "The update does not match this platform or architecture",
    "安装包完整性校验失败，已拒绝安装":
        "Installer integrity check failed; installation rejected",
    "下载中断，可重试或切换镜像": "Download interrupted; retry or switch mirrors",
    "无法启动安装程序": "Could not start the installer",
    "没有写入权限或磁盘空间不足": "No write permission or not enough disk space",
    "镜像地址无效": "Invalid mirror URL",
    "未知更新错误": "Unknown update error",
    "还没有媒体库": "No library yet",
    "M1 只预留内容源接口，未接入服务器。配对入口将在媒体库里程碑开放。":
        "M1 only reserves the content-source interface; no server is connected. "
        "Pairing will arrive in the library milestone.",
    "libmpv 缺少导出符号：%1": "libmpv is missing export symbols: %1",
    "未找到 libmpv 动态库（libmpv-2.dll / libmpv.so.2 / libmpv.2.dylib）。":
        "libmpv dynamic library not found (libmpv-2.dll / libmpv.so.2 / libmpv.2.dylib).",
    "禁止静态链接，请把动态库放在程序目录。":
        "Static linking is not allowed; place the dynamic library next to the executable.",
    "mpv_create 失败": "mpv_create failed",
    "mpv_initialize 失败：%1": "mpv_initialize failed: %1",
    "测试图样打开失败：%1": "Could not open the test pattern: %1",
    "未初始化": "Not initialized",
    "未知": "Unknown",
    "wid + d3d11 + gpu-next（Windows 主路径）":
        "wid + d3d11 + gpu-next (Windows primary path)",
    "render API (GL) — 接口已预留，M0 未在本机验证":
        "render API (GL) — interface reserved; not verified locally in M0",
    "mpv 尚未初始化": "mpv is not initialized",
    "打开文件失败：%1": "Could not open the file: %1",
    "更新清单不是合法 JSON：%1": "Update manifest is not valid JSON: %1",
    "更新清单缺少 payload 对象": "Update manifest is missing the payload object",
    "不支持的 schemaVersion：%1": "Unsupported schemaVersion: %1",
    "更新清单缺少 signature，拒绝使用未签名清单":
        "Update manifest is missing a signature; unsigned manifests are rejected",
    "更新清单缺少 version": "Update manifest is missing version",
    "更新清单版本号不合规：%1": "Invalid manifest version: %1",
    "通道 %1 与预发布版本 %2 不一致": "Channel %1 does not match prerelease version %2",
    "通道 stable 不接受预发布版本 %1": "The stable channel does not accept prerelease version %1",
    "未知发布通道：%1": "Unknown release channel: %1",
    "产物条目不完整：%1/%2": "Incomplete artifact entry: %1/%2",
    "UPD-01 清单中没有适用于 %1/%2 的产物": "UPD-01: no artifact for %1/%2 in the manifest",
    "更新清单缺少 artifacts 产物列表": "Update manifest is missing the artifacts list",
    "产物缺少签名，拒绝使用未签名安装包":
        "Artifact is unsigned; unsigned installers are rejected",
    "更新清单缺少 url": "Update manifest is missing url",
    "更新清单 sha256 长度不合法": "Invalid sha256 length in the update manifest",
    "更新清单 size 不合法": "Invalid size in the update manifest",
    "未找到 libsodium 动态库，无法校验更新签名":
        "libsodium dynamic library not found; cannot verify update signatures",
    "签名或公钥长度不合法": "Invalid signature or public-key length",
    "Ed25519 签名校验未通过": "Ed25519 signature verification failed",
    "无法读取内置公钥资源：%1": "Could not read the built-in public-key resource: %1",
    "内置公钥不是 32 字节十六进制": "Built-in public key is not 32 bytes of hex",
    "示例影片 %1": "Sample video %1",
    "示例影片 · 1080p": "Sample video · 1080p",
    "示例影片 · 4K60": "Sample video · 4K60",
    "主音轨": "Main audio",
    "评论音轨": "Commentary",
    "简体中文": "Chinese (Simplified)",
    "播放失败：无法解码该文件（样本错误）":
        "Playback failed: cannot decode this file (sample error)",
    "(截图模式)": "(screenshot mode)",
}

CPP_FILES = [
    "src/update/UpdateService.cpp",
    "src/update/UpdateManifest.cpp",
    "src/update/UpdateCrypto.cpp",
    "src/media/MediaLibraryService.h",
    "src/mpv/MpvClient.cpp",
    "src/app/AppController.cpp",
    "src/app/ShotContexts.h",
]



def qml_files() -> list[Path]:
    files: list[Path] = []
    for base in QML_DIRS:
        files.extend(sorted(base.rglob("*.qml")))
    return files


def unescape(raw: str) -> str:
    def repl(match: re.Match[str]) -> str:
        ch = match.group(1)
        return {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}.get(ch, ch)
    return re.sub(r"\\(.)", repl, raw)


def wrap_file(path: Path) -> tuple[str, list[str]]:
    """返回（新内容，源字符串列表）；注释行不处理。"""
    sources: list[str] = []
    out_lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines(keepends=True):
        stripped = line.lstrip()
        if stripped.startswith("//"):
            out_lines.append(line)
            continue
        result = ""
        last = 0
        changed = False
        for match in LITERAL.finditer(line):
            literal = match.group(0)
            if not CJK.search(literal):
                continue
            if line[:match.start()].rstrip().endswith("qsTr("):
                sources.append(literal[1:-1])
                continue
            sources.append(literal[1:-1])
            result += line[last:match.start()] + "qsTr(" + literal + ")"
            last = match.end()
            changed = True
        if changed:
            result += line[last:]
            out_lines.append(result)
        else:
            out_lines.append(line)
    return "".join(out_lines), sources


def wrap_cpp_file(path: Path) -> tuple[str, list[str], int]:
    text = path.read_text(encoding="utf-8")
    used: list[str] = []
    count = 0
    for source in CPP_TRANSLATIONS:
        literal = 'QStringLiteral("' + source + '")'
        if literal not in text:
            continue
        replacement = (
            'QCoreApplication::translate("Palune", "' + source + '")'
        )
        count += text.count(literal)
        text = text.replace(literal, replacement)
        used.append(source)
    if used and "#include <QCoreApplication>" not in text:
        anchor = text.find("\n#include ")
        if anchor >= 0:
            text = text[: anchor + 1] + "#include <QCoreApplication>\n" + text[anchor + 1:]
        else:
            text = "#include <QCoreApplication>\n\n" + text
    if used:
        path.write_text(text, encoding="utf-8")
    return text, used, count


def write_ts(entries: dict[str, dict[str, str]], path: Path) -> None:
    lines = ['<?xml version="1.0" encoding="utf-8"?>', "<!DOCTYPE TS>",
             '<TS version="2.1" language="en_US">']
    for context in sorted(entries):
        lines.append("<context>")
        lines.append(f"    <name>{escape(context)}</name>")
        table = CPP_TRANSLATIONS if context == "Palune" else TRANSLATIONS
        for source in sorted(entries[context]):
            lines.append("    <message>")
            lines.append(f"        <source>{escape(unescape(source))}</source>")
            lines.append(
                f"        <translation>{escape(table[source])}</translation>"
            )
            lines.append("    </message>")
        lines.append("</context>")
    lines.append("</TS>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    entries: dict[str, dict[str, str]] = {}
    missing: list[str] = []
    wrapped = 0
    for path in qml_files():
        new_text, sources = wrap_file(path)
        old_text = path.read_text(encoding="utf-8")
        if new_text != old_text:
            path.write_text(new_text, encoding="utf-8")
            wrapped += 1
        context = path.stem
        for literal in sources:
            entries.setdefault(context, {})[literal] = literal
            if literal not in TRANSLATIONS:
                missing.append(f"{path.name}: {literal}")

    if missing:
        print("缺少英文翻译：")
        for item in missing:
            print("  " + item)
        return 1

    cpp_total = 0
    cpp_wrapped = 0
    for relative in CPP_FILES:
        path = ROOT / relative
        if not path.exists():
            print(f"缺少文件：{relative}")
            return 1
        _, used, count = wrap_cpp_file(path)
        if count:
            cpp_wrapped += 1
        cpp_total += count
        for source in used:
            entries.setdefault("Palune", {})[source] = source
    for source in CPP_TRANSLATIONS:
        entries.setdefault("Palune", {})[source] = source

    write_ts(entries, ROOT / "i18n" / "palune_en_US.ts")
    total = sum(len(items) for items in entries.values())
    print(f"已处理 QML 文件：{len(qml_files())}，改写 {wrapped} 个")
    print(f"C++ 文件：{len(CPP_FILES)}，改写 {cpp_wrapped} 个，替换 {cpp_total} 处")
    print(f"翻译条目：{total}（上下文 {len(entries)}）")
    print("已写入 i18n/palune_en_US.ts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
