#include "update/UpdateManifest.h"

#include "update/Version.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace palune {

QByteArray UpdateManifest::canonicalJson(const QJsonValue &value)
{
    // QJsonObject 内部按键排序，Compact 输出无空白；与签名工具的
    // json.dumps(sort_keys=True, separators=(',', ':')) 等价。
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    if (value.isString())
        return '"' + value.toString().toUtf8() + '"';
    if (value.isBool())
        return value.toBool() ? "true" : "false";
    if (value.isDouble())
        return QByteArray::number(value.toDouble(), 'g', 17);
    return "null";
}

UpdateManifest UpdateManifest::parse(const QByteArray &json,
                                     const QUrl &feedUrl,
                                     QString *error)
{
    UpdateManifest m;
    m.feedUrl = feedUrl;
    auto fail = [&](const QString &msg) {
        m.valid = false;
        m.error = msg;
        if (error) *error = msg;
        return m;
    };

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("更新清单不是合法 JSON：%1").arg(parseError.errorString()));

    m.raw = doc.object();
    m.version = m.raw.value(QStringLiteral("version")).toString();
    m.publishedAt = m.raw.value(QStringLiteral("publishedAt")).toString();
    m.arch = m.raw.value(QStringLiteral("arch")).toString();
    m.minOs = m.raw.value(QStringLiteral("minOs")).toString();
    m.url = m.raw.value(QStringLiteral("url")).toString();
    m.sha256 = m.raw.value(QStringLiteral("sha256")).toString().toLower();
    m.notes = m.raw.value(QStringLiteral("notes")).toString();
    m.size = static_cast<qint64>(m.raw.value(QStringLiteral("size")).toDouble());
    m.signatureBase64 = m.raw.value(QStringLiteral("signature")).toString();

    if (m.version.isEmpty())
        return fail(QStringLiteral("更新清单缺少 version"));
    QString versionError;
    const Version v = Version::parse(m.version, &versionError);
    if (!v.valid || !validateVersionRule(v, &versionError))
        return fail(QStringLiteral("更新清单版本号不合规：%1").arg(versionError));
    if (m.url.isEmpty())
        return fail(QStringLiteral("更新清单缺少 url"));
    if (m.sha256.size() != 64)
        return fail(QStringLiteral("更新清单 sha256 长度不合法"));
    if (m.signatureBase64.isEmpty())
        return fail(QStringLiteral("更新清单缺少 signature，拒绝使用未签名清单"));
    if (m.size <= 0)
        return fail(QStringLiteral("更新清单 size 不合法"));

    const QJsonObject installer = m.raw.value(QStringLiteral("installer")).toObject();
    if (!installer.isEmpty()) {
        m.installerKind = installer.value(QStringLiteral("kind")).toString(QStringLiteral("exe"));
        for (const QJsonValue &arg : installer.value(QStringLiteral("args")).toArray())
            m.installerArgs << arg.toString();
    } else {
#if defined(Q_OS_WIN)
        m.installerKind = QStringLiteral("exe");
        m.installerArgs = {QStringLiteral("/S"), QStringLiteral("--wait-pid"), QStringLiteral("{pid}")};
#endif
    }

    for (const QJsonValue &value : m.raw.value(QStringLiteral("mirrors")).toArray()) {
        const QJsonObject obj = value.toObject();
        UpdateMirror mirror;
        mirror.name = obj.value(QStringLiteral("name")).toString();
        mirror.baseUrl = QUrl(obj.value(QStringLiteral("url")).toString());
        // 镜像地址允许相对写法，按清单所在目录解析成绝对 URL（镜像只是下载通道）。
        if (mirror.baseUrl.isRelative() && !feedUrl.isEmpty())
            mirror.baseUrl = feedUrl.resolved(mirror.baseUrl);
        if (mirror.name.isEmpty() || !mirror.baseUrl.isValid() || mirror.baseUrl.isEmpty())
            continue;
        m.mirrors << mirror;
    }

    m.valid = true;
    if (error) *error = QString();
    return m;
}

QByteArray UpdateManifest::canonicalSignedPayload() const
{
    QJsonObject copy = raw;
    copy.remove(QStringLiteral("signature"));
    return canonicalJson(copy);
}

QUrl UpdateManifest::directUrl() const
{
    const QUrl u(url);
    if (u.isValid() && !u.scheme().isEmpty())
        return u;
    if (!feedUrl.isEmpty()) {
        QUrl base = feedUrl;
        base.setQuery(QString());
        base.setFragment(QString());
        if (!base.path().endsWith(QLatin1Char('/'))) {
            const QString path = base.path();
            base.setPath(path.left(path.lastIndexOf(QLatin1Char('/')) + 1));
        }
        return base.resolved(QUrl(url));
    }
    return u;
}

QUrl UpdateManifest::artifactUrl(const QUrl &base) const
{
    if (base.isEmpty())
        return directUrl();
    QUrl b = base;
    if (!b.path().endsWith(QLatin1Char('/')))
        b.setPath(b.path() + QLatin1Char('/'));
    return b.resolved(QUrl(url));
}

QStringList UpdateManifest::mirrorNames() const
{
    QStringList names;
    for (const UpdateMirror &m : mirrors)
        names << m.name;
    return names;
}

} // namespace palune
