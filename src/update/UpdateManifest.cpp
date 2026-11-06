#include "update/UpdateManifest.h"

#include "update/Version.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSysInfo>

namespace palune {

namespace {

QString currentPlatformName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString currentArchCanonical()
{
    const QString raw = QSysInfo::currentCpuArchitecture().toLower();
    return raw;
}

QString normalizeArch(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("amd64") || v == QLatin1String("x64") || v == QLatin1String("x86_64"))
        return QStringLiteral("x86_64");
    if (v == QLatin1String("arm64") || v == QLatin1String("aarch64"))
        return QStringLiteral("aarch64");
    if (v == QLatin1String("x86") || v == QLatin1String("i386") || v == QLatin1String("i686"))
        return QStringLiteral("x86");
    return v;
}

QString signatureValue(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return value.toObject().value(QStringLiteral("value")).toString();
    return {};
}

QString notesText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        QString text = obj.value(QStringLiteral("zh-CN")).toString();
        if (text.isEmpty())
            text = obj.value(QStringLiteral("en")).toString();
        return text;
    }
    return {};
}

} // namespace

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

    const QJsonObject root = doc.object();
    m.envelope = root;
    const bool envelope = root.contains(QStringLiteral("payload"));
    if (envelope) {
        if (!root.value(QStringLiteral("payload")).isObject())
            return fail(QStringLiteral("更新清单缺少 payload 对象"));
        m.raw = root.value(QStringLiteral("payload")).toObject();
        const QJsonObject sig = root.value(QStringLiteral("signature")).toObject();
        m.signatureBase64 = signatureValue(root.value(QStringLiteral("signature")));
        m.keyId = sig.value(QStringLiteral("keyId")).toString();
        m.signatureType = sig.value(QStringLiteral("type")).toString();
        m.schemaVersion = m.raw.value(QStringLiteral("schemaVersion")).toInt();
        if (m.schemaVersion != 1)
            return fail(QStringLiteral("不支持的 schemaVersion：%1").arg(m.schemaVersion));
        if (m.signatureBase64.isEmpty())
            return fail(QStringLiteral("更新清单缺少 signature，拒绝使用未签名清单"));
    } else {
        // 兼容 M0 的扁平清单（仅本地联调使用，发布产出已统一为信封格式）。
        m.raw = root;
        m.signatureBase64 = root.value(QStringLiteral("signature")).toString();
        if (m.signatureBase64.isEmpty())
            return fail(QStringLiteral("更新清单缺少 signature，拒绝使用未签名清单"));
    }

    m.version = m.raw.value(QStringLiteral("version")).toString();
    m.publishedAt = m.raw.value(QStringLiteral("publishedAt")).toString();
    m.channel = m.raw.value(QStringLiteral("channel")).toString();
    m.minOs = m.raw.value(QStringLiteral("minOs")).toString();
    m.notes = notesText(m.raw.value(QStringLiteral("notes")));

    if (m.version.isEmpty())
        return fail(QStringLiteral("更新清单缺少 version"));
    QString versionError;
    const Version v = Version::parse(m.version, &versionError);
    if (!v.valid || !validateVersionRule(v, &versionError))
        return fail(QStringLiteral("更新清单版本号不合规：%1").arg(versionError));

    if (!m.channel.isEmpty()) {
        const bool alpha = m.version.contains(QStringLiteral("-alpha"));
        if ((m.channel == QLatin1String("alpha") || m.channel == QLatin1String("beta")) && !alpha)
            return fail(QStringLiteral("通道 %1 与预发布版本 %2 不一致").arg(m.channel, m.version));
        if (m.channel == QLatin1String("stable") && alpha)
            return fail(QStringLiteral("通道 stable 不接受预发布版本 %1").arg(m.version));
        if (m.channel != QLatin1String("alpha") && m.channel != QLatin1String("beta")
            && m.channel != QLatin1String("stable"))
            return fail(QStringLiteral("未知发布通道：%1").arg(m.channel));
    }

    // 产物选择：能匹配当前平台/架构的产物优先；扁平清单沿用顶层 url/sha256/size。
    const QString platform = currentPlatformName();
    const QString arch = normalizeArch(currentArchCanonical());
    m.artifacts.clear();
    QJsonArray artifactList = m.raw.value(QStringLiteral("artifacts")).toArray();
    if (artifactList.isEmpty())
        artifactList = m.raw.value(QStringLiteral("dists")).toArray();
    for (const QJsonValue &value : artifactList) {
        const QJsonObject obj = value.toObject();
        UpdateArtifact artifact;
        artifact.platform = obj.value(QStringLiteral("platform")).toString().toLower();
        artifact.arch = normalizeArch(obj.value(QStringLiteral("arch")).toString());
        artifact.url = obj.value(QStringLiteral("url")).toString();
        artifact.sha256 = obj.value(QStringLiteral("sha256")).toString().toLower();
        artifact.signatureBase64 = signatureValue(obj.value(QStringLiteral("signature")));
        artifact.installer = obj.value(QStringLiteral("installer")).toString();
        artifact.minOsVersion = obj.value(QStringLiteral("minOsVersion")).toString();
        artifact.size = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
        if (artifact.url.isEmpty() || artifact.sha256.size() != 64 || artifact.size <= 0)
            return fail(QStringLiteral("产物条目不完整：%1/%2").arg(artifact.platform, artifact.arch));
        m.artifacts << artifact;
        if (artifact.platform == platform && artifact.arch == arch && m.url.isEmpty()) {
            m.url = artifact.url;
            m.sha256 = artifact.sha256;
            m.size = artifact.size;
            m.arch = artifact.arch;
            m.minOs = artifact.minOsVersion;
            m.installerKind = artifact.installer.isEmpty() ? m.installerKind : artifact.installer;
            m.artifactSignatureBase64 = artifact.signatureBase64;
        }
    }
    if (!m.artifacts.isEmpty() && m.url.isEmpty())
        return fail(QStringLiteral("UPD-01 清单中没有适用于 %1/%2 的产物").arg(platform, arch));
    if (envelope && m.artifacts.isEmpty())
        return fail(QStringLiteral("更新清单缺少 artifacts 产物列表"));
    if (envelope && m.artifactSignatureBase64.isEmpty())
        return fail(QStringLiteral("产物缺少签名，拒绝使用未签名安装包"));

    if (m.url.isEmpty())
        m.url = m.raw.value(QStringLiteral("url")).toString();
    if (m.sha256.isEmpty())
        m.sha256 = m.raw.value(QStringLiteral("sha256")).toString().toLower();
    if (m.size <= 0)
        m.size = static_cast<qint64>(m.raw.value(QStringLiteral("size")).toDouble());
    if (m.arch.isEmpty())
        m.arch = m.raw.value(QStringLiteral("arch")).toString(m.arch);

    if (m.url.isEmpty())
        return fail(QStringLiteral("更新清单缺少 url"));
    if (m.sha256.size() != 64)
        return fail(QStringLiteral("更新清单 sha256 长度不合法"));
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
