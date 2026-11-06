#!/usr/bin/env python3
"""生成（或读取）M0 测试用 Ed25519 密钥对，并写入内置公钥资源。"""

from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
KEY_PATH = ROOT / "tools" / "update" / "test-key.pem"
PUB_PATH = ROOT / "resources" / "trust" / "update.pub"
TEST_PUB_PATH = ROOT / "resources" / "trust" / "test-update.pub"

if KEY_PATH.exists():
    key = serialization.load_pem_private_key(KEY_PATH.read_bytes(), password=None)
else:
    key = Ed25519PrivateKey.generate()
    KEY_PATH.write_bytes(
        key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )

public_hex = key.public_key().public_bytes(
    encoding=serialization.Encoding.Raw, format=serialization.PublicFormat.Raw
).hex()
PUB_PATH.write_text(public_hex + "\n", encoding="utf-8")
TEST_PUB_PATH.write_text(public_hex + "\n", encoding="utf-8")
print(public_hex)
