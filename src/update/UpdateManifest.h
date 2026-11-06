#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

namespace palune {

struct UpdateMirror {
    QString name;
    QUrl baseUrl;
};

// 清单中的一个平台产物（M1 起按 platform/arch 选择，不再假定单产物）。
struct UpdateArtifact {
    QString platform; // windows | macos | linux
    QString arch;     // x86_64 | aarch64
    QString url;
    QString sha256;
    QString signatureBase64;
    QString installer; // exe | msi | dmg | appimage | deb | rpm
    QString minOsVersion;
    qint64 size = 0;
};

struct UpdateManifest {
    bool valid = false;
    QString error;

    QString version;
    QString publishedAt;
    QString arch;
    QString minOs;
    QString url;
    QString sha256;
    QString notes;
    qint64 size = 0;
    QString installerKind = QStringLiteral("exe");
    QStringList installerArgs;
    QList<UpdateMirror> mirrors;

    // 信封格式（schemaVersion=1）字段
    int schemaVersion = 0;
    QString channel;
    QString keyId;
    QString signatureType;
    QString artifactSignatureBase64;
    QList<UpdateArtifact> artifacts;

    QString signatureBase64;
    QJsonObject raw;      // payload（签名覆盖的内容）
    QJsonObject envelope; // 完整信封，便于诊断
    QUrl feedUrl;

    static UpdateManifest parse(const QByteArray &json,
                                const QUrl &feedUrl,
                                QString *error = nullptr);

    // 签名覆盖的规范化字节：去掉 signature 字段，键排序、无空白。
    QByteArray canonicalSignedPayload() const;

    QUrl directUrl() const;
    QUrl artifactUrl(const QUrl &base) const;
    QStringList mirrorNames() const;

    static QByteArray canonicalJson(const QJsonValue &value);
};

} // namespace palune
