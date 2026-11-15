# Palune.exe 副本逐个真启动取证

- 生成时间：2026-11-15 11:08:15
- 方式：每个副本在独立隐藏桌面（`PaluneQAVerify`）上按「工作目录 = exe 所在目录」启动，等 6s，
  枚举该 pid 的顶层窗口，读 `#32770` 对话框正文原文，然后结束进程。前台不受影响。

| # | 副本 | 版本 | Qt6Core | Qt6Quick | Qt6Widgets | platforms\qwindows.dll | qt.conf | 启动结果 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-before\Palune.exe` |  | True | True | True | False | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; #32770 :: Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIME UI ;; IME :: Default IME |
| 2 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-final\bin\Palune.exe` |  | True | True | True | True | True | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683TrayIconMessageWindowClass :: QTrayIconMessageWindow ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndF… |
| 3 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-final2\bin\Palune.exe` |  | True | True | True | True | True | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683TrayIconMessageWindowClass :: QTrayIconMessageWindow ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndF… |
| 4 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-install-check\bin\Palune.exe` |  | True | True | True | True | True | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683TrayIconMessageWindowClass :: QTrayIconMessageWindow ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndF… |
| 5 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-install-noplugins\bin\Palune.exe` |  | True | True | True | False | True | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; #32770 :: Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIME UI ;; IME :: Default IME |
| 6 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-noplugin2\Palune.exe` |  | False | False | False | False | False | EXITED=1/CODE=0xC0000135/WIN=(none) |
| 7 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-noplugin3\Palune.exe` |  | True | True | True | False | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; #32770 :: Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIME UI ;; IME :: Default IME |
| 8 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-noplugins\Palune.exe` |  | False | False | False | False | False | EXITED=1/CODE=0xC0000135/WIN=(none) |
| 9 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-pkg-current\bin\Palune.exe` |  | True | True | True | True | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683TrayIconMessageWindowClass :: QTrayIconMessageWindow ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndF… |
| 10 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-pkg-noplugins\bin\Palune.exe` |  | True | True | True | False | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; #32770 :: Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIME UI ;; IME :: Default IME |
| 11 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-pkg-old\bin\Palune.exe` |  | True | True | True | True | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683TrayIconMessageWindowClass :: QTrayIconMessageWindow ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndF… |
| 12 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-preflight\Palune.exe` |  | True | True | True | False | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; #32770 :: Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIME UI ;; IME :: Default IME |
| 13 | `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-solo\Palune.exe` |  | False | False | False | False | False | EXITED=1/CODE=0xC0000135/WIN=(none) |
| 14 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\_CPack_Packages\win64\NSIS\Palune-1.0.0-alpha4-win-x64\bin\Palune.exe` |  | True | True | True | True | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIM… |
| 15 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build-m2\Release\Palune.exe` |  | False | False | False | False | False | EXITED=1/CODE=0xC0000135/WIN=(none) |
| 16 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build-m2b\Release\Palune.exe` |  | False | False | False | False | False | EXITED=1/CODE=0xC0000135/WIN=(none) |
| 17 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\_CPack_Packages\win64\NSIS\Palune-1.0.0-RC1-win-x64\bin\Palune.exe` |  | True | True | True | True | True | EXITED=0/CODE=0x00000103/WIN=IME :: Default IME |
| 18 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\_CPack_Packages\win64\WIX\Palune-win-x64-msi\bin\Palune.exe` |  | True | True | True | True | False | EXITED=0/CODE=0x00000103/WIN=IME :: Default IME |
| 19 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\Release\Palune.exe` |  | True | True | True | True | True | EXITED=0/CODE=0x00000103/WIN=IME :: Default IME |
| 20 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\stage-record\bin\Palune.exe` |  | True | True | True | True | True | EXITED=0/CODE=0x00000103/WIN=IME :: Default IME |
| 21 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\qa\runtime\install-tree\bin\Palune.exe` |  | True | True | True | True | False | EXITED=0/CODE=0x00000103/WIN=Touch Tooltip Window :: Tooltip ;; Qt683QWindowIcon :: Palune ;; Qt683QWindowIcon :: Palune Video Surface - Palune ;; CiceroUIWndFrame :: CiceroUIWndFrame ;; CiceroUIWndFrame :: TF_FloatingLangBar_WndTitle ;; MSCTFIME UI :: MSCTFIM… |
| 22 | `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\qa\runtime\soak-bin\Palune.exe` |  | False | False | False | False | False | EXITED=1/CODE=0xC0000135/WIN=(none) |

## MuiCache 里出现过的历史路径

- `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\Release\Palune.exe`（存在）
- `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build-m2\Release\Palune.exe`（存在）
- `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\qa\runtime\install-tree\bin\Palune.exe`（存在）
- `C:\PaluneVerify\bin\Palune.exe`（目录已不存在）
- `C:\Temp\palune-rc-install\bin\Palune.exe`（目录已不存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-solo\Palune.exe`（存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-noplugin3\Palune.exe`（存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-preflight\Palune.exe`（存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-before\Palune.exe`（存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-pkg-old\bin\Palune.exe`（存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-pkg-noplugins\bin\Palune.exe`（存在）
- `C:\Users\Tyler Zhang\AppData\Local\Temp\palune-install-noplugins\bin\Palune.exe`（存在）
- `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\dist\preflight-corrupt-plugin\Palune.exe`（目录已不存在）
- `C:\PaluneVerify2\bin\Palune.exe`（目录已不存在）
- `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\qa\case-noplugin\Palune.exe`（目录已不存在）
- `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\qa\case-nompv\Palune.exe`（目录已不存在）
