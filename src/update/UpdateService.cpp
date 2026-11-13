#include "update/UpdateService.h"

#include "diag/Log.h"
#include "update/UpdateCrypto.h"
#include "update/Version.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QSysInfo>
#include <QVariantMap>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace palune {

namespace {
constexpr int kCheckTimeoutMs = 45000;          // 整轮检查（含多源切换）总超时
// 单个清单来源超时：实测 ghfast.top 冷启动约 13 s（缓存命中约 1.5 s），
// 低于 15 s 会把可用镜像误判为失败。
constexpr int kManifestAttemptTimeoutMs = 15000;
constexpr int kProgressIntervalMs = 250;

// GitHub Releases 公开加速镜像：清单与安装包都可按“前缀 + 原始 GitHub URL”取。
// 顺序来自 2026-11-08 实测（见 docs/evidence/release-1.0.0-rc/manifest-multisource-probe.json）：
// ghfast.top 返回 200 且清单签名校验通过；ghproxy.net 在本机出口跳到同一 504，
// 其余候选 404/429/证书过期/无法解析，未纳入。
struct ProxyMirror {
    const char *name;
    const char *prefix;
};
const ProxyMirror kGithubProxyMirrors[] = {
    {"ghfast.top 加速", "https://ghfast.top/"},
};

bool isGithubFeed(const QUrl &url)
{
    return url.host().compare(QLatin1String("github.com"), Qt::CaseInsensitive) == 0;
}


#if defined(Q_OS_WIN)
// 用自带引号规则的命令行启动进程，避免 QProcess 对 cmd.exe 参数二次转义。
bool runHidden(const QString &commandLine, const QString &workingDir, qint64 *pid)
{
    std::wstring cmd = commandLine.toStdWString();
    std::wstring dir = workingDir.toStdWString();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi))
        return false;
    if (pid)
        *pid = static_cast<qint64>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#endif
} // namespace

QString UpdateService::stateName(State s)
{
    switch (s) {
    case State::Unchecked: return QStringLiteral("unchecked");
    case State::Checking: return QStringLiteral("checking");
    case State::UpToDate: return QStringLiteral("uptodate");
    case State::Available: return QStringLiteral("available");
    case State::Downloading: return QStringLiteral("downloading");
    case State::Downloaded: return QStringLiteral("downloaded");
    case State::Installing: return QStringLiteral("installing");
    case State::Error: return QStringLiteral("error");
    }
    return QStringLiteral("unchecked");
}

UpdateService::UpdateService(QObject *parent)
    : UpdateService(QUrl(), QString(), parent)
{
}

UpdateService::UpdateService(const QUrl &feedUrl, const QString &downloadDirOverride, QObject *parent)
    : QObject(parent)
    , m_feedUrl(feedUrl)
    , m_downloadDirOverride(downloadDirOverride)
{
    if (m_feedUrl.isEmpty()) {
        const QString env = qEnvironmentVariable("PALUNE_UPDATE_URL");
        // 源码仓即发布仓且已公开，客户端匿名读取，不内嵌任何凭据。
        m_feedUrl = env.isEmpty()
            ? QUrl(QStringLiteral("https://github.com/yunyu697/palune/releases/latest/download/latest.json"))
            : QUrl(env);
    }
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kCheckTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        // 总超时直接判定失败；abort 后 finished 处理里不会再尝试下一个来源。
        fail(QStringLiteral("UPD-001"), QCoreApplication::translate("Palune", "检查更新超时"));
        if (m_reply)
            m_reply->abort();
    });

    m_manifestAttemptTimer = new QTimer(this);
    m_manifestAttemptTimer->setSingleShot(true);
    m_manifestAttemptTimer->setInterval(kManifestAttemptTimeoutMs);
    connect(m_manifestAttemptTimer, &QTimer::timeout, this, [this]() {
        if (!m_reply)
            return;
        m_attemptTimedOut = true;
        m_reply->abort();
    });

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(kProgressIntervalMs);
    connect(m_progressTimer, &QTimer::timeout, this, &UpdateService::handleDownloadProgress);

    loadLastCheck();
}

void UpdateService::setChannel(const QString &channel)
{
    m_channel = channel == QLatin1String("beta") ? QStringLiteral("beta")
                                                  : QStringLiteral("stable");
    const QString base = QStringLiteral("https://github.com/yunyu697/palune/releases/latest/download/");
    const QString current = m_feedUrl.toString();
    if (!current.startsWith(base))
        return; // 测试或自定义 feed 不按通道改写
    const QString file = m_channel == QLatin1String("beta") ? QStringLiteral("latest-beta.json")
                                                            : QStringLiteral("latest.json");
    m_feedUrl = QUrl(base + file);
}

QString UpdateService::state() const
{
    return stateName(m_state);
}

QString UpdateService::currentVersion() const
{
    return QStringLiteral(PALUNE_VERSION);
}

bool UpdateService::busy() const
{
    return m_state == State::Checking || m_state == State::Downloading || m_state == State::Installing;
}

bool UpdateService::isDownloading() const
{
    return m_state == State::Downloading;
}

QString UpdateService::sizeText() const
{
    if (m_manifest.size <= 0)
        return QStringLiteral("—");
    const double mb = m_manifest.size / 1024.0 / 1024.0;
    return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 1));
}

QString UpdateService::speedText() const
{
    if (m_state != State::Downloading || m_speedBytesPerSecond <= 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 MB/s").arg(QString::number(m_speedBytesPerSecond / 1024.0 / 1024.0, 'f', 2));
}

QString UpdateService::etaText() const
{
    if (m_state != State::Downloading || m_speedBytesPerSecond <= 0 || m_manifest.size <= 0)
        return QStringLiteral("—");
    const double remain = qMax<qint64>(0, m_manifest.size - m_received) / m_speedBytesPerSecond;
    if (remain < 1)
        return QCoreApplication::translate("Palune", "即将完成");
    return QCoreApplication::translate("Palune", "剩余 %1 秒").arg(static_cast<int>(remain));
}

QString UpdateService::mirrorName() const
{
    if (m_mirrorIndex <= 0)
        return QCoreApplication::translate("Palune", "直连");
    const int idx = m_mirrorIndex - 1;
    if (idx < m_manifest.mirrors.size())
        return m_manifest.mirrors.at(idx).name;
    return QCoreApplication::translate("Palune", "直连");
}

QString UpdateService::lastCheckedText() const
{
    if (m_lastCheckedIso.isEmpty())
        return QCoreApplication::translate("Palune", "尚未检查");
    const QDateTime dt = QDateTime::fromString(m_lastCheckedIso, Qt::ISODate);
    return dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                        : QCoreApplication::translate("Palune", "尚未检查");
}

QString UpdateService::statusText() const
{
    switch (m_state) {
    case State::Unchecked:
        return QCoreApplication::translate("Palune", "尚未检查更新");
    case State::Checking:
        return QCoreApplication::translate("Palune", "正在检查更新…");
    case State::UpToDate:
        return QCoreApplication::translate("Palune", "已是最新版本（当前 %1）· 上次检查 %2")
            .arg(currentVersion(), lastCheckedText());
    case State::Available:
        return QCoreApplication::translate("Palune", "发现新版本 %1（发布于 %2，体积 %3）")
            .arg(m_manifest.version, m_manifest.publishedAt.left(10), sizeText());
    case State::Downloading:
        return QCoreApplication::translate("Palune", "正在下载 %1 · %2%").arg(mirrorName()).arg(m_progress);
    case State::Downloaded:
        return QCoreApplication::translate("Palune", "已下载到 %1，可安装").arg(QFileInfo(m_downloadedPath).fileName());
    case State::Installing:
        return QCoreApplication::translate("Palune", "正在安装，完成后将自动重启");
    case State::Error:
        return QStringLiteral("%1：%2").arg(m_errorCode, m_errorMessage);
    }
    return QString();
}

QString UpdateService::installActionText() const
{
    return m_state == State::Downloaded ? QCoreApplication::translate("Palune", "安装") : QCoreApplication::translate("Palune", "更新");
}

void UpdateService::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
    emit finished(stateName(s));
}

void UpdateService::fail(const QString &code, const QString &detail)
{
    m_errorCode = code;
    m_errorMessage = readableError(code) + (detail.isEmpty() ? QString() : QStringLiteral("（%1）").arg(detail));
    emit errorChanged();
    setState(State::Error);
    Log::error(QStringLiteral("更新失败 %1: %2").arg(code, m_errorMessage));
}

QList<UpdateService::ManifestSource> UpdateService::buildManifestSources() const
{
    QList<ManifestSource> sources;
    sources << ManifestSource{QCoreApplication::translate("Palune", "直连（官方源）"), m_feedUrl};

    // 测试/自建镜像：完整清单 URL，分号或逗号分隔，设置后完全替代内置镜像列表。
    const QString env = qEnvironmentVariable("PALUNE_UPDATE_MANIFEST_MIRRORS").trimmed();
    if (!env.isEmpty()) {
        QString normalized = env;
        normalized.replace(QLatin1Char(','), QLatin1Char(';'));
        int n = 0;
        for (const QString &raw : normalized.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QUrl url(raw.trimmed());
            if (url.isEmpty() || url.scheme().isEmpty() || !url.isValid()) {
                Log::warn(QStringLiteral("忽略非法清单镜像：%1").arg(raw));
                continue;
            }
            sources << ManifestSource{
                QCoreApplication::translate("Palune", "配置镜像 %1").arg(++n), url};
        }
        return sources;
    }

    // 内置镜像只对 GitHub Releases 主链生效，第三方镜像只拿到“前缀 + 原始 URL”。
    if (isGithubFeed(m_feedUrl)) {
        for (const ProxyMirror &mirror : kGithubProxyMirrors) {
            const QUrl url(QString::fromLatin1(mirror.prefix) + m_feedUrl.toString());
            sources << ManifestSource{QString::fromUtf8(mirror.name), url};
        }
    }
    return sources;
}

void UpdateService::checkForUpdates()
{
    if (busy())
        return;
    Log::info(QStringLiteral("开始检查更新：%1").arg(m_feedUrl.toString()));
    m_errorCode.clear();
    m_errorMessage.clear();
    emit errorChanged();

    m_manifest = UpdateManifest{};
    m_manifestBytes.clear();
    m_hasUpdate = false;
    m_manifestSource.clear();
    m_manifestSourceUrl.clear();
    m_manifestAttempts.clear();
    m_manifestSourceIndex = -1;
    emit manifestChanged();
    emit manifestSourceChanged();
    emit manifestAttemptsChanged();

    m_manifestSources = buildManifestSources();
    QStringList plan;
    for (const ManifestSource &source : m_manifestSources)
        plan << source.name + QStringLiteral("=") + source.url.toString();
    Log::info(QStringLiteral("清单来源顺序：%1").arg(plan.join(QStringLiteral(" -> "))));

    m_timeoutTimer->start();
    m_mirrorIndex = 0; // 每次检查都从官方直连开始，避免沿用上次失败换到的镜像
    setState(State::Checking);
    fetchManifest(0);
}

void UpdateService::fetchManifest(int index)
{
    if (index < 0 || index >= m_manifestSources.size()) {
        fail(QStringLiteral("UPD-001"), QCoreApplication::translate("Palune", "没有可用的清单来源"));
        return;
    }
    const ManifestSource source = m_manifestSources.at(index);
    m_manifestSourceIndex = index;
    m_attemptTimedOut = false;
    m_attemptClock.start();
    Log::info(QStringLiteral("清单来源尝试 %1/%2：%3（%4）")
                  .arg(index + 1)
                  .arg(m_manifestSources.size())
                  .arg(source.name, source.url.toString()));

    QNetworkRequest request(source.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Palune-Updater/1.0");
    request.setRawHeader("Cache-Control", "no-cache");

    m_manifestAttemptTimer->start();
    m_reply = m_nam.get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, source]() {
        m_manifestAttemptTimer->stop();
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        if (!reply)
            return;
        handleManifestReply(reply, source, m_attemptClock.elapsed());
    });
}

void UpdateService::recordManifestAttempt(const ManifestSource &source, const QString &outcome,
                                          const QString &detail, qint64 elapsedMs,
                                          int httpStatus, const QByteArray &bytes)
{
    QVariantMap item;
    item.insert(QStringLiteral("index"), m_manifestSourceIndex);
    item.insert(QStringLiteral("name"), source.name);
    item.insert(QStringLiteral("url"), source.url.toString());
    item.insert(QStringLiteral("outcome"), outcome);
    item.insert(QStringLiteral("detail"), detail);
    item.insert(QStringLiteral("elapsedMs"), double(elapsedMs));
    item.insert(QStringLiteral("httpStatus"), httpStatus);
    item.insert(QStringLiteral("bytes"), double(bytes.size()));
    item.insert(QStringLiteral("sha256"),
                bytes.isEmpty() ? QString()
                                : QString::fromLatin1(QCryptographicHash::hash(
                                      bytes, QCryptographicHash::Sha256).toHex()));
    m_manifestAttempts.append(item);
    emit manifestAttemptsChanged();
    QString byteInfo;
    if (!bytes.isEmpty()) {
        byteInfo = QStringLiteral("，%1 B，sha256=%2")
                       .arg(bytes.size())
                       .arg(item.value(QStringLiteral("sha256")).toString().left(16));
    }
    Log::info(QStringLiteral("清单来源结果「%1」%2：%3%4")
                  .arg(source.name, outcome, detail, byteInfo));
}

void UpdateService::tryNextManifestSource(const ManifestSource &source, const QString &outcome,
                                          const QString &detail, qint64 elapsedMs,
                                          int httpStatus, const QByteArray &bytes)
{
    recordManifestAttempt(source, outcome, detail, elapsedMs, httpStatus, bytes);
    if (m_manifestSourceIndex + 1 < m_manifestSources.size()) {
        const ManifestSource next = m_manifestSources.at(m_manifestSourceIndex + 1);
        Log::warn(QStringLiteral("清单来源「%1」不可用（%2），切换到「%3」")
                      .arg(source.name, detail, next.name));
        fetchManifest(m_manifestSourceIndex + 1);
        return;
    }
    m_manifestAttemptTimer->stop();
    m_timeoutTimer->stop();
    QString code = QStringLiteral("UPD-001");
    if (outcome == QLatin1String("signature-error"))
        code = QStringLiteral("UPD-004");
    else if (outcome == QLatin1String("parse-error"))
        code = QStringLiteral("UPD-003");
    else if (outcome == QLatin1String("http-status"))
        code = QStringLiteral("UPD-002");
    fail(code, QStringLiteral("%1：%2").arg(source.name, detail));
}

void UpdateService::handleManifestReply(QNetworkReply *reply, const ManifestSource &source,
                                        qint64 elapsedMs)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = reply->error();
    Log::info(QStringLiteral("更新清单响应「%1」：error=%2 status=%3")
                  .arg(source.name).arg(int(netError)).arg(status));

    if (netError == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        if (m_state != State::Checking)
            return; // 总超时已把状态置为 Error
        if (m_attemptTimedOut) {
            tryNextManifestSource(source, QStringLiteral("timeout"),
                                  QCoreApplication::translate("Palune", "单源超时 %1 ms")
                                      .arg(kManifestAttemptTimeoutMs),
                                  elapsedMs, status, {});
        } else {
            tryNextManifestSource(source, QStringLiteral("canceled"),
                                  QCoreApplication::translate("Palune", "请求被取消"),
                                  elapsedMs, status, {});
        }
        return;
    }
    if (netError != QNetworkReply::NoError) {
        const QString detail = reply->errorString();
        reply->deleteLater();
        tryNextManifestSource(source, QStringLiteral("network-error"), detail, elapsedMs, status, {});
        return;
    }
    if (status != 200) {
        reply->deleteLater();
        tryNextManifestSource(source, QStringLiteral("http-status"),
                              QStringLiteral("HTTP %1").arg(status), elapsedMs, status, {});
        return;
    }

    const QByteArray bytes = reply->readAll();
    reply->deleteLater();
    m_manifestBytes = bytes;
    Log::info(QStringLiteral("更新清单字节数：%1（%2）").arg(bytes.size()).arg(source.name));

    QString parseError;
    // 相对产物 URL 按“实际提供清单的来源”解析；绝对 URL 不受影响。
    m_manifest = UpdateManifest::parse(bytes, source.url, &parseError);
    Log::info(QStringLiteral("更新清单解析：valid=%1 error=%2").arg(m_manifest.valid).arg(parseError));
    if (!m_manifest.valid) {
        tryNextManifestSource(source, QStringLiteral("parse-error"), parseError,
                              elapsedMs, status, bytes);
        return;
    }

    // 验签对每个来源都独立执行：镜像返回被改动的清单直接丢弃并换下一个来源。
    QString sigError;
    if (!verifyManifestSignature(&sigError)) {
        tryNextManifestSource(source, QStringLiteral("signature-error"), sigError,
                              elapsedMs, status, bytes);
        return;
    }

    auto canonicalArch = [](QString value) {
        value = value.toLower();
        if (value == QLatin1String("amd64") || value == QLatin1String("x64"))
            return QStringLiteral("x86_64");
        if (value == QLatin1String("arm64"))
            return QStringLiteral("aarch64");
        return value;
    };
    const QString arch = canonicalArch(m_manifest.arch);
    const QString currentArch = canonicalArch(QSysInfo::currentCpuArchitecture());
    if (!arch.isEmpty() && arch != currentArch) {
        // 签名已通过说明清单真实，平台不匹配是确定结论，继续换镜像不会改变。
        recordManifestAttempt(source, QStringLiteral("arch-mismatch"),
                              QCoreApplication::translate("Palune", "清单架构 %1 / 当前 %2")
                                  .arg(arch, currentArch),
                              elapsedMs, status, bytes);
        m_manifestAttemptTimer->stop();
        m_timeoutTimer->stop();
        fail(QStringLiteral("UPD-005"),
             QCoreApplication::translate("Palune", "清单架构 %1 / 当前 %2").arg(arch, currentArch));
        return;
    }

    recordManifestAttempt(source, QStringLiteral("ok"), QStringLiteral("签名校验通过"),
                          elapsedMs, status, bytes);
    m_manifestSource = source.name;
    m_manifestSourceUrl = source.url.toString();
    emit manifestSourceChanged();

    m_manifestAttemptTimer->stop();
    m_timeoutTimer->stop();
    const Version remote = Version::parse(m_manifest.version);
    const Version current = Version::parse(currentVersion());
    m_hasUpdate = remote > current;
    recordLastCheck();
    rebuildMirrors();
    emit manifestChanged();
    setState(m_hasUpdate ? State::Available : State::UpToDate);
    Log::info(QStringLiteral("更新检查完成：来源 %1，远端 %2，本地 %3，%4")
                  .arg(source.name, m_manifest.version, currentVersion(),
                       m_hasUpdate ? QStringLiteral("可更新") : QStringLiteral("已是最新")));
}

bool UpdateService::verifyManifestSignature(QString *error)
{
    const QByteArray key = publicKey();
    if (key.isEmpty()) {
        if (error) *error = QCoreApplication::translate("Palune", "内置更新公钥不可用");
        return false;
    }
    return UpdateCrypto::verifyEd25519Base64(m_manifest.canonicalSignedPayload(),
                                             m_manifest.signatureBase64.toUtf8(),
                                             key, error);
}

QByteArray UpdateService::publicKey() const
{
    const QString override = qEnvironmentVariable("PALUNE_UPDATE_PUBKEY_HEX");
    if (!override.isEmpty()) {
        const QByteArray key = QByteArray::fromHex(override.toLatin1());
        if (key.size() == 32)
            return key;
        Log::warn(QStringLiteral("PALUNE_UPDATE_PUBKEY_HEX 不是 32 字节十六进制，忽略"));
    }
    QString error;
    return UpdateCrypto::publicKeyFromResource(QStringLiteral(":/resources/trust/update.pub"), &error);
}

void UpdateService::rebuildMirrors()
{
    m_mirrors.clear();
    QVariantMap direct;
    direct.insert(QStringLiteral("name"), QCoreApplication::translate("Palune", "直连（官方源）"));
    direct.insert(QStringLiteral("url"), m_manifest.directUrl().toString());
    direct.insert(QStringLiteral("direct"), true);
    m_mirrors << direct;
    for (const UpdateMirror &mirror : m_manifest.mirrors) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), mirror.name);
        item.insert(QStringLiteral("url"), m_manifest.artifactUrl(mirror.baseUrl).toString());
        item.insert(QStringLiteral("direct"), false);
        m_mirrors << item;
    }
    // 安装包下载也补上实测可用的 GitHub 加速通道：清单来自镜像时，
    // 产物仍可能是绝对 github.com 地址，直连在该出口会 504。
    if (isGithubFeed(m_feedUrl)) {
        const QString direct = m_manifest.directUrl().toString();
        if (!direct.isEmpty()) {
            for (const ProxyMirror &mirror : kGithubProxyMirrors) {
                QVariantMap item;
                item.insert(QStringLiteral("name"), QString::fromUtf8(mirror.name));
                item.insert(QStringLiteral("url"), QString::fromLatin1(mirror.prefix) + direct);
                item.insert(QStringLiteral("direct"), false);
                m_mirrors << item;
            }
        }
    }
    if (m_mirrorIndex >= m_mirrors.size())
        m_mirrorIndex = 0;
    emit mirrorsChanged();
    emit mirrorChanged();
}

bool UpdateService::tryNextChannel(const QString &reason)
{
    // 通道顺序：直连（官方发行仓库）→ 清单 mirrors。
    // 清单与签名始终走主链，镜像只决定安装包从哪下载；请求失败或内容校验不过就换下一条。
    if (m_mirrors.isEmpty() || m_mirrorIndex + 1 >= m_mirrors.size())
        return false;
    const QString failed = mirrorName();
    ++m_mirrorIndex;
    m_restartingDownload = true;
    emit mirrorChanged();
    Log::warn(QStringLiteral("下载通道「%1」失败（%2），自动切换到「%3」")
                  .arg(failed, reason, mirrorName()));
    QTimer::singleShot(0, this, [this]() {
        m_restartingDownload = false;
        startDownload(m_action);
    });
    return true;
}

QUrl UpdateService::selectedArtifactUrl() const
{
    if (m_mirrorIndex <= 0)
        return m_manifest.directUrl();
    const int idx = m_mirrorIndex - 1;
    if (idx < m_manifest.mirrors.size())
        return m_manifest.artifactUrl(m_manifest.mirrors.at(idx).baseUrl);
    return m_manifest.directUrl();
}

QString UpdateService::packageFileName() const
{
    QString name = QFileInfo(QUrl(m_manifest.url).path()).fileName();
    if (name.isEmpty())
        name = QStringLiteral("Palune-%1-setup.exe").arg(m_manifest.version);
    return name;
}

QString UpdateService::targetDirectory() const
{
    if (!m_downloadDirOverride.isEmpty())
        return m_downloadDirOverride;
    if (m_action == Action::DownloadOnly)
        return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/Palune");
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/Palune-update-") + m_manifest.version;
}

void UpdateService::startUpdate()
{
    m_action = Action::UpdateAndInstall;
    startDownload(m_action);
}

void UpdateService::startDownloadOnly()
{
    m_action = Action::DownloadOnly;
    startDownload(m_action);
}

void UpdateService::startDownload(Action action)
{
    if (!m_manifest.valid) {
        fail(QStringLiteral("UPD-003"), QCoreApplication::translate("Palune", "尚未成功获取更新清单"));
        return;
    }
    const QUrl url = selectedArtifactUrl();
    if (!url.isValid() || url.isEmpty()) {
        fail(QStringLiteral("UPD-010"), QCoreApplication::translate("Palune", "下载地址为空"));
        return;
    }

    QDir().mkpath(targetDirectory());
    const QString finalPath = targetDirectory() + QLatin1Char('/') + packageFileName();
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    m_currentPath = finalPath + QStringLiteral(".part");
    m_file = new QFile(m_currentPath, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("UPD-009"), m_file->errorString());
        delete m_file;
        m_file = nullptr;
        return;
    }
    m_received = 0;
    m_progress = 0;
    m_speedBytesPerSecond = 0;
    emit progressChanged();
    m_downloadClock.start();
    m_speedClock.start();
    setState(State::Downloading);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Palune-Updater/1.0");
    m_reply = m_nam.get(request);
    m_progressTimer->start();

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_reply || !m_file)
            return;
        const QByteArray chunk = m_reply->readAll();
        m_file->write(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
        m_received = rec;
        if (total > 0 && m_manifest.size <= 0)
            m_manifest.size = total;
        if (m_manifest.size > 0) {
            const int p = static_cast<int>(qBound<qint64>(qint64(0), rec * 100 / m_manifest.size, qint64(100)));
            if (p != m_progress) {
                m_progress = p;
                emit progressChanged();
            }
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        m_progressTimer->stop();
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (m_restartingDownload) {
                m_restartingDownload = false;
                startDownload(m_action);
                return;
            }
            if (m_file) {
                m_file->close();
                m_file->remove();
                delete m_file;
                m_file = nullptr;
            }
            const QString reason = reply->errorString();
            if (tryNextChannel(reason))
                return;
            fail(QStringLiteral("UPD-007"), reason);
            return;
        }
        finishDownload();
    });
}

void UpdateService::handleDownloadProgress()
{
    if (m_state != State::Downloading)
        return;
    const qint64 elapsedMs = qMax<qint64>(1, m_speedClock.elapsed());
    if (m_received > 0) {
        m_speedBytesPerSecond = m_received * 1000.0 / elapsedMs;
    }
    emit progressChanged();
}

void UpdateService::finishDownload()
{
    if (!m_file) {
        fail(QStringLiteral("UPD-007"), QCoreApplication::translate("Palune", "下载文件句柄已丢失"));
        return;
    }
    m_file->flush();
    m_file->close();
    delete m_file;
    m_file = nullptr;

    QString verifyError;
    if (!verifyPackage(m_currentPath, &verifyError)) {
        // 镜像/反代返回的内容没通过 SHA-256 或签名校验：整包丢弃，换下一个通道重下。
        QFile::remove(m_currentPath);
        if (tryNextChannel(verifyError))
            return;
        fail(QStringLiteral("UPD-006"), verifyError);
        return;
    }

    const QString finalPath = m_currentPath.left(m_currentPath.size() - 5); // 去掉 ".part"
    QFile::remove(finalPath);
    if (!QFile::rename(m_currentPath, finalPath)) {
        fail(QStringLiteral("UPD-009"), QCoreApplication::translate("Palune", "无法重命名下载文件"));
        return;
    }
    m_downloadedPath = finalPath;
    m_progress = 100;
    emit progressChanged();
    emit downloadedPathChanged();
    Log::info(QStringLiteral("更新包校验通过：%1").arg(finalPath));

    if (m_action == Action::DownloadOnly) {
        setState(State::Downloaded);
        return;
    }
    launchInstaller(finalPath);
}

bool UpdateService::verifyPackage(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual.compare(m_manifest.sha256, Qt::CaseInsensitive) != 0) {
        if (error) *error = QCoreApplication::translate("Palune", "SHA-256 不匹配：期望 %1，实际 %2")
                                 .arg(m_manifest.sha256, actual);
        return false;
    }
    // 完整性不依赖下载通道：再次校验清单签名，未通过则不安装。
    QString sigError;
    if (!verifyManifestSignature(&sigError)) {
        if (error) *error = QCoreApplication::translate("Palune", "清单签名复核失败：%1").arg(sigError);
        return false;
    }
    // 信封格式还要求产物本身带 Ed25519 签名（防镜像/仓库被替换安装包）。
    if (!m_manifest.artifactSignatureBase64.isEmpty()) {
        if (!UpdateCrypto::verifyEd25519Base64(data, m_manifest.artifactSignatureBase64.toUtf8(),
                                               publicKey(), &sigError)) {
            if (error) *error = QCoreApplication::translate("Palune", "安装包签名校验失败：%1").arg(sigError);
            return false;
        }
    }
    return true;
}

void UpdateService::launchInstaller(const QString &path)
{
    const QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString marker = QDir::toNativeSeparators(qEnvironmentVariable("PALUNE_E2E_MARKER"));
    // 通过环境变量把应用路径/标记传给子进程，避免命令行引号解析破坏含空格路径。
    qputenv("PALUNE_INSTALL_APP", appPath.toUtf8());
    qputenv("PALUNE_INSTALL_MARKER", marker.toUtf8());

    QStringList installerArgs;
    for (QString arg : m_manifest.installerArgs) {
        arg.replace(QStringLiteral("{pid}"), QString::number(QCoreApplication::applicationPid()));
        arg.replace(QStringLiteral("{app}"), appPath);
        arg.replace(QStringLiteral("{version}"), m_manifest.version);
        arg.replace(QStringLiteral("{marker}"), marker);
        installerArgs << arg;
    }

    const QString nativePath = QDir::toNativeSeparators(path);
    const QString workingDir = QFileInfo(nativePath).absolutePath();
    const bool isScript = m_manifest.installerKind.compare(QLatin1String("cmd"), Qt::CaseInsensitive) == 0
                          || m_manifest.installerKind.compare(QLatin1String("bat"), Qt::CaseInsensitive) == 0;

    setState(State::Installing);
    qint64 pid = 0;
    if (isScript) {
        // cmd /d /s /c ""script" args…"：/s 精确剥离最外层引号，内部引号原样保留。
        QString command = QLatin1Char('"') + nativePath + QLatin1Char('"');
        for (const QString &arg : installerArgs)
            command += QLatin1String(" \"") + arg + QLatin1Char('"');
        QString cmdExe = qEnvironmentVariable("ComSpec");
        if (cmdExe.isEmpty())
            cmdExe = QStringLiteral("C:\\Windows\\System32\\cmd.exe");
        const QString commandLine = QLatin1Char('"') + cmdExe + QLatin1String("\" /d /s /c \"")
                                    + command + QLatin1Char('"');
        Log::info(QStringLiteral("启动安装程序（隐藏窗口）：%1").arg(commandLine));
#ifdef Q_OS_WIN
        if (!runHidden(commandLine, workingDir, &pid)) {
            fail(QStringLiteral("UPD-008"), commandLine);
            return;
        }
#else
        if (!QProcess::startDetached(QStringLiteral("sh"), {nativePath}, workingDir, &pid)) {
            fail(QStringLiteral("UPD-008"), commandLine);
            return;
        }
#endif
    } else {
        Log::info(QStringLiteral("启动安装程序：%1").arg(nativePath));
        if (!QProcess::startDetached(nativePath, installerArgs, workingDir, &pid)) {
            fail(QStringLiteral("UPD-008"), nativePath);
            return;
        }
    }
    m_installerPid = pid;
    Log::info(QStringLiteral("已启动安装程序 pid=%1，应用即将退出并自动重启").arg(pid));
    QTimer::singleShot(300, qApp, &QCoreApplication::quit);
}

void UpdateService::installDownloaded()
{
    if (m_downloadedPath.isEmpty() || !QFileInfo::exists(m_downloadedPath)) {
        fail(QStringLiteral("UPD-006"), QCoreApplication::translate("Palune", "下载文件不存在"));
        return;
    }
    QString error;
    if (!verifyPackage(m_downloadedPath, &error)) {
        fail(QStringLiteral("UPD-006"), error);
        return;
    }
    m_action = Action::DownloadOnly;
    launchInstaller(m_downloadedPath);
}

void UpdateService::selectMirror(int index)
{
    if (index < 0 || index >= m_mirrors.size())
        return;
    if (index == m_mirrorIndex)
        return;
    m_mirrorIndex = index;
    emit mirrorChanged();
    if (m_state == State::Downloading) {
        m_restartingDownload = true;
        if (m_reply)
            m_reply->abort();
        Log::info(QStringLiteral("切换下载通道为「%1」，重新开始下载").arg(mirrorName()));
    }
}

void UpdateService::retry()
{
    if (m_state != State::Error)
        return;
    m_errorCode.clear();
    m_errorMessage.clear();
    emit errorChanged();
    if (m_manifest.valid && m_action != Action::None)
        startDownload(m_action);
    else
        checkForUpdates();
}

void UpdateService::openHomePage()
{
    // 主页即本仓公开地址；此前指向的 palune.app 并不存在，点开是空页面。
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/yunyu697/palune")));
}

void UpdateService::recordLastCheck()
{
    m_lastCheckedIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSettings settings(QStringLiteral("Palune"), QStringLiteral("Palune"));
    settings.setValue(QStringLiteral("update/lastCheckIso"), m_lastCheckedIso);
    emit lastCheckedTextChanged();
}

void UpdateService::loadLastCheck()
{
    QSettings settings(QStringLiteral("Palune"), QStringLiteral("Palune"));
    m_lastCheckedIso = settings.value(QStringLiteral("update/lastCheckIso")).toString();
}

void UpdateService::setDemoState(const QString &stateNameValue)
{
    m_manifest.valid = true;
    m_manifest.version = QStringLiteral("1.0.0-alpha2");
    m_manifest.publishedAt = QStringLiteral("2026-11-06T09:30:00Z");
    m_manifest.size = 51380224;
    m_manifest.notes = QCoreApplication::translate("Palune", "M0 技术验证版：完善全屏与 DPI 切换，修复窗口偏移。");
    m_manifest.url = QStringLiteral("Palune-1.0.0-alpha2-win-x64.exe");
    UpdateMirror m1{QCoreApplication::translate("Palune", "镜像 A · 华东"), QUrl(QStringLiteral("https://mirror-a.example.com/palune/"))};
    UpdateMirror m2{QCoreApplication::translate("Palune", "镜像 B · 华南"), QUrl(QStringLiteral("https://mirror-b.example.com/palune/"))};
    m_manifest.mirrors = {m1, m2};
    m_mirrors.clear();
    QVariantMap direct;
    direct.insert(QStringLiteral("name"), QCoreApplication::translate("Palune", "直连（官方源）"));
    direct.insert(QStringLiteral("url"), QStringLiteral("https://updates.palune.app/beta/Palune-1.0.0-alpha2-win-x64.exe"));
    direct.insert(QStringLiteral("direct"), true);
    m_mirrors << direct;
    for (const auto &mirror : m_manifest.mirrors) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), mirror.name);
        item.insert(QStringLiteral("url"), m_manifest.artifactUrl(mirror.baseUrl).toString());
        item.insert(QStringLiteral("direct"), false);
        m_mirrors << item;
    }
    m_lastCheckedIso = QStringLiteral("2026-11-06T09:45:00Z");
    m_hasUpdate = true;
    m_progress = 42;
    m_speedBytesPerSecond = 3.8 * 1024 * 1024;
    m_received = m_manifest.size * 42 / 100;
    m_downloadedPath = QStringLiteral("C:/Users/User/Downloads/Palune/Palune-1.0.0-alpha2-win-x64.exe");
    if (stateNameValue == QLatin1String("error")) {
        m_errorCode = QStringLiteral("UPD-006");
        m_errorMessage = readableError(m_errorCode);
        setState(State::Error);
    } else if (stateNameValue == QLatin1String("downloaded")) {
        setState(State::Downloaded);
    } else if (stateNameValue == QLatin1String("downloading")) {
        setState(State::Downloading);
    } else if (stateNameValue == QLatin1String("available")) {
        setState(State::Available);
    } else if (stateNameValue == QLatin1String("uptodate")) {
        m_hasUpdate = false;
        setState(State::UpToDate);
    } else {
        setState(State::Unchecked);
    }
    emit manifestChanged();
    emit mirrorsChanged();
    emit progressChanged();
    emit downloadedPathChanged();
    emit lastCheckedTextChanged();
}

QString UpdateService::readableError(const QString &code)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("UPD-001"), QCoreApplication::translate("Palune", "网络不可达或请求超时，请检查网络后重试")},
        {QStringLiteral("UPD-002"), QCoreApplication::translate("Palune", "更新服务器返回异常状态")},
        {QStringLiteral("UPD-003"), QCoreApplication::translate("Palune", "更新清单无法解析")},
        {QStringLiteral("UPD-004"), QCoreApplication::translate("Palune", "更新清单签名校验失败，已拒绝使用")},
        {QStringLiteral("UPD-005"), QCoreApplication::translate("Palune", "该更新包与当前平台或架构不匹配")},
        {QStringLiteral("UPD-006"), QCoreApplication::translate("Palune", "安装包完整性校验失败，已拒绝安装")},
        {QStringLiteral("UPD-007"), QCoreApplication::translate("Palune", "下载中断，可重试或切换镜像")},
        {QStringLiteral("UPD-008"), QCoreApplication::translate("Palune", "无法启动安装程序")},
        {QStringLiteral("UPD-009"), QCoreApplication::translate("Palune", "没有写入权限或磁盘空间不足")},
        {QStringLiteral("UPD-010"), QCoreApplication::translate("Palune", "镜像地址无效")},
    };
    return map.value(code, QCoreApplication::translate("Palune", "未知更新错误"));
}

} // namespace palune
