#!/usr/bin/env python3
"""生成 M1 冒烟用的 20 个媒体样本（合法可分发，全部由 FFmpeg 合成）。

用法：
  python tools/qa/gen_samples.py --out qa/runtime/samples [--force]

样本覆盖：MP4/MKV/WEBM/TS/AVI/MOV 容器；H.264/HEVC/VP9/VP8/MPEG-2/MPEG-4/MJPEG 编码；
1080p/720p/480p；多帧率；AAC/MP3/Opus/Vorbis/AC3/FLAC/PCM；单/双音轨与字幕轨。
第 1 个样本固定为 1080p H.264，供 --m1-perf 与 --m1-theme-switch 使用。

依赖：ffmpeg（在 PATH 中即可）。不联网、不访问用户媒体库。
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DURATION = 8
SINE = "sine=frequency=440:sample_rate=44100:duration=%d" % DURATION


def testsrc(width: int, height: int, fps: int) -> str:
    return f"testsrc2=size={width}x{height}:rate={fps}:duration={DURATION}"


# (文件名, 视频参数, 音频参数, 骨架/容器, 额外参数)
SPECS = [
    ("01-h264-1080p-aac.mp4", testsrc(1920, 1080, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "mp4", []),
    ("02-h264-720p-aac.mkv", testsrc(1280, 720, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "matroska", []),
    ("03-h264-480p-aac.mp4", testsrc(854, 480, 25),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "96k", "-ac", "2"], "mp4", []),
    ("04-hevc-1080p-aac.mp4", testsrc(1920, 1080, 30),
     ["-c:v", "libx265", "-preset", "ultrafast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "mp4", ["-tag:v", "hvc1"]),
    ("05-hevc-720p-aac.mkv", testsrc(1280, 720, 30),
     ["-c:v", "libx265", "-preset", "ultrafast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "matroska", []),
    ("06-vp9-720p-opus.webm", testsrc(1280, 720, 30),
     ["-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "8", "-pix_fmt", "yuv420p"],
     ["-c:a", "libopus", "-b:a", "96k", "-ac", "2"], "webm", []),
    ("07-vp8-480p-vorbis.webm", testsrc(854, 480, 25),
     ["-c:v", "libvpx", "-deadline", "realtime", "-cpu-used", "8", "-pix_fmt", "yuv420p"],
     ["-c:a", "libvorbis", "-q:a", "3", "-ac", "2"], "webm", []),
    ("08-mpeg2-1080p-mp2.ts", testsrc(1920, 1080, 25),
     ["-c:v", "mpeg2video", "-b:v", "4000k"],
     ["-c:a", "mp2", "-b:a", "192k", "-ac", "2"], "mpegts", []),
    ("09-h264-1080p-ac3-51.mkv", testsrc(1920, 1080, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "ac3", "-b:a", "384k", "-ac", "6"], "matroska", []),
    ("10-mpeg4-720p-mp3.avi", testsrc(1280, 720, 25),
     ["-c:v", "mpeg4", "-b:v", "2500k"],
     ["-c:a", "libmp3lame", "-b:a", "128k", "-ac", "2"], "avi", []),
    ("11-mjpeg-720p-pcm.mov", testsrc(1280, 720, 25),
     ["-c:v", "mjpeg", "-q:v", "5"],
     ["-c:a", "pcm_s16le", "-ac", "2"], "mov", []),
    ("12-h264-1080p-multiaudio.mkv", testsrc(1920, 1080, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "matroska", ["dual-audio"]),
    ("13-h264-1080p-subtitle.mkv", testsrc(1920, 1080, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "matroska", ["subtitle"]),
    ("14-h264-720p-videoonly.mp4", testsrc(1280, 720, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     [], "mp4", []),
    ("15-h264-1080p-60fps-aac.mp4", testsrc(1920, 1080, 60),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "mp4", []),
    ("16-hevc-720p-opus.mkv", testsrc(1280, 720, 30),
     ["-c:v", "libx265", "-preset", "ultrafast", "-pix_fmt", "yuv420p"],
     ["-c:a", "libopus", "-b:a", "96k", "-ac", "2"], "matroska", []),
    ("17-h264-720p-aac.ts", testsrc(1280, 720, 25),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "128k", "-ac", "2"], "mpegts", []),
    ("18-h264-480p-flac.mkv", testsrc(854, 480, 25),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "flac", "-ac", "2"], "matroska", []),
    ("19-h264-1080p-aac-51.mp4", testsrc(1920, 1080, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "aac", "-b:a", "256k", "-ac", "6"], "mp4", []),
    ("20-h264-1080p-wav.mkv", testsrc(1920, 1080, 30),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"],
     ["-c:a", "pcm_s16le", "-ac", "2"], "matroska", []),
]

# 某些 ffmpeg 发行版（如 Homebrew 的精简构建）不带 libvorbis 编码器；
# 合成样本时按可用编码器回退，避免整个冒烟作业因单个样本失败。
AUDIO_FALLBACKS = {"libvorbis": "libopus"}
AVAILABLE_ENCODERS: set[str] = set()


def probe_encoders() -> set[str]:
    try:
        text = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"],
                              capture_output=True, text=True, check=True).stdout
    except subprocess.CalledProcessError:
        return set()
    names = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and len(parts[0]) == 6 and parts[0][0] in "VAS":
            names.add(parts[1])
    return names


SRT = """1
00:00:01,000 --> 00:00:04,000
Palune 字幕轨测试

2
00:00:04,500 --> 00:00:07,500
第二条字幕
"""


def build_one(out: Path, spec, tmpdir: Path) -> None:
    name, video, vargs, aargs, container, extra = spec
    target = out / name
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
           "-f", "lavfi", "-i", video]
    maps = ["0:v"]
    next_input = 1

    if "dual-audio" in extra:
        cmd += ["-f", "lavfi", "-i", SINE]
        cmd += ["-f", "lavfi", "-i",
                "sine=frequency=880:sample_rate=44100:duration=%d" % DURATION]
        maps += [f"{next_input}:a", f"{next_input + 1}:a"]
        next_input += 2
    elif aargs:
        cmd += ["-f", "lavfi", "-i", SINE]
        maps += [f"{next_input}:a"]
        next_input += 1

    if "subtitle" in extra:
        sub = tmpdir / "sample.srt"
        sub.write_text(SRT, encoding="utf-8")
        cmd += ["-f", "srt", "-i", str(sub)]
        maps += [f"{next_input}:s"]

    for m in maps:
        cmd += ["-map", m]
    cmd += vargs
    if aargs:
        if "-c:a" in aargs:
            index = aargs.index("-c:a") + 1
            codec = aargs[index]
            fallback = AUDIO_FALLBACKS.get(codec)
            if AVAILABLE_ENCODERS and codec not in AVAILABLE_ENCODERS and fallback:
                print(f"[WARN] {name}：编码器 {codec} 不可用，改用 {fallback}")
                aargs = list(aargs)
                aargs[index] = fallback
        cmd += aargs
    if "subtitle" in extra:
        cmd += ["-c:s", "srt"]
    cmd += ["-t", str(DURATION), "-f", container, str(target)]

    subprocess.run(cmd, check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="qa/runtime/samples")
    ap.add_argument("--force", action="store_true", help="覆盖已存在的样本")
    args = ap.parse_args()

    if shutil.which("ffmpeg") is None:
        print("[FAIL] 未找到 ffmpeg，请先安装（Windows: winget install Gyan.FFmpeg）", file=sys.stderr)
        return 1

    AVAILABLE_ENCODERS.update(probe_encoders())

    root = Path(__file__).resolve().parents[2]
    out = Path(args.out)
    if not out.is_absolute():
        out = root / out
    out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        for spec in SPECS:
            name = spec[0]
            target = out / name
            if target.exists() and not args.force:
                print(f"[SKIP] {name} 已存在")
                continue
            print(f"[GEN ] {name}")
            build_one(out, spec, tmpdir)

    files = sorted(p for p in out.iterdir() if p.is_file())
    total = sum(p.stat().st_size for p in files)
    print(f"[OK] 共 {len(files)} 个样本，总计 {total / 1024 / 1024:.1f} MB，目录 {out}")
    return 0 if len(files) >= 20 else 2


if __name__ == "__main__":
    sys.exit(main())
