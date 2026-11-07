#include "update/UpdateCrypto.h"

#include "diag/Log.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QResource>

namespace palune {

namespace {

struct SodiumApi {
    QLibrary *library = nullptr;
    int (*sodiumInit)() = nullptr;
    int (*verifyDetached)(const unsigned char *, const unsigned char *,
                          unsigned long long, const unsigned char *) = nullptr;
    bool ready = false;
    QString error;
};

SodiumApi &api()
{
    static SodiumApi s;
    if (s.ready || !s.error.isEmpty())
        return s;

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList names;
#if defined(Q_OS_WIN)
    names << appDir + QStringLiteral("/libsodium.dll") << QStringLiteral("libsodium");
#elif defined(Q_OS_MACOS)
    names << appDir + QStringLiteral("/libsodium.dylib") << QStringLiteral("libsodium");
#else
    names << appDir + QStringLiteral("/libsodium.so") << QStringLiteral("libsodium.so.26")
          << QStringLiteral("libsodium.so.23") << QStringLiteral("libsodium");
#endif

    for (const QString &name : names) {
        auto *lib = new QLibrary(name);
        if (!lib->load()) {
            delete lib;
            continue;
        }
        auto init = reinterpret_cast<int (*)()>(lib->resolve("sodium_init"));
        auto verify = reinterpret_cast<int (*)(const unsigned char *, const unsigned char *,
                                               unsigned long long, const unsigned char *)>(
            lib->resolve("crypto_sign_verify_detached"));
        if (!init || !verify) {
            delete lib;
            continue;
        }
        if (init() < 0) {
            delete lib;
            continue;
        }
        s.library = lib;
        s.sodiumInit = init;
        s.verifyDetached = verify;
        s.ready = true;
        Log::info(QStringLiteral("libsodium 已动态加载：%1").arg(lib->fileName()));
        return s;
    }
    s.error = QCoreApplication::translate("Palune", "未找到 libsodium 动态库，无法校验更新签名");
    Log::error(s.error);
    return s;
}

} // namespace

bool UpdateCrypto::sodiumAvailable()
{
    return api().ready;
}

bool UpdateCrypto::verifyEd25519(const QByteArray &message,
                                 const QByteArray &signature,
                                 const QByteArray &publicKey,
                                 QString *error)
{
    auto &s = api();
    if (!s.ready) {
        if (error) *error = s.error;
        return false;
    }
    if (signature.size() != 64 || publicKey.size() != 32) {
        if (error) *error = QCoreApplication::translate("Palune", "签名或公钥长度不合法");
        return false;
    }
    const int rc = s.verifyDetached(
        reinterpret_cast<const unsigned char *>(signature.constData()),
        reinterpret_cast<const unsigned char *>(message.constData()),
        static_cast<unsigned long long>(message.size()),
        reinterpret_cast<const unsigned char *>(publicKey.constData()));
    if (rc != 0) {
        if (error) *error = QCoreApplication::translate("Palune", "Ed25519 签名校验未通过");
        return false;
    }
    return true;
}

bool UpdateCrypto::verifyEd25519Base64(const QByteArray &message,
                                       const QByteArray &signatureBase64,
                                       const QByteArray &publicKey,
                                       QString *error)
{
    const QByteArray sig = QByteArray::fromBase64(signatureBase64.trimmed());
    return verifyEd25519(message, sig, publicKey, error);
}

QByteArray UpdateCrypto::publicKeyFromResource(const QString &resourcePath, QString *error)
{
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QCoreApplication::translate("Palune", "无法读取内置公钥资源：%1").arg(resourcePath);
        return {};
    }
    const QString hex = QString::fromUtf8(f.readAll()).trimmed();
    const QByteArray key = QByteArray::fromHex(hex.toLatin1());
    if (key.size() != 32) {
        if (error) *error = QCoreApplication::translate("Palune", "内置公钥不是 32 字节十六进制");
        return {};
    }
    return key;
}

} // namespace palune
