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
    # P0 界面语言、存储、媒体库与 B 布局新增文案
    " / ": " / ",
    " · 切换语言需重启 Palune": " · Restart Palune to apply",
    " · 滞回 ±": " · hysteresis ±",
    " 处键位冲突（同作用域内相同组合键）": " shortcut conflicts (same combo in the same scope)",
    " 项）": " items)",
    "30 秒": "30 s",
    "60 秒": "60 s",
    "BT.2390 色调映射": "BT.2390 tone mapping",
    "HDR ": "HDR ",
    "HDR 色调映射（BT.2390）": "HDR tone mapping (BT.2390)",
    "[audio] channels = auto / stereo / 5.1，即时生效":
        "[audio] channels = auto / stereo / 5.1; applies immediately",
    "[audio] delay，默认 0.0s；音画不同步时微调，即时生效":
        "[audio] delay, default 0.0s; fine-tune when audio and video are out of sync; applies immediately",
    "[audio] device，默认跟随系统；列表由 mpv 的 audio-device-list 提供，即时生效":
        "[audio] device, follows the system by default; the list comes from the player audio-device-list and applies immediately",
    "[playback] autoNext，默认关；播完当前文件后顺序播放下一个同目录文件":
        "[playback] autoNext, off by default; plays the next file in the same folder after the current one ends",
    "[playback] cacheSeconds = 0/30/60；0 表示 mpv 默认，下次打开文件生效":
        "[playback] cacheSeconds = 0/30/60; 0 uses the player default and applies the next time a file is opened",
    "[playback] deinterlace = auto / on / off，即时生效":
        "[playback] deinterlace = auto / on / off; applies immediately",
    "[playback] hwdec = auto / hardware / off，即时生效（切文件后重新初始化解码器）":
        "[playback] hwdec = auto / hardware / off; applies immediately (reinitializes the decoder after switching files)",
    "[playback] resume，默认开；关闭后不再自动跳回上次位置":
        "[playback] resume, on by default; when off, playback no longer jumps back to the previous position",
    "[playback] targetPeakNits，默认 1000 nits，即时生效":
        "[playback] targetPeakNits, default 1000 nits; applies immediately",
    "[playback] toneMapping，默认开；关闭时交给 mpv 选映射算法":
        "[playback] toneMapping, on by default; when off, the player chooses the mapping algorithm",
    "[sub] autoLoad，默认开（模糊匹配同名字幕）":
        "[sub] autoLoad, on by default (fuzzy-matches subtitles with the same name)",
    "[sub] color，默认白，即时生效":
        "[sub] color, white by default; applies immediately",
    "[sub] delay，默认 0.0s（正值为字幕推迟），即时生效":
        "[sub] delay, default 0.0s (positive values delay subtitles); applies immediately",
    "[sub] encoding，默认自动检测；乱码时手动指定":
        "[sub] encoding, auto-detected by default; set manually if text is garbled",
    "[sub] fontSize，默认 15，即时生效":
        "[sub] fontSize, default 15; applies immediately",
    "[sub] outline，默认 1.0px，即时生效":
        "[sub] outline, default 1.0px; applies immediately",
    "[sub] position，默认贴底 8%，即时生效":
        "[sub] position, default 8% from the bottom; applies immediately",
    "[ui] autoPlay，默认开": "[ui] autoPlay, on by default",
    "[ui] closeToTray，默认关；托盘菜单「退出」始终真正退出":
        "[ui] closeToTray, off by default; the tray menu \"Exit\" always quits",
    "[ui] confirmExit，默认关": "[ui] confirmExit, off by default",
    "[ui] minimizeToTray，默认关": "[ui] minimizeToTray, off by default",
    "[ui] startMaximized，默认关（下次启动生效）":
        "[ui] startMaximized, off by default (applies on next launch)",
    "[update] autoCheck，默认开；启动 3 秒后静默检查，只提示下载，不自动安装":
        "[update] autoCheck, on by default; checks silently 3 seconds after launch, only prompts to download, never auto-installs",
    "[update] channel = stable / beta；beta 读取 latest-beta.json，需发行仓库发布该清单后才有内容":
        "[update] channel = stable / beta; beta reads latest-beta.json and has no content until the release repository publishes it",
    "[update] proxy，默认跟随系统；即时应用，下次启动仍读取该值":
        "[update] proxy, follows the system by default; applies immediately and is read again on next launch",
    "px": "px",
    "下载目录": "Download folder",
    "不使用代理": "No proxy",
    "信息": "Info",
    "修改日期": "Modified",
    "全屏 / 还原": "Fullscreen / Restore",
    "全部恢复默认": "Restore all defaults",
    "关闭字幕": "Subtitles off",
    "关闭窗口时留在托盘": "Keep running in the tray when the window closes",
    "关闭（软解）": "Off (software decoding)",
    "写入 [diag] logLevel，即时生效；debug 仅用于排查，会产生大量日志":
        "Writes [diag] logLevel; applies immediately. Use debug only for troubleshooting; it produces a lot of logs",
    "写入 [library] viewMode = poster / list，即时生效，重启后保持":
        "Writes [library] viewMode = poster / list; applies immediately and persists across restarts",
    "写入 [storage] logRetentionDays，默认 14 天；修改后立即清理一次，启动时再清理":
        "Writes [storage] logRetentionDays, default 14 days; cleans up immediately after a change and again at startup",
    "写入 [storage] thumbCacheLimitMb，默认 2000 MB；下次扫描结束时按最旧优先淘汰":
        "Writes [storage] thumbCacheLimitMb, default 2000 MB; evicts oldest thumbnails at the end of the next scan",
    "写入 [ui] language：system / zh_CN / en_US，默认跟随系统，重启后生效":
        "Writes [ui] language: system / zh_CN / en_US; follows the system by default and applies after a restart",
    "列表": "List",
    "加载外挂字幕…": "Load external subtitles…",
    "去隔行": "Deinterlace",
    "取消改键": "Cancel rebinding",
    "名称": "Name",
    "启动与关闭": "Startup & shutdown",
    "启动时最大化": "Start maximized",
    "启动时自动打开最近播放的文件（[ui] restoreLastSession，默认关）":
        "Open the most recently played file at startup ([ui] restoreLastSession, off by default)",
    "启动时自动检查更新": "Check for updates at startup",
    "声道": "Channels",
    "大小": "Size",
    "天": "days",
    "媒体": "Media",
    "媒体信息": "Media info",
    "媒体库还是空的": "Your library is empty",
    "媒体库：": "Library: ",
    "字号": "Font size",
    "字号 %1 · 描边 %2px · 底部安全距 %3%":
        "Font size %1 · outline %2px · bottom safe distance %3%",
    "字幕样式": "Subtitle style",
    "字幕编码": "Subtitle encoding",
    "字幕预览": "Subtitle preview",
    "字幕预览：字号、颜色、描边、位置按左侧设置实时变化":
        "Subtitle preview: font size, color, outline, and position follow the settings on the left in real time",
    "存储": "Storage",
    "导入配置（重启生效）…": "Import settings (restart required)…",
    "导出配置…": "Export settings…",
    "就绪": "Ready",
    "工作台": "Workbench",
    "底部位置": "Bottom position",
    "延迟": "Delay",
    "开启": "On",
    "强制硬解": "Force hardware decoding",
    "当前文件没有章节信息": "No chapter information for this file",
    "当前生效：": "Currently active: ",
    "当前：": "Current: ",
    "快捷键（共 ": "Shortcuts (",
    "恢复上次会话": "Restore last session",
    "打开媒体库": "Open library",
    "打开崩溃目录": "Open crash folder",
    "打开文件…": "Open file…",
    "打开文件后自动播放": "Autoplay after opening a file",
    "打开索引目录": "Open index folder",
    "打开配置目录": "Open config folder",
    "扫描中…": "Scanning…",
    "按上限立即清理": "Clean up to the limit now",
    "按键中…": "Press keys…",
    "描边": "Outline",
    "提示：目标峰值来自播放页的「目标峰值」，修改后立即作用于 mpv。":
        "Note: the target peak comes from \"Target peak\" on the playback page and takes effect immediately in the player.",
    "播放 / 暂停": "Play / Pause",
    "播放行为": "Playback behavior",
    "播放速度在退出时写入 [playback] speed，下次启动恢复。":
        "Playback speed is written to [playback] speed on exit and restored on next launch.",
    "改键": "Rebind",
    "断点续播": "Resume playback",
    "日志保留天数": "Log retention days",
    "日志级别": "Log level",
    "时长 ": "Duration ",
    "时长 —": "Duration —",
    "映射目标峰值": "Mapping target peak",
    "曲线预览不可用": "Curve preview unavailable",
    "曲线预览不可用\\n缺少真实 BT.2390 数据源，1.0 不提供图形曲线（非占位图）":
        "Curve preview unavailable\\nNo real BT.2390 data source; 1.0 does not ship a graphical curve (not a placeholder).",
    "更新通道": "Update channel",
    "最小化时隐藏到托盘": "Hide to tray when minimized",
    "本地模式：不提供账户、登录与云端同步。账户体系无后端支撑，1.1 之后显式延后，不计入 1.0.0 范围。":
        "Local mode: no accounts, sign-in, or cloud sync. The account system has no backend and is explicitly deferred beyond 1.1; it is out of scope for 1.0.0.",
    "条目 %1 · 根目录 %2 · 状态：%3": "Entry %1 · root %2 · status: %3",
    "查看许可与开源声明": "View licenses and open-source notices",
    "格式": "Format",
    "检测到 ": "Detected ",
    "正在扫描": "Scanning",
    "正在等待新快捷键…按 Esc 取消": "Waiting for a new shortcut… press Esc to cancel",
    "正式版": "Stable",
    "测试版": "Beta",
    "海报": "Poster",
    "海报墙": "Poster wall",
    "添加目录": "Add folder",
    "添加目录…": "Add folder…",
    "清理缩略图缓存": "Clean thumbnail cache",
    "点击「改键」后直接按下新组合键；「解绑」置为空；「默认」删除自定义项。写入 [keys] <动作ID>，即时生效并重启保持。":
        "Click \"Rebind\" and press the new combination; \"Unbind\" clears it; \"Default\" removes the custom entry. Writes [keys] <action ID>; applies immediately and persists across restarts.",
    "界面语言": "Interface language",
    "白色": "White",
    "目录": "Folder",
    "目标峰值 %1 nits（playback.targetPeakNits，即时生效）":
        "Target peak %1 nits (playback.targetPeakNits, applies immediately)",
    "硬件解码": "Hardware decoding",
    "硬解 ": "HW decode ",
    "移除": "Remove",
    "窗口宽度低于该值切到紧凑布局，写入 [ui.layout.<主题>] compactBelow，即时生效（360–1600）":
        "Below this window width the compact layout is used; writes [ui.layout.<theme>] compactBelow; applies immediately (360–1600)",
    "立体声": "Stereo",
    "章节": "Chapters",
    "简体中文": "Simplified Chinese",
    "索引文件：": "Index file: ",
    "紧凑断点": "Compact breakpoint",
    "绿色": "Green",
    "缓存与索引占用": "Cache and index usage",
    "缓存时长": "Cache duration",
    "缩略图 %1 · 索引 %2 · 日志 %3 · 配置 %4 · 合计 %5":
        "Thumbnails %1 · index %2 · logs %3 · config %4 · total %5",
    "缩略图缓存上限": "Thumbnail cache limit",
    "网络代理": "Network proxy",
    "自动": "Auto",
    "自动加载外挂字幕": "Load external subtitles automatically",
    "自动播放下一集": "Autoplay next episode",
    "自动（原样）": "Auto (as-is)",
    "自动（系统默认）": "Auto (system default)",
    "解码 ": "Decode ",
    "解码 —": "Decode —",
    "解绑": "Unbind",
    "警告": "Warning",
    "许可与诊断": "Licenses & diagnostics",
    "诊断": "Diagnostics",
    "调试": "Debug",
    "账户": "Account",
    "跟随系统": "Follow system",
    "跟随系统（当前界面文案）": "Follow system (current UI language)",
    "输出": "Output",
    "输出设备": "Output device",
    "还没有添加视频目录。点击「添加目录…」后会扫描并生成缩略图。":
        "No video folders yet. Click \"Add folder…\" to scan and generate thumbnails.",
    "这是一行用于校准字号与描边的字幕预览":
        "This is a subtitle preview for calibrating font size and outline",
    "退出": "Exit",
    "退出前确认": "Confirm before exit",
    "选择…": "Choose…",
    "速度 ": "Speed ",
    "速度 —": "Speed —",
    "配置目录：": "Config folder: ",
    "重建媒体库索引": "Rebuild library index",
    "重新扫描": "Rescan",
    "错误": "Error",
    "队列": "Queue",
    "青色": "Cyan",
    "音轨 — · 字幕 —": "Audio — · subtitles —",
    "音量与静音状态在退出时写入 [audio] volume / muted，下次启动恢复。":
        "Volume and mute state are written to [audio] volume / muted on exit and restored on next launch.",
    "音频延迟": "Audio delay",
    "预览": "Preview",
    "颜色": "Color",
    "黄色": "Yellow",
    "默认": "Default",
    "默认视图": "Default view",
    "（": "(",
    "（尚未生成）": " (not generated yet)",
    "（未绑定）": " (unbound)",
    "）：": "): ",
    "媒体服务器（Jellyfin / Emby）": "Media server (Jellyfin / Emby)",
    "极空间、飞牛等兼容 Jellyfin 协议的设备请选「其他 Jellyfin」。密码或 API Key 保存在系统凭据存储（Windows 凭据管理器 / macOS 钥匙串 / libsecret），不写入配置文件。":
        "Choose \"Other Jellyfin\" for Jellyfin-compatible devices such as ZSpace or FeiNiu. "
        "Passwords and API keys are stored in the system credential store "
        "(Windows Credential Manager / macOS Keychain / libsecret) and never written to the config file.",
    "其他 Jellyfin（极空间 / 飞牛）": "Other Jellyfin (ZSpace / FeiNiu)",
    "用户名 + 密码": "Username + password",
    "服务器地址": "Server address",
    "用户名": "Username",
    "Jellyfin 用户名": "Jellyfin username",
    "密码": "Password",
    "控制台 → 高级 → API 密钥": "Dashboard → Advanced → API Keys",
    "登录方式": "Sign-in method",
    "测试连接": "Test connection",
    "保存并连接": "Save and connect",
    "正在测试连接…": "Testing connection…",
    "正在保存并拉取媒体库…": "Saving and loading library…",
    "删除连接": "Remove connection",
    "未连接": "Not connected",
    "类型": "Type",
    "经典": "Classic",
    "沉浸": "Immersive",
    "经典：顶部悬浮标题条 + 底部双行控制栏，无侧栏":
        "Classic: floating title bar at the top plus a two-row control bar at the bottom, no side panel",
    "工作台：顶部扁平栏 + 右侧常驻检查器，信息密度最高":
        "Workbench: flat top bar with a persistent right inspector, highest information density",
    "沉浸：左上标题胶囊 + 底部悬浮胶囊，最少干扰":
        "Immersive: floating title pill at the top-left and a floating bottom pill, minimal distraction",
    "[sub] outline，默认 1.0（播放器实际描边 2px = outline × 2，预览同口径），即时生效":
        "[sub] outline, default 1.0 (player draws 2 px = outline × 2; preview uses the same scale), applied immediately",
    "API Key": "API Key",
    "输入 0–10000 nits（虚线=不压缩）": "Enter 0–10000 nits (dashed = no compression)",
    "拐点 ≈ %1 nits · 10000 nits 输入映射到 %2 nits":
        "Knee ≈ %1 nits · 10000 nits input maps to %2 nits",
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
            # 已包在 qsTr() 里的字面量一律登记（含非 CJK 的标点/单位片段），
            # 否则 check_i18n 会判为缺翻译；未包裹的只需处理含 CJK 的文案。
            if line[:match.start()].rstrip().endswith("qsTr("):
                sources.append(literal[1:-1])
                continue
            if not CJK.search(literal):
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
