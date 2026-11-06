#pragma once

#include <QString>

namespace palune {

// 命名与目录规则（ARCHITECTURE §5.10，M1 起冻结）：
//   Windows  %APPDATA%\Palune
//   macOS    ~/Library/Application Support/Palune
//   Linux    ~/.config/palune
class AppPaths
{
public:
    static QString configDir();
    static QString settingsFile();
    static QString logDir();
    static QString logFile();
    static QString crashDir();
    static QString stateFile();
    static QString updateStagingDir(const QString &version);

    // 便于测试注入根目录；空字符串表示恢复平台默认值。
    static void setConfigDirOverride(const QString &dir);
};

} // namespace palune
