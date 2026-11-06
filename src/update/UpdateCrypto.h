#pragma once

#include <QByteArray>
#include <QString>

namespace palune {

// 更新签名校验（Ed25519）。
// 完整性不依赖下载通道：manifest 的 Ed25519 签名 + 安装包 SHA-256 双重校验。
// libsodium 以动态库方式加载（不静态链接）。
class UpdateCrypto
{
public:
    static bool verifyEd25519(const QByteArray &message,
                              const QByteArray &signature,
                              const QByteArray &publicKey,
                              QString *error);

    static bool verifyEd25519Base64(const QByteArray &message,
                                    const QByteArray &signatureBase64,
                                    const QByteArray &publicKey,
                                    QString *error);

    // 从资源读取十六进制公钥（32 字节）。
    static QByteArray publicKeyFromResource(const QString &resourcePath, QString *error);

    static bool sodiumAvailable();
};

} // namespace palune
