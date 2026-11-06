#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace palune {

// 极简 TOML 读写（只覆盖 settings.toml 用到的子集）：
// 表头 [a.b]、键值 key = value，值支持字符串/布尔/整数/浮点/标量数组，行内 # 注释。
// 未知键在读写过程中保留（读入 QVariantMap，写回时按表原样输出），不做静默丢弃。
namespace Toml {

QVariantMap parse(const QString &text, QString *error = nullptr);
QString serialize(const QVariantMap &values);

} // namespace Toml
} // namespace palune
