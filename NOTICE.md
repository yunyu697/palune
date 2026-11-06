# NOTICE

Palune 以动态链接方式使用下列第三方组件。仓库不包含它们的二进制（`third_party/` 下的动态库不入库，由 `tools/ci/fetch_deps.*` 获取）。

## libmpv（mpv）

- 组件：`libmpv-2.dll` / `libmpv.so.*` / `libmpv.dylib`，以及 `third_party/libmpv/include/mpv/*.h` 头文件（头文件随仓库提供，便于直接编译）
- 许可：LGPL-2.1-or-later（mpv 部分组件为 GPL，Palune 只使用 LGPL 的 libmpv 动态库，不做静态链接）
- 可替换性：程序通过 `QLibrary` 在运行时加载 libmpv，用户可自行替换为兼容版本的动态库，无需重新链接 Palune
- 上游：https://github.com/mpv-player/mpv

## libsodium

- 组件：`libsodium.dll` / `libsodium.so.*` / `libsodium.dylib`（运行时动态加载，用于 Ed25519 更新签名校验）
- 许可：ISC License
- 上游：https://github.com/jedisct1/libsodium

## Qt 6

- 许可：LGPL-3.0（Qt 官方二进制分发条款）/ 商业许可
- 上游：https://www.qt.io/

## 测试密钥

`resources/trust/test-update.pub` 与 `tools/update/gen_test_key.py` 仅用于本地更新流程的端到端验证。
测试私钥 `tools/update/test-key.pem` **不入库**，需要时用 `tools/update/gen_test_key.py` 现场生成。
`resources/trust/update.pub` 为正式发布用的公钥位，发布前必须替换为真实密钥。
