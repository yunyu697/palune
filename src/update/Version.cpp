#include "update/Version.h"

#include <QRegularExpression>

namespace palune {

namespace {
const QRegularExpression kPattern(
    QStringLiteral("^(?<major>0|[1-9]\\d*)\\.(?<minor>0|[1-9]\\d*)\\.(?<patch>0|[1-9]\\d*)"
                   "(?:-alpha(?<alpha>[1-9]\\d*))?$"));
}

Version Version::parse(const QString &text, QString *error)
{
    Version v;
    const auto m = kPattern.match(text.trimmed());
    if (!m.hasMatch()) {
        if (error)
            *error = QStringLiteral("版本号格式不合法：%1（应为 MAJOR.MINOR.PATCH-alphaN）").arg(text);
        return v;
    }
    v.major = m.captured("major").toInt();
    v.minor = m.captured("minor").toInt();
    v.patch = m.captured("patch").toInt();
    if (m.captured("alpha").isEmpty()) {
        v.hasAlpha = false;
    } else {
        v.hasAlpha = true;
        v.alpha = m.captured("alpha").toInt();
    }
    v.valid = true;
    if (error)
        validateVersionRule(v, error);
    return v;
}

QString Version::toString() const
{
    QString s = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (hasAlpha)
        s += QStringLiteral("-alpha%1").arg(alpha);
    return s;
}

bool Version::operator==(const Version &o) const
{
    return major == o.major && minor == o.minor && patch == o.patch
        && hasAlpha == o.hasAlpha && alpha == o.alpha;
}

bool Version::operator>(const Version &o) const
{
    if (major != o.major) return major > o.major;
    if (minor != o.minor) return minor > o.minor;
    if (patch != o.patch) return patch > o.patch;
    // 正式版高于同号预发布版
    if (!hasAlpha && o.hasAlpha) return true;
    if (hasAlpha && !o.hasAlpha) return false;
    return alpha > o.alpha;
}

bool validateVersionRule(const Version &v, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error) *error = msg;
        return false;
    };
    if (!v.valid)
        return fail(QStringLiteral("版本号无法解析"));
    if (v.minor > 10)
        return fail(QStringLiteral("MINOR 段不得超过 10：%1").arg(v.minor));
    if (v.patch > 10)
        return fail(QStringLiteral("PATCH 段不得超过 10：%1").arg(v.patch));
    if (v.hasAlpha && (v.alpha < 1 || v.alpha > 10))
        return fail(QStringLiteral("alpha 编号必须在 1..10，且到 10 之后转为 1.0.0：%1").arg(v.alpha));
    if (!v.hasAlpha && v.toString() != QStringLiteral("1.0.0"))
        return fail(QStringLiteral("仅正式版 1.0.0 可以不带 alpha 后缀：%1").arg(v.toString()));
    return true;
}

} // namespace palune
