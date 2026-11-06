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

    QString signatureBase64;
    QJsonObject raw;
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
