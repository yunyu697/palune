#pragma once

#include <QString>

namespace palune {
namespace credential {

// 系统凭据存储（ARCHITECTURE.md §5.5）：机密不落 settings.toml。
// Windows 走 Credential Manager；macOS 走 Keychain（security 命令行）；Linux 走 libsecret（secret-tool）。
// 任一平台不可用时返回错误，调用方不得回退到明文落盘。
bool supported();

// 不存在时返回空串且 error 为空；系统错误时返回空串并填充 error。
QString read(const QString &service, const QString &account, QString *error = nullptr);
bool write(const QString &service, const QString &account, const QString &secret,
           QString *error = nullptr);
bool remove(const QString &service, const QString &account, QString *error = nullptr);

} // namespace credential
} // namespace palune
