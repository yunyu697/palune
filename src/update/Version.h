#pragma once

#include <QString>

namespace palune {

// 开发期版本格式：MAJOR.MINOR.PATCH-alphaN
// 硬约束：除 MAJOR 外任何数值段 <= 10；alpha 编号 <= 10。
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int alpha = 0;          // 仅 hasAlpha 时有效，1..10
    bool hasAlpha = false;
    bool valid = false;

    static Version parse(const QString &text, QString *error = nullptr);
    QString toString() const;
    bool operator>(const Version &other) const;
    bool operator<(const Version &other) const { return other > *this; }
    bool operator==(const Version &other) const;
};

// 版本号规则校验（脚本与单元测试共用同一判定）。
bool validateVersionRule(const Version &v, QString *error);

} // namespace palune
