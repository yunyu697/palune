#include "settings/Preferences.h"

#include "diag/Log.h"
#include "mpv/MpvClient.h"
#include "settings/Settings.h"
#include "ui/ActionRegistry.h"
#include "ui/CompactController.h"
#include "update/UpdateService.h"
#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QUrl>

namespace palune {

namespace {
// 配置键常量：与 docs/SETTINGS-GAP.md §7 的键表一一对应。
const QString kCompactBelow = QStringLiteral("ui.layout.%1.compactBelow");
const QString kRestoreSession = QStringLiteral("ui.restoreLastSession");
const QString kAutoPlay = QStringLiteral("ui.autoPlay");
const QString kStartMax = QStringLiteral("ui.startMaximized");
const QString kCloseToTray = QStringLiteral("ui.closeToTray");
const QString kMinimizeToTray = QStringLiteral("ui.minimizeToTray");
const QString kConfirmExit = QStringLiteral("ui.confirmExit");

const QString kHwdec = QStringLiteral("playback.hwdec");
const QString kCache = QStringLiteral("playback.cacheSeconds");
const QString kToneMapping = QStringLiteral("playback.toneMapping");
const QString kTargetPeak = QStringLiteral("playback.targetPeakNits");
const QString kDeinterlace = QStringLiteral("playback.deinterlace");
const QString kResume = QStringLiteral("playback.resume");
const QString kAutoNext = QStringLiteral("playback.autoNext");
const QString kVolume = QStringLiteral("audio.volume");
const QString kMuted = QStringLiteral("audio.muted");
const QString kSpeed = QStringLiteral("playback.speed");

const QString kSubFont = QStringLiteral("sub.fontSize");
const QString kSubColor = QStringLiteral("sub.color");
const QString kSubOutline = QStringLiteral("sub.outline");
const QString kSubDelay = QStringLiteral("sub.delay");
const QString kSubPos = QStringLiteral("sub.position");
const QString kSubAuto = QStringLiteral("sub.autoLoad");
const QString kSubEncoding = QStringLiteral("sub.encoding");

const QString kAudioDevice = QStringLiteral("audio.device");
const QString kAudioChannels = QStringLiteral("audio.channels");
const QString kAudioDelay = QStringLiteral("audio.delay");

const QString kUpdateChannel = QStringLiteral("update.channel");
const QString kAutoCheck = QStringLiteral("update.autoCheck");
const QString kUpdateProxy = QStringLiteral("update.proxy");
const QString kDownloadDir = QStringLiteral("update.downloadDir");
} // namespace

Preferences::Preferences(Settings *settings, MpvClient *player, UpdateService *updater,
                         CompactController *compact, ActionRegistry *actions, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_player(player)
    , m_updater(updater)
    , m_compact(compact)
    , m_actions(actions)
{
    reload();

    // 键位覆盖：settings.toml 的 [keys] 表按 actionId 存键位字符串，空串表示解绑。
    rebuildKeyOverrides();
}

// ---------------------------------------------------------------------------
// 读取
// ---------------------------------------------------------------------------

QString Preferences::stringValue(const QString &key, const QString &fallback) const
{
    if (!m_settings)
        return fallback;
    const QVariant raw = m_settings->value(key);
    if (!raw.isValid() || raw.toString().isEmpty())
        return fallback;
    return raw.toString();
}

bool Preferences::boolValue(const QString &key, bool fallback) const
{
    if (!m_settings || !m_settings->value(key).isValid())
        return fallback;
    return m_settings->value(key).toBool();
}

int Preferences::intValue(const QString &key, int fallback) const
{
    if (!m_settings || !m_settings->value(key).isValid())
        return fallback;
    bool ok = false;
    const int value = m_settings->value(key).toInt(&ok);
    return ok ? value : fallback;
}

double Preferences::doubleValue(const QString &key, double fallback) const
{
    if (!m_settings || !m_settings->value(key).isValid())
        return fallback;
    bool ok = false;
    const double value = m_settings->value(key).toDouble(&ok);
    return ok ? value : fallback;
}

void Preferences::reload()
{
    m_compactBreakpoint = CompactController::kDefaultBreakpoint;
    if (m_compact) {
        m_compactBreakpoint = m_compact->breakpoint();
    } else if (m_settings) {
        m_compactBreakpoint = intValue(kCompactBelow.arg(QStringLiteral("A")),
                                       CompactController::kDefaultBreakpoint);
    }

    m_restoreLastSession = boolValue(kRestoreSession, false);
    m_autoPlay = boolValue(kAutoPlay, true);
    m_startMaximized = boolValue(kStartMax, false);
    m_closeToTray = boolValue(kCloseToTray, false);
    m_minimizeToTray = boolValue(kMinimizeToTray, false);
    m_confirmExit = boolValue(kConfirmExit, false);
    m_logLevel = stringValue(QStringLiteral("diag.logLevel"), QStringLiteral("info"));

    m_hwdecMode = stringValue(kHwdec, QStringLiteral("auto"));
    m_cacheSeconds = intValue(kCache, 0);
    m_toneMapping = boolValue(kToneMapping, true);
    m_targetPeakNits = intValue(kTargetPeak, 1000);
    m_deinterlace = stringValue(kDeinterlace, QStringLiteral("auto"));
    m_resumePlayback = boolValue(kResume, true);
    m_autoNext = boolValue(kAutoNext, false);

    m_subtitleFontSize = intValue(kSubFont, 15);
    m_subtitleColor = stringValue(kSubColor, QStringLiteral("#ffffff"));
    m_subtitleOutline = doubleValue(kSubOutline, 1.0);
    m_subtitleDelay = doubleValue(kSubDelay, 0.0);
    m_subtitlePosition = intValue(kSubPos, 8);
    m_autoLoadSubtitles = boolValue(kSubAuto, true);
    m_subtitleEncoding = stringValue(kSubEncoding, QStringLiteral("auto"));

    m_audioDevice = stringValue(kAudioDevice, QStringLiteral("auto"));
    m_audioChannels = stringValue(kAudioChannels, QStringLiteral("auto"));
    m_audioDelay = doubleValue(kAudioDelay, 0.0);

    m_updateChannel = stringValue(kUpdateChannel, QStringLiteral("stable"));
    m_autoCheckUpdates = boolValue(kAutoCheck, true);
    m_updateProxy = stringValue(kUpdateProxy, QStringLiteral("system"));
    m_downloadDir = stringValue(kDownloadDir, QString());
}

// ---------------------------------------------------------------------------
// 写入（即时落盘）
// ---------------------------------------------------------------------------

void Preferences::store(const QString &key, const QVariant &value)
{
    if (!m_settings)
        return;
    m_settings->setValue(key, value);
    QString error;
    if (!m_settings->save(&error))
        Log::warn(QStringLiteral("设置写入失败 %1：%2").arg(key, error));
}

void Preferences::setCompactBreakpoint(int value)
{
    const int clamped = qBound(CompactController::kMinBreakpoint, value,
                               CompactController::kMaxBreakpoint);
    if (clamped == m_compactBreakpoint)
        return;
    m_compactBreakpoint = clamped;
    const QString theme = m_compact ? m_compact->themeId() : QStringLiteral("A");
    store(kCompactBelow.arg(theme), clamped);
    if (m_compact)
        m_compact->reloadBreakpoint();
    emit preferencesChanged();
}

void Preferences::setRestoreLastSession(bool value)
{
    if (value == m_restoreLastSession) return;
    m_restoreLastSession = value;
    store(kRestoreSession, value);
    emit preferencesChanged();
}

void Preferences::setAutoPlay(bool value)
{
    if (value == m_autoPlay) return;
    m_autoPlay = value;
    store(kAutoPlay, value);
    emit preferencesChanged();
}

void Preferences::setStartMaximized(bool value)
{
    if (value == m_startMaximized) return;
    m_startMaximized = value;
    store(kStartMax, value);
    emit preferencesChanged();
}

void Preferences::setCloseToTray(bool value)
{
    if (value == m_closeToTray) return;
    m_closeToTray = value;
    store(kCloseToTray, value);
    emit preferencesChanged();
}

void Preferences::setMinimizeToTray(bool value)
{
    if (value == m_minimizeToTray) return;
    m_minimizeToTray = value;
    store(kMinimizeToTray, value);
    emit preferencesChanged();
}

void Preferences::setConfirmExit(bool value)
{
    if (value == m_confirmExit) return;
    m_confirmExit = value;
    store(kConfirmExit, value);
    emit preferencesChanged();
}

void Preferences::setLogLevel(const QString &value)
{
    const QString normalized = value.toLower();
    if (normalized == m_logLevel) return;
    if (!Log::setMinimumLevelName(normalized)) {
        Log::warn(QStringLiteral("非法日志级别：%1").arg(value));
        return;
    }
    m_logLevel = normalized;
    store(QStringLiteral("diag.logLevel"), normalized);
    emit preferencesChanged();
}

void Preferences::setHwdecMode(const QString &value)
{
    if (value == m_hwdecMode) return;
    m_hwdecMode = value;
    store(kHwdec, value);
    applyPlayback();
    emit preferencesChanged();
}

void Preferences::setCacheSeconds(int value)
{
    value = qBound(0, value, 120);
    if (value == m_cacheSeconds) return;
    m_cacheSeconds = value;
    store(kCache, value);
    applyPlayback();
    emit preferencesChanged();
}

void Preferences::setToneMapping(bool value)
{
    if (value == m_toneMapping) return;
    m_toneMapping = value;
    store(kToneMapping, value);
    applyPlayback();
    emit preferencesChanged();
}

void Preferences::setTargetPeakNits(int value)
{
    value = qBound(200, value, 2000);
    if (value == m_targetPeakNits) return;
    m_targetPeakNits = value;
    store(kTargetPeak, value);
    applyPlayback();
    emit preferencesChanged();
}

void Preferences::setDeinterlace(const QString &value)
{
    if (value == m_deinterlace) return;
    m_deinterlace = value;
    store(kDeinterlace, value);
    applyPlayback();
    emit preferencesChanged();
}

void Preferences::setResumePlayback(bool value)
{
    if (value == m_resumePlayback) return;
    m_resumePlayback = value;
    store(kResume, value);
    emit preferencesChanged();
}

void Preferences::setAutoNext(bool value)
{
    if (value == m_autoNext) return;
    m_autoNext = value;
    store(kAutoNext, value);
    emit preferencesChanged();
}

void Preferences::setSubtitleFontSize(int value)
{
    value = qBound(8, value, 64);
    if (value == m_subtitleFontSize) return;
    m_subtitleFontSize = value;
    store(kSubFont, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setSubtitleColor(const QString &value)
{
    if (value == m_subtitleColor) return;
    m_subtitleColor = value;
    store(kSubColor, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setSubtitleOutline(double value)
{
    value = qBound(0.0, value, 6.0);
    if (qFuzzyCompare(value + 1.0, m_subtitleOutline + 1.0)) return;
    m_subtitleOutline = value;
    store(kSubOutline, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setSubtitleDelay(double value)
{
    value = qBound(-10.0, value, 10.0);
    if (qFuzzyCompare(value + 1.0, m_subtitleDelay + 1.0)) return;
    m_subtitleDelay = value;
    store(kSubDelay, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setSubtitlePosition(int value)
{
    value = qBound(0, 40, value);
    if (value == m_subtitlePosition) return;
    m_subtitlePosition = value;
    store(kSubPos, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setAutoLoadSubtitles(bool value)
{
    if (value == m_autoLoadSubtitles) return;
    m_autoLoadSubtitles = value;
    store(kSubAuto, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setSubtitleEncoding(const QString &value)
{
    if (value == m_subtitleEncoding) return;
    m_subtitleEncoding = value;
    store(kSubEncoding, value);
    applySubtitles();
    emit preferencesChanged();
}

void Preferences::setAudioDevice(const QString &value)
{
    if (value == m_audioDevice) return;
    m_audioDevice = value;
    store(kAudioDevice, value);
    applyAudio();
    emit preferencesChanged();
}

void Preferences::setAudioChannels(const QString &value)
{
    if (value == m_audioChannels) return;
    m_audioChannels = value;
    store(kAudioChannels, value);
    applyAudio();
    emit preferencesChanged();
}

void Preferences::setAudioDelay(double value)
{
    value = qBound(-10.0, value, 10.0);
    if (qFuzzyCompare(value + 1.0, m_audioDelay + 1.0)) return;
    m_audioDelay = value;
    store(kAudioDelay, value);
    applyAudio();
    emit preferencesChanged();
}

void Preferences::setUpdateChannel(const QString &value)
{
    if (value == m_updateChannel) return;
    m_updateChannel = value;
    store(kUpdateChannel, value);
    applyUpdates();
    emit preferencesChanged();
}

void Preferences::setAutoCheckUpdates(bool value)
{
    if (value == m_autoCheckUpdates) return;
    m_autoCheckUpdates = value;
    store(kAutoCheck, value);
    emit preferencesChanged();
}

void Preferences::setUpdateProxy(const QString &value)
{
    if (value == m_updateProxy) return;
    m_updateProxy = value;
    store(kUpdateProxy, value);
    applyUpdates();
    emit preferencesChanged();
}

void Preferences::setDownloadDir(const QString &value)
{
    if (value == m_downloadDir) return;
    m_downloadDir = value;
    store(kDownloadDir, value);
    applyUpdates();
    emit preferencesChanged();
}

// ---------------------------------------------------------------------------
// 作用到播放器 / 更新模块
// ---------------------------------------------------------------------------

void Preferences::applyPlayback()
{
    if (!m_player)
        return;
    m_player->setHwdecMode(m_hwdecMode);
    m_player->setCacheSeconds(m_cacheSeconds);
    m_player->setToneMapping(m_toneMapping, m_targetPeakNits);
    m_player->setDeinterlace(m_deinterlace);
}

void Preferences::applySubtitles()
{
    if (!m_player)
        return;
    m_player->setSubtitleStyle(m_subtitleFontSize, m_subtitleColor, m_subtitleOutline,
                               m_subtitlePosition);
    m_player->setSubtitleDelayAbsolute(m_subtitleDelay);
    m_player->setSubtitleAutoLoad(m_autoLoadSubtitles);
    m_player->setSubtitleEncoding(m_subtitleEncoding);
}

void Preferences::applyAudio()
{
    if (!m_player)
        return;
    m_player->setAudioDevice(m_audioDevice);
    m_player->setAudioChannels(m_audioChannels);
    m_player->setAudioDelay(m_audioDelay);
}

void Preferences::applyUpdates()
{
    QNetworkProxy proxy;
    if (m_updateProxy.startsWith(QLatin1String("manual:"))) {
        const QString endpoint = m_updateProxy.mid(7);
        const QStringList parts = endpoint.split(QLatin1Char(':'));
        if (parts.size() == 2)
            proxy = QNetworkProxy(QNetworkProxy::HttpProxy, parts.at(0), parts.at(1).toUShort());
    } else if (m_updateProxy == QLatin1String("none")) {
        proxy = QNetworkProxy(QNetworkProxy::NoProxy);
    }
    // system：不设置，沿用 Qt 的系统代理配置。
    QNetworkProxy::setApplicationProxy(proxy);

    if (m_updater) {
        m_updater->setChannel(m_updateChannel);
        m_updater->setDownloadDirOverride(m_downloadDir);
    }
}

void Preferences::applyAll()
{
    applyPlayback();
    applySubtitles();
    applyAudio();
    applyUpdates();

    if (m_settings) {
        if (m_player) {
            m_player->setVolume(doubleValue(kVolume, 100.0));
            if (boolValue(kMuted, false))
                m_player->toggleMute();
            const double speed = doubleValue(kSpeed, 1.0);
            if (speed > 0.25 && speed < 4.0)
                m_player->setSpeed(speed);
        }
    }
}

void Preferences::saveSessionState()
{
    if (!m_settings)
        return;
    if (m_player) {
        store(kVolume, m_player->volume());
        store(kMuted, m_player->muted());
        store(kSpeed, m_player->speed());
    }
}

// ---------------------------------------------------------------------------
// QML 辅助
// ---------------------------------------------------------------------------

QVariantList Preferences::audioDevices() const
{
    return m_player ? m_player->audioDevices() : QVariantList();
}

QString Preferences::keySpec(const QString &id) const
{
    if (!m_actions)
        return QString();
    return m_actions->keysFor(id, ActionRegistry::currentPlatform());
}

QVariantList Preferences::keyRows() const
{
    QVariantList rows;
    if (!m_actions)
        return rows;
    for (const ActionRegistry::Action &action : ActionRegistry::actionTable()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), action.id);
        row.insert(QStringLiteral("label"), action.zh);
        row.insert(QStringLiteral("group"), action.group);
        row.insert(QStringLiteral("key"), keySpec(action.id));
        row.insert(QStringLiteral("defaultKey"),
                   m_actions->defaultKeyFor(action.id, ActionRegistry::currentPlatform()));
        row.insert(QStringLiteral("custom"),
                   m_settings && m_settings->value(QStringLiteral("keys.") + action.id).isValid());
        rows.append(row);
    }
    return rows;
}

void Preferences::rebuildKeyOverrides()
{
    if (!m_actions || !m_settings)
        return;
    QVariantMap overrides;
    for (const ActionRegistry::Action &action : ActionRegistry::actionTable()) {
        const QString key = QStringLiteral("keys.") + action.id;
        if (m_settings->value(key).isValid())
            overrides.insert(action.id, m_settings->value(key).toString());
    }
    m_actions->setOverrides(overrides);
}

void Preferences::setKeyBinding(const QString &id, const QString &spec)
{
    if (!m_settings || id.isEmpty())
        return;
    store(QStringLiteral("keys.") + id, spec);
    rebuildKeyOverrides();
    emit preferencesChanged();
}

void Preferences::clearKeyBinding(const QString &id)
{
    if (!m_settings || id.isEmpty())
        return;
    // 用空串表示「解绑」，与「没有覆盖」区分。
    store(QStringLiteral("keys.") + id, QString());
    rebuildKeyOverrides();
    emit preferencesChanged();
}

void Preferences::resetKeyBinding(const QString &id)
{
    if (!m_settings || id.isEmpty())
        return;
    m_settings->removeValue(QStringLiteral("keys.") + id);
    QString error;
    if (!m_settings->save(&error))
        Log::warn(QStringLiteral("键位恢复失败：%1").arg(error));
    rebuildKeyOverrides();
    emit preferencesChanged();
}

QString Preferences::specForKey(int key, int modifiers) const
{
    if (!m_actions)
        return QString();
    return m_actions->specForKey(key, modifiers);
}

QVariantList Preferences::keyConflicts() const
{
    return m_actions ? m_actions->conflicts() : QVariantList();
}

QString Preferences::downloadDirEffective() const
{
    if (!m_downloadDir.isEmpty())
        return m_downloadDir;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QDir::toNativeSeparators(base + QStringLiteral("/Palune"));
}

void Preferences::chooseDownloadDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("选择更新包下载目录"), downloadDirEffective());
    if (!dir.isEmpty())
        setDownloadDir(QDir::toNativeSeparators(dir));
}

void Preferences::chooseSubtitleFile()
{
    const QString file = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("加载外挂字幕"), QString(),
        QStringLiteral("字幕文件 (*.srt *.ass *.ssa *.vtt *.sub);;所有文件 (*)"));
    if (file.isEmpty() || !m_player)
        return;
    m_player->addSubtitleFile(file);
}

void Preferences::openLicense()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/NOTICE.md"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/palune/NOTICE.md"),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return;
        }
    }
    Log::warn(QStringLiteral("未找到 NOTICE.md，无法打开许可说明"));
}

void Preferences::openConfigDir()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::configDir()));
}

void Preferences::openCrashDir()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::crashDir()));
}

} // namespace palune
