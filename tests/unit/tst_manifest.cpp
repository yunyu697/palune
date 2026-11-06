#include <QtTest>

#include "update/UpdateCrypto.h"
#include "update/UpdateManifest.h"

using namespace palune;

namespace {
// moc 无法解析 C++ 原始字符串字面量，这里用普通转义字符串，避免 AUTOMOC 生成空 moc。
const char *kManifestJson =
    "{\n"
    "  \"arch\": \"x86_64\",\n"
    "  \"installer\": {\"args\": [\"/S\", \"--wait-pid\", \"{pid}\"], \"kind\": \"exe\"},\n"
    "  \"mirrors\": [{\"name\": \"镜像 A\", \"url\": \"https://mirror-a.example.com/palune/\"}],\n"
    "  \"notes\": \"测试版本\",\n"
    "  \"publishedAt\": \"2026-11-06T09:30:00Z\",\n"
    "  \"schema\": 1,\n"
    "  \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
    "  \"signature\": \"AAAA\",\n"
    "  \"size\": 1024,\n"
    "  \"url\": \"Palune-1.0.0-alpha2-win-x64.exe\",\n"
    "  \"version\": \"1.0.0-alpha2\"\n"
    "}";

// 由 tools/update/test-key.pem 对 "palune-test" 生成的签名（仅测试）
const char *kPublicKeyHex =
    "1ef63fe4ede6e6a5e7a4e7e3551f42d8ddbadc383092ce8a4fc2ecb0221b9e88";
const char *kSignatureBase64 =
    "jd1b2ayxH/7bA49OsUnhiEEoSUVKrrtvMjN9ljme40zY4i3uH5Y4AVQRmOwkbELAVfdWZ3Y5PKfkLirYbp5QDw==";
} // namespace

class TestManifest : public QObject
{
    Q_OBJECT

private slots:
    void parsesManifest()
    {
        QString error;
        const UpdateManifest m = UpdateManifest::parse(
            QByteArray(kManifestJson), QUrl(QStringLiteral("https://updates.palune.app/beta/latest.json")), &error);
        QVERIFY2(m.valid, qPrintable(error));
        QCOMPARE(m.version, QStringLiteral("1.0.0-alpha2"));
        QCOMPARE(m.size, qint64(1024));
        QCOMPARE(m.mirrors.size(), 1);
        QCOMPARE(m.installerKind, QStringLiteral("exe"));
        QCOMPARE(m.mirrorNames().first(), QStringLiteral("镜像 A"));
        QVERIFY(m.directUrl().toString().endsWith(QStringLiteral("Palune-1.0.0-alpha2-win-x64.exe")));
        QCOMPARE(m.artifactUrl(QUrl(QStringLiteral("https://mirror-a.example.com/palune/"))).toString(),
                 QStringLiteral("https://mirror-a.example.com/palune/Palune-1.0.0-alpha2-win-x64.exe"));
    }

    void canonicalPayloadExcludesSignature()
    {
        QString error;
        const UpdateManifest m = UpdateManifest::parse(QByteArray(kManifestJson), QUrl(), &error);
        QVERIFY2(m.valid, qPrintable(error));
        const QByteArray canonical = m.canonicalSignedPayload();
        QVERIFY(!canonical.contains("signature"));
        QVERIFY(canonical.startsWith("{\"arch\":\"x86_64\""));
        QVERIFY(canonical.contains("\"version\":\"1.0.0-alpha2\""));
        QVERIFY(!canonical.contains("\n"));
        QVERIFY(!canonical.contains(": "));
    }

    void rejectsUnsignedManifest()
    {
        QString error;
        const UpdateManifest m = UpdateManifest::parse(
            "{\"version\":\"1.0.0-alpha2\",\"url\":\"a.exe\",\"size\":1,"
            "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
            QUrl(), &error);
        QVERIFY(!m.valid);
        QVERIFY(error.contains(QStringLiteral("signature")));
    }

    void rejectsVersionRuleViolation()
    {
        QString error;
        const UpdateManifest m = UpdateManifest::parse(
            "{\"version\":\"1.0.0-alpha11\",\"url\":\"a.exe\",\"size\":1,\"signature\":\"AA==\","
            "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
            QUrl(), &error);
        QVERIFY(!m.valid);
    }

    void verifiesEd25519Vector()
    {
        const QByteArray message = QByteArrayLiteral("palune-test");
        const QByteArray key = QByteArray::fromHex(kPublicKeyHex);
        QString error;
        QVERIFY2(UpdateCrypto::verifyEd25519Base64(message, QByteArray(kSignatureBase64), key, &error),
                 qPrintable(error));
        QVERIFY(!UpdateCrypto::verifyEd25519Base64(message + "x", QByteArray(kSignatureBase64), key, &error));
    }

    void rejectsTamperedSignature()
    {
        QByteArray sig = QByteArray::fromBase64(kSignatureBase64);
        sig[0] = char(sig[0] ^ 0x01);
        QString error;
        const QByteArray key = QByteArray::fromHex(kPublicKeyHex);
        QVERIFY(!UpdateCrypto::verifyEd25519(QByteArrayLiteral("palune-test"), sig, key, &error));
    }
};

QTEST_GUILESS_MAIN(TestManifest)
#include "tst_manifest.moc"
