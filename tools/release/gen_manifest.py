#!/usr/bin/env python3
"""生成发布更新清单 latest.json（payload + 分离签名）与 SHA256SUMS。

格式依据：docs/UPDATER_SPEC.md 第 2 节。

- artifacts 数组严格一 platform+arch 一条（Windows=exe、macOS=dmg、Linux=AppImage）；
  msi/deb/rpm 作为同一版本的附加分发格式放进 extraArtifacts，客户端只读 artifacts。
- 签名私钥从环境变量 PALUNE_UPDATE_PRIVATE_KEY 读取（PEM 文本，或其 base64）；
  未配置时明确标记 signed=false 并跳过签名，不伪造签名。
- 产物签名是对文件原始字节做 Ed25519 签名；清单签名只覆盖 payload。

用法：
  python tools/release/gen_manifest.py --dist dist --version 1.0.0-alpha2 \
      --channel alpha --base-url https://github.com/<owner>/<repo>/releases/download/<tag> \
      [--notes "更新说明"] [--key-id palune-update-2026]

输出：<dist>/latest.json、<dist>/SHA256SUMS
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

# 文件名 -> (platform, arch, installer, 是否进入 artifacts 主列表)
PATTERNS = [
    (re.compile(r"^Palune-(?P<ver>.+)-win-x64\.exe$"), "windows", "x86_64", "nsis", True),
    (re.compile(r"^Palune-(?P<ver>.+)-win-x64\.msi$"), "windows", "x86_64", "msi", False),
    (re.compile(r"^Palune-(?P<ver>.+)-macos-arm64\.dmg$"), "macos", "aarch64", "dmg", True),
    (re.compile(r"^Palune-(?P<ver>.+)-linux-x86_64\.AppImage$"), "linux", "x86_64", "appimage", True),
    (re.compile(r"^Palune-(?P<ver>.+)-linux-x86_64\.deb$"), "linux", "x86_64", "deb", False),
    (re.compile(r"^Palune-(?P<ver>.+)-linux-x86_64\.rpm$"), "linux", "x86_64", "rpm", False),
]

MIN_OS = {"windows": "10.0", "macos": "12.0", "linux": "glibc 2.31"}


def canonical(obj) -> bytes:
    """与客户端 QJsonDocument::Compact + 键排序等价的最小规范化。"""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def load_private_key(raw: str):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    raw = raw.strip()
    if "BEGIN" in raw:
        pem = raw.encode("utf-8")
    else:
        decoded = base64.b64decode(raw)
        if b"BEGIN" in decoded:
            pem = decoded
        else:
            return Ed25519PrivateKey.from_private_bytes(decoded)
    return serialization.load_pem_private_key(pem, password=None)


def artifact_entry(path: Path, platform: str, arch: str, installer: str,
                   base_url: str, key) -> dict:
    data = path.read_bytes()
    entry = {
        "platform": platform,
        "arch": arch,
        "url": f"{base_url.rstrip('/')}/{path.name}",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "installer": installer,
        "minOsVersion": MIN_OS.get(platform, ""),
    }
    if key is not None:
        entry["signature"] = {
            "type": "ed25519",
            "value": base64.b64encode(key.sign(data)).decode("ascii"),
        }
    else:
        entry["signature"] = None
    return entry


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dist", required=True, help="产物目录")
    ap.add_argument("--version", required=True)
    ap.add_argument("--channel", required=True, choices=["alpha", "stable", "beta"])
    ap.add_argument("--base-url", required=True, help="Release 资产下载前缀（不含文件名）")
    ap.add_argument("--notes", default="")
    ap.add_argument("--key-id", default=os.environ.get("PALUNE_UPDATE_KEY_ID", "palune-update-2026"))
    args = ap.parse_args()

    dist = Path(args.dist).resolve()
    if not dist.is_dir():
        print(f"[FAIL] 产物目录不存在：{dist}", file=sys.stderr)
        return 1

    key = None
    key_env = os.environ.get("PALUNE_UPDATE_PRIVATE_KEY", "").strip()
    if key_env:
        key = load_private_key(key_env)
    else:
        print("[WARN] 未配置 PALUNE_UPDATE_PRIVATE_KEY，本次清单与产物均未签名（signed=false）",
              file=sys.stderr)

    artifacts, extras = [], []
    for path in sorted(dist.iterdir()):
        if not path.is_file():
            continue
        for pattern, platform, arch, installer, primary in PATTERNS:
            m = pattern.match(path.name)
            if not m:
                continue
            if m.group("ver") != args.version:
                print(f"[FAIL] 文件名版本 {m.group('ver')} 与 VERSION {args.version} 不一致：{path.name}",
                      file=sys.stderr)
                return 1
            entry = artifact_entry(path, platform, arch, installer, args.base_url, key)
            (artifacts if primary else extras).append(entry)
            break

    if not artifacts:
        print("[FAIL] 没有识别到任何主产物（exe/dmg/AppImage）", file=sys.stderr)
        return 1

    payload = {
        "schemaVersion": 1,
        "channel": args.channel,
        "version": args.version,
        "publishedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "notes": {
            "zh-CN": args.notes or f"Palune {args.version}",
            "en": args.notes or f"Palune {args.version}",
        },
        "artifacts": artifacts,
    }
    if extras:
        # 规范之外的分发格式（客户端不读此字段，仅用于发布页与校验脚本）。
        payload["extraArtifacts"] = extras

    if key is not None:
        envelope = {
            "payload": payload,
            "signature": {
                "type": "ed25519",
                "keyId": args.key_id,
                "value": base64.b64encode(key.sign(canonical(payload))).decode("ascii"),
            },
        }
    else:
        envelope = {"payload": payload, "signature": None,
                    "signing": {"signed": False, "reason": "未配置 PALUNE_UPDATE_PRIVATE_KEY"}}

    (dist / "latest.json").write_text(json.dumps(envelope, ensure_ascii=False, indent=2),
                                      encoding="utf-8")

    lines = []
    for path in sorted(dist.iterdir()):
        if path.is_file() and path.name not in {"latest.json", "SHA256SUMS"}:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            lines.append(f"{digest}  {path.name}")
    (dist / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"[OK] latest.json 已生成：{dist / 'latest.json'}")
    print(f"     签名：{'已签名 keyId=' + args.key_id if key else '未签名（缺私钥）'}")
    for entry in artifacts + extras:
        print(f"     {entry['platform']}/{entry['arch']} {entry['installer']} "
              f"{entry['size']} B {entry['sha256'][:12]}…")
    return 0


if __name__ == "__main__":
    sys.exit(main())
