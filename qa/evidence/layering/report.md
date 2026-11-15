# 覆盖/还原类渲染缺陷逐格取证

- 生成时间：2026-11-15 11:57:25
- 被测程序：`D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\Release\Palune.exe`（pid 36540）
- 测试样片：`D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\qa\runtime\media\resume-60s.mp4`（启动后先暂停，画面静止才可前后比对）
- 每格重复次数：3；被测窗口标题：`Palune — resume-60s.mp4`；暂停态静止：True
- 判定：`diff` = 中央区域与参照帧的像素差异占比；`foreign` = 接近遮挡窗品红的像素占比
- 判定阈值：diff > 15% 记为「未通过」（foreign 仅记录，不参与判定）；N 次全挂 = 必现，部分挂 = 偶发，全过 = 当前构建未复现
- 口径：本机只有 1 台显示器、且没有做真实休眠/真实换 DPI，跨屏 / DPI / 休眠三格一律记「未验证」，**不计入通过**
- 口径：未做修正前代码的 A/B 对照，因此通过项一律表述为「当前构建未复现」，不表述为「已修复」
- 机制层：恢复后 `GetWindow(video, GW_HWNDPREV) == overlay` = True；本轮看门狗自愈日志条数 = 22

| 场景 | 判定 | diff(最大) | foreign(最大) | 证据文件 | 说明 |
| --- | --- | --- | --- | --- | --- |
| control-video-behind | 必现（符合预期） | 1 | 0 | control-video-behind-before.png, control-video-behind-broken.png | 故意把视频窗压到最底层后与正常帧比对：diff=1（>15% 说明取证能检出真实缺陷）；截图时 z 序是否仍被破坏=True |
| control-cover-actually-covers | 遮挡生效（前提成立） | 1 | 0 | control-cover-pre.png, control-cover-on.png, control-cover-off.png | 盖上时 diff=1（应 >15%），撤下后 diff=0（应 ≤15%） |
| covered-switchback | 当前构建未复现 | 0 | 0 | covered-switchback-i1-before.png, covered-switchback-i2-before.png, covered-switchback-i3-before.png | 0/3 次中央区域与参照帧差异 >15%（品红占比仅作参考，内容本身含近品红像素） |
| minimize-restore | 当前构建未复现 | 0 | 0 | minimize-restore-i1-before.png, minimize-restore-i2-before.png, minimize-restore-i3-before.png | 0/3 次中央区域与参照帧差异 >15%（品红占比仅作参考，内容本身含近品红像素） |
| alt-tab-roundtrip | 当前构建未复现 | 0 | 0 | alt-tab-roundtrip-i1-before.png, alt-tab-roundtrip-i2-before.png, alt-tab-roundtrip-i3-before.png | 0/3 次中央区域与参照帧差异 >15%（品红占比仅作参考，内容本身含近品红像素） |
| pause-resume | 当前构建未复现 | 0 | 0 | pause-resume-i1-before.png, pause-resume-i2-before.png, pause-resume-i3-before.png | 0/3 次中央区域与参照帧差异 >15%（品红占比仅作参考，内容本身含近品红像素） |
| covered-by-browser | 当前构建未复现 | 0 | 0 | covered-by-browser-i1-after.png, covered-by-browser-i2-after.png, covered-by-browser-i3-after.png | 3/3 次确实被浏览器盖住（覆盖时 diff 最大 1）；切回后 0/3 次与参照帧差异 >15% |
| fullscreen-roundtrip | 当前构建未复现 | 0 | 0 | fullscreen-roundtrip-i1-after.png, fullscreen-roundtrip-i2-after.png, fullscreen-roundtrip-i3-after.png | 0/3 次退全屏后与进入前不一致 |
| theme-cycle-A-B-C-A | 当前构建未复现 | 0 | 0 | theme-cycle-i1-A-after.png, theme-cycle-i2-A-after.png, theme-cycle-i3-A-after.png | 0/3 次回到主题 A 后与切换前不一致 |
| cross-screen-move | 未验证 | 0 | 0 |  | 本机只有 1 台显示器，不具备触发条件（不计入通过） |
| dpi-change | 未验证 | 0 | 0 | dpi-change-i1-before.png, dpi-change-i2-before.png, dpi-change-i3-before.png | 仅注入消息（非真实条件），0/3 次中央区域差异 >15% 仅作参考；不计入通过 |
| sleep-wake | 未验证 | 0 | 0 | sleep-wake-i1-before.png, sleep-wake-i2-before.png, sleep-wake-i3-before.png | 仅注入消息（非真实条件），0/3 次中央区域差异 >15% 仅作参考；不计入通过 |
