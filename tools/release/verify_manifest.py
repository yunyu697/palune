#!/usr/bin/env python3
"""用客户端信任公钥独立校验发布清单：SHA-256、清单签名、产物签名。

与 gen_manifest.py 的签名路径无关：这里只读 resources/trust/*.pub 与磁盘上的产物字节。

用法：
  python tools/release/verify_manifest.py --dir dist/release-1.0.0-rc \
      --out docs/evidence/release-1.0.0-rc/manifest-verify.json
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey


def canonical(obj) -> bytes:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def load_trust(root: Path) -> dict:
    keys = {}
    for name in ("update.pub", "test-update.pub"):
        path = root / "resources" / "trust" / name
        if path.is_file():
            # 信任公钥文件是 64 位十六进制文本，不是二进制
            keys[name] = path.read_text(encoding="utf-8").strip()
    return keys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--expect-version", default="1.0.0")
    args = ap.parse_args()

    dist = Path(args.dir).resolve()
    root = dist.parents[1]
    trust = load_trust(root)
    report = {
        "dir": str(dist),
        "expectedVersion": args.expect_version,
        "trustKeys": trust,
        "manifest": {},
        "artifacts": [],
        "checks": {},
        "verdict": "FAIL",
    }

    envelope = json.loads((dist / "latest.json").read_text(encoding="utf-8"))
    payload = envelope["payload"]
    sig = envelope.get("signature") or {}
    report["manifest"] = {
        "version": payload.get("version"),
        "channel": payload.get("channel"),
        "publishedAt": payload.get("publishedAt"),
        "keyId": sig.get("keyId"),
        "signaturePresent": bool(sig.get("value")),
        "artifactCount": len(payload.get("artifacts", [])),
    }

    # 1) 清单签名：用 update.pub 验 payload（唯一规范化的 JSON）
    ok_manifest_sig = False
    for name, hexkey in trust.items():
        pub = Ed25519PublicKey.from_public_bytes(bytes.fromhex(hexkey))
        try:
            pub.verify(base64.b64decode(sig["value"]), canonical(payload))
            ok_manifest_sig = True
            report["manifest"]["verifiedBy"] = name
            break
        except Exception:
            continue
    report["checks"]["manifestSignature"] = ok_manifest_sig

    # 2) 版本与产物：磁盘字节重新算 SHA-256，再验每个产物的 Ed25519 签名
    ok_version = payload.get("version") == args.expect_version
    report["checks"]["versionMatches"] = ok_version

    ok_artifacts = True
    for item in payload.get("artifacts", []):
        path = dist / Path(item["url"]).name
        entry = {"name": path.name, "url": item["url"], "platform": item.get("platform"),
                 "installer": item.get("installer")}
        if not path.is_file():
            entry["error"] = "missing file"
            ok_artifacts = False
            report["artifacts"].append(entry)
            continue
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        entry["size"] = len(data)
        entry["sizeMatches"] = len(data) == item.get("size")
        entry["sha256"] = digest
        entry["sha256Matches"] = digest.lower() == str(item.get("sha256", "")).lower()
        sig_value = (item.get("signature") or {}).get("value")
        entry["signaturePresent"] = bool(sig_value)
        verified = False
        for hexkey in trust.values():
            pub = Ed25519PublicKey.from_public_bytes(bytes.fromhex(hexkey))
            try:
                pub.verify(base64.b64decode(sig_value), data)
                verified = True
                break
            except Exception:
                continue
        entry["artifactSignature"] = verified
        ok_artifacts &= entry["sizeMatches"] and entry["sha256Matches"] and verified
        report["artifacts"].append(entry)

    # 3) SHA256SUMS 与磁盘一致
    sums_path = dist / "SHA256SUMS"
    sums_ok = sums_path.is_file()
    if sums_ok:
        listed = {}
        for line in sums_path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            digest, name = line.split(None, 1)
            listed[name.strip()] = digest.lower()
        for item in payload.get("artifacts", []):
            name = Path(item["url"]).name
            actual = hashlib.sha256((dist / name).read_bytes()).hexdigest()
            sums_ok &= listed.get(name) == actual
    report["checks"]["sha256sums"] = sums_ok
    report["checks"]["artifacts"] = ok_artifacts

    report["verdict"] = "PASS" if all([
        ok_manifest_sig, ok_version, ok_artifacts, sums_ok,
        report["manifest"]["signaturePresent"],
    ]) else "FAIL"

    out = Path(args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"verdict": report["verdict"], "checks": report["checks"]}, ensure_ascii=False))
    return 0 if report["verdict"] == "PASS" else 2


if __name__ == "__main__":
    sys.exit(main())
