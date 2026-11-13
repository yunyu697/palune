#include "core/CredentialStore.h"

#include <QProcess>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

namespace palune {
namespace credential {
namespace {

QString targetName(const QString &service, const QString &account)
{
    return service + QLatin1Char('/') + account;
}

#if defined(Q_OS_WIN)
QString winError(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString message = length > 0
                          ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
                          : QStringLiteral("错误码 %1").arg(code);
    if (buffer)
        LocalFree(buffer);
    return message;
}
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
// 通过系统命令行工具访问钥匙串 / Secret Service；失败时把 stderr 一并带回。
QString runTool(const QString &program, const QStringList &args, const QByteArray &input,
                int *exitCode)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(3000)) {
        if (exitCode)
            *exitCode = -1;
        return {};
    }
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    process.waitForFinished(5000);
    if (exitCode)
        *exitCode = process.exitCode();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
#endif

} // namespace

bool supported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

QString read(const QString &service, const QString &account, QString *error)
{
    if (service.isEmpty() || account.isEmpty()) {
        if (error)
            *error = QStringLiteral("凭据服务名或账户为空");
        return {};
    }
#if defined(Q_OS_WIN)
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(targetName(service, account).utf16()),
                   CRED_TYPE_GENERIC, 0, &cred)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND)
            return {};
        if (error)
            *error = winError(code);
        return {};
    }
    const int chars = static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t));
    const QString secret = QString::fromWCharArray(
        reinterpret_cast<const wchar_t *>(cred->CredentialBlob), chars);
    CredFree(cred);
    return secret;
#elif defined(Q_OS_MACOS)
    int code = 0;
    const QString out = runTool(QStringLiteral("security"),
                                {QStringLiteral("find-generic-password"),
                                 QStringLiteral("-s"), service,
                                 QStringLiteral("-a"), account,
                                 QStringLiteral("-w")},
                                {}, &code);
    if (code != 0) {
        if (error && code >= 0)
            *error = QStringLiteral("Keychain 读取失败（退出码 %1）").arg(code);
        return {};
    }
    return out;
#elif defined(Q_OS_LINUX)
    int code = 0;
    const QString out = runTool(QStringLiteral("secret-tool"),
                                {QStringLiteral("lookup"), QStringLiteral("service"), service,
                                 QStringLiteral("account"), account},
                                {}, &code);
    if (code != 0) {
        if (error && code >= 0)
            *error = QStringLiteral("Secret Service 读取失败（退出码 %1）").arg(code);
        return {};
    }
    return out;
#else
    if (error)
        *error = QStringLiteral("当前平台没有系统凭据存储实现");
    return {};
#endif
}

bool write(const QString &service, const QString &account, const QString &secret, QString *error)
{
    if (service.isEmpty() || account.isEmpty()) {
        if (error)
            *error = QStringLiteral("凭据服务名或账户为空");
        return false;
    }
#if defined(Q_OS_WIN)
    const QString target = targetName(service, account);
    QByteArray blob(reinterpret_cast<const char *>(secret.utf16()),
                    secret.size() * static_cast<int>(sizeof(char16_t)));
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(account.utf16()));
    if (!CredWriteW(&cred, 0)) {
        if (error)
            *error = winError(GetLastError());
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    int code = 0;
    runTool(QStringLiteral("security"),
            {QStringLiteral("add-generic-password"), QStringLiteral("-U"),
             QStringLiteral("-s"), service, QStringLiteral("-a"), account,
             QStringLiteral("-w"), secret},
            {}, &code);
    if (code != 0) {
        if (error)
            *error = QStringLiteral("Keychain 写入失败（退出码 %1）").arg(code);
        return false;
    }
    return true;
#elif defined(Q_OS_LINUX)
    int code = 0;
    runTool(QStringLiteral("secret-tool"),
            {QStringLiteral("store"), QStringLiteral("--label"), QStringLiteral("Palune"),
             QStringLiteral("service"), service, QStringLiteral("account"), account},
            secret.toUtf8(), &code);
    if (code != 0) {
        if (error)
            *error = QStringLiteral("Secret Service 写入失败（退出码 %1）").arg(code);
        return false;
    }
    return true;
#else
    if (error)
        *error = QStringLiteral("当前平台没有系统凭据存储实现");
    return false;
#endif
}

bool remove(const QString &service, const QString &account, QString *error)
{
    if (service.isEmpty() || account.isEmpty()) {
        if (error)
            *error = QStringLiteral("凭据服务名或账户为空");
        return false;
    }
#if defined(Q_OS_WIN)
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(targetName(service, account).utf16()),
                     CRED_TYPE_GENERIC, 0)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = winError(code);
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    int code = 0;
    runTool(QStringLiteral("security"),
            {QStringLiteral("delete-generic-password"), QStringLiteral("-s"), service,
             QStringLiteral("-a"), account},
            {}, &code);
    return code == 0;
#elif defined(Q_OS_LINUX)
    int code = 0;
    runTool(QStringLiteral("secret-tool"),
            {QStringLiteral("clear"), QStringLiteral("service"), service,
             QStringLiteral("account"), account},
            {}, &code);
    return code == 0;
#else
    if (error)
        *error = QStringLiteral("当前平台没有系统凭据存储实现");
    return false;
#endif
}

} // namespace credential
} // namespace palune
