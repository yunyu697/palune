#!/usr/bin/env python3
"""生成本地模拟更新源（清单 + 安装包 + 镜像目录），用于 M0 更新模块端到端验证。

用法：
  python tools/update/make_test_feed.py --out qa/runtime/feed
  python tools/update/make_test_feed.py --out qa/runtime/feed-bad-signature --variant bad-signature
  python tools/update/make_test_feed.py --out qa/runtime/feed-bad-hash --variant bad-hash

说明：test-key.pem 仅用于本地/CI 测试，正式发布前必须替换为离线保管的发布私钥。
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import shutil
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
KEY_PATH = ROOT / "tools" / "update" / "test-key.pem"
PUB_PATH = ROOT / "resources" / "trust" / "update.pub"
FEED_VERSION = "1.0.0-alpha2"


def canonical(obj: dict) -> bytes:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def installer_script(version: str) -> str:
    return f"""@echo off
rem Palune 模拟安装器（仅测试用）
set APP=%PALUNE_INSTALL_APP%
set MARKER=%PALUNE_INSTALL_MARKER%
echo installed version={version} at %DATE% %TIME% > "%~dp0install-receipt.txt"
if not "%MARKER%"=="" echo installed >> "%MARKER%"
if not "%APP%"=="" start "" "%APP%" --e2e-restarted --e2e-marker "%MARKER%"
exit /b 0
"""


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--variant", default="good", choices=["good", "bad-signature", "bad-hash"])
    ap.add_argument("--mirrors", type=int, default=2)
    args = ap.parse_args()

    out = Path(args.out).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    version = FEED_VERSION
    package_name = f"Palune-{version}-win-x64-setup.cmd"
    package = out / package_name
    package.write_text(installer_script(version), encoding="utf-8")

    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    if args.variant == "bad-hash":
        digest = "0" * 64  # 签名覆盖该错误哈希，用错误包触发 UPD-006

    manifest_payload = {
        "schemaVersion": 1,
        "channel": "alpha",
        "version": version,
        "publishedAt": "2026-11-06T09:30:00Z",
        "notes": {
            "zh-CN": "M1 技术验证版：四层主题骨架与内容源抽象。",
            "en": "M1 technical preview: layered themes and media source abstraction.",
        },
        "installer": {"kind": "cmd", "args": ["--app", "{app}", "--marker", "{marker}"]},
        "mirrors": [
            {"name": f"镜像 {chr(65 + i)}", "url": f"mirror-{chr(97 + i)}/"} for i in range(args.mirrors)
        ],
        "artifacts": [
            {
                "platform": "windows",
                "arch": "x86_64",
                "url": package_name,
                "size": package.stat().st_size,
                "sha256": digest,
                "installer": "cmd",
                "minOsVersion": "10.0.17763",
                "signature": None,
            }
        ],
    }

    key = serialization.load_pem_private_key(KEY_PATH.read_bytes(), password=None)
    # 产物签名覆盖安装包原始字节；清单签名只覆盖 payload。
    manifest_payload["artifacts"][0]["signature"] = {
        "type": "ed25519",
        "value": base64.b64encode(key.sign(package.read_bytes())).decode("ascii"),
    }
    manifest_signature = key.sign(canonical(manifest_payload))
    if args.variant == "bad-signature":
        manifest_signature = b"\x00" * 64

    manifest = {
        "payload": manifest_payload,
        "signature": {
            "type": "ed25519",
            "keyId": "palune-test",
            "value": base64.b64encode(manifest_signature).decode("ascii"),
        },
    }

    for i in range(args.mirrors):
        mirror_dir = out / f"mirror-{chr(97 + i)}"
        mirror_dir.mkdir(exist_ok=True)
        shutil.copy2(package, mirror_dir / package_name)

    (out / "latest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    public_hex = key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw, format=serialization.PublicFormat.Raw
    ).hex()
    print(f"[OK] feed={out}")
    print(f"     version={version} size={manifest['size']} sha256={digest}")
    print(f"     publicKeyHex={public_hex}")
    print(f"     variant={args.variant}")


if __name__ == "__main__":
    main()
