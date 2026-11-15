# 覆盖/还原类渲染缺陷：独立桌面逐格取证（不抢前台）

- 生成时间：2026-11-15 12:29:41
- 被测程序：`D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\Release\Palune.exe`（pid 17772）
- 运行位置：独立桌面 ``（CreateDesktop + STARTUPINFO.lpDesktop），全程不激活、不切前台
- 按键：一律 `PostMessage(WM_KEYDOWN/WM_KEYUP)` 定向到覆盖窗 hwnd，不使用任何真实前台按键序列
- 取像素：`PrintWindow(PW_RENDERFULLCONTENT)` 直接取窗口自身像素，不依赖屏幕合成
- 测试图：`still-fixture.png`（静态彩条，mpv keep-open 停在最后一帧）。动态样片每帧都在变，前后比对不成立

## 判据能说明什么、不能说明什么

- 能说明：窗口自身是否仍在正常渲染（扰动前后像素差异）、双窗 z 序是否仍满足「视频窗紧贴覆盖窗正下方」、窗口是否仍可见。
- 不能说明：用户肉眼看到的**屏幕合成层**（遮挡窗/浏览器撤走后的残留像素）。隐藏桌面不参与 DWM 屏幕合成，该维度必须真实前台，本报告一律标「未在真实前台验证」。
- 取样门控：两次取样差异 0.0000（<0.01 视为画面静止），平均亮度 155.2、色数 33；渲染判据成立
- 呈现证据：5 次取样相邻变化 3/4 次（该实例亮度 104.0/104.0/104.3/104.9/104.0）。相邻取样无变化时，「差异 0」分不清「画面对」与「两边都黑」，只能写「未测出问题」

| 场景 | 判定 | 结构判据 | 渲染连续性 | 说明 |
| --- | --- | --- | --- | --- |
| covered-switchback | 当前构建未复现 | z 序异常 0/3 次 | 0/3 次差异 >15%（最大 0.0000）；基线 155.2/非黑 95% → 结果 155.2/非黑 95% | 遮挡方是同桌面纯 GDI 窗，不等于浏览器/会议软件这类 DWM 合成窗（后者见 covered-by-browser，未验证）；证据：covered-switchback-h1-after.png, covered-switchback-h2-after.png, covered-switchback-h3-after.png |
| pause-resume | 当前构建未复现 | z 序异常 0/3 次 | 0/3 次差异 >15%（最大 0.0000）；基线 155.2/非黑 95% → 结果 155.2/非黑 95% | pause-resume-h1-after.png, pause-resume-h2-after.png, pause-resume-h3-after.png |
| theme-cycle | 当前构建未复现 | z 序异常 0/3 次 | 0/3 次差异 >15%（最大 0.0000）；基线 155.2/非黑 95% → 结果 155.2/非黑 95% | 主题作用于覆盖窗；像素判据看视频窗，仅能证明视频窗未被主题切换破坏；证据：theme-cycle-h1-after.png, theme-cycle-h2-after.png, theme-cycle-h3-after.png |
| fullscreen-roundtrip | 未验证（还原后取不到可比画面） | z 序异常 0/3 次 | 0/3 次差异 >15%（最大 0.0000）；基线 155.2/非黑 95% → 结果 155.2/非黑 95% | 全屏会重建视频表面，独立桌面无合成器驱动重绘，还原后的画面不可比；证据：fullscreen-roundtrip-h1-after.png, fullscreen-roundtrip-h2-after.png, fullscreen-roundtrip-h3-after.png |
| minimize-restore | 未验证（还原后取不到可比画面） | z 序异常 0/3 次 | 1/1 次差异 >15%（最大 1.0000）；另 2 次状态未回到基线未比对；还原后近全黑 1/3 次；基线 155.2/非黑 95% → 结果 1.5/非黑 1% | 最小化销毁交换链，独立桌面无新帧驱动重绘，还原后近全黑，画面不可比；证据：minimize-restore-h1-after.png, minimize-restore-h2-before.png, minimize-restore-h3-before.png |
| alt-tab-roundtrip | 未在真实前台验证 | — | — | Alt+Tab 的语义就是前景切换，独立桌面上无法产生；按要求不用 SetForegroundWindow 伪造，也不回落用户桌面补数 |
| covered-by-browser | 未在真实前台验证 | — | — | 浏览器遮挡依赖 DWM 合成层，隐藏桌面观测不到合成结果；按要求不在用户桌面补跑 |
| cross-screen-move | 未验证 | — | — | 本机只有 1 台显示器，不具备触发条件（不计入通过） |
| dpi-change | 未验证 | — | — | 没有第二台不同 DPI 的显示器；注入 WM_DPICHANGED 不是真实条件（本轮未注入） |
| sleep-wake | 未验证 | — | — | 真休眠会中断整机；注入 PBT_APMSUSPEND 不是真实条件（本轮未注入） |

## 口径

- 跨屏 / DPI / 休眠三格记「未验证」，单列，不计入通过。
- Alt+Tab 与浏览器遮挡两格记「未在真实前台验证」，不计入通过。
- 未做修正前代码 A/B，通过项一律表述为「当前构建未复现」，不是「已修复」。
