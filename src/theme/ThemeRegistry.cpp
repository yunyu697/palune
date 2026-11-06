#include "theme/ThemeRegistry.h"

#include "diag/Log.h"
#include "settings/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace palune {

namespace {
const QString kFallbackTheme = QStringLiteral("A");

QString qrcRoot()
{
    return QStringLiteral(":/qml/layouts/");
}
} // namespace

ThemeRegistry::ThemeRegistry(Settings *settings, ResourceChecker checker, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_checker(std::move(checker))
{
    if (!m_checker) {
        m_checker = [](const QString &path) { return QFile::exists(path); };
    }
    resolveInitialTheme();
}

QUrl ThemeRegistry::layoutUrl(const QString &themeId)
{
    return QUrl(QStringLiteral("qrc:/qml/layouts/Layout%1/Layout%1.qml").arg(themeId));
}

QString ThemeRegistry::qrcLayoutPath(const QString &themeId)
{
    return QStringLiteral(":/qml/layouts/Layout%1/Layout%1.qml").arg(themeId);
}

bool ThemeRegistry::isValidTheme(const QString &themeId)
{
    return availableThemes().contains(themeId);
}

bool ThemeRegistry::resourceExists(const QString &themeId) const
{
    return m_checker(qrcLayoutPath(themeId));
}

void ThemeRegistry::resolveInitialTheme()
{
    const QString raw = m_settings ? m_settings->rawTheme() : QString();
    if (raw.isEmpty()) {
        m_currentTheme = kFallbackTheme;
        m_fallbackReason = QStringLiteral("缺少 [ui] theme，已回退到布局 A");
        Log::warn(QStringLiteral("主题：%1").arg(m_fallbackReason));
        return;
    }
    if (!isValidTheme(raw)) {
        m_currentTheme = kFallbackTheme;
        m_fallbackReason = QStringLiteral("非法主题值 \"%1\"，已回退到布局 A").arg(raw);
        Log::warn(QStringLiteral("主题：%1").arg(m_fallbackReason));
        return;
    }
    if (!resourceExists(raw)) {
        m_currentTheme = kFallbackTheme;
        m_fallbackReason = QStringLiteral("主题 %1 资源缺失，已回退到布局 A").arg(raw);
        Log::warn(QStringLiteral("主题：%1").arg(m_fallbackReason));
        return;
    }
    m_currentTheme = raw;
    m_fallbackReason.clear();
}

ThemeRegistry::SwitchResult ThemeRegistry::switchTo(const QString &themeId)
{
    SwitchResult result;
    result.theme = themeId;
    QElapsedTimer timer;
    timer.start();

    if (!isValidTheme(themeId)) {
        result.error = QStringLiteral("未知主题：%1").arg(themeId);
        m_lastSwitchMs = timer.nsecsElapsed() / 1.0e6;
        Log::error(QStringLiteral("主题切换失败：%1").arg(result.error));
        return result;
    }
    if (!resourceExists(themeId)) {
        result.error = QStringLiteral("主题 %1 资源缺失").arg(themeId);
        m_lastSwitchMs = timer.nsecsElapsed() / 1.0e6;
        Log::error(QStringLiteral("主题切换失败：%1").arg(result.error));
        return result;
    }

    result.changed = m_currentTheme != themeId;
    m_currentTheme = themeId;
    m_fallbackReason.clear();
    if (m_settings && m_settings->setTheme(themeId)) {
        QString saveError;
        if (!m_settings->save(&saveError))
            Log::warn(QStringLiteral("主题持久化失败：%1").arg(saveError));
    }
    m_lastSwitchMs = timer.nsecsElapsed() / 1.0e6;
    result.ok = true;
    result.elapsedMs = m_lastSwitchMs;

    m_switchLog << QStringLiteral("%1 -> %2 (%3ms)")
                       .arg(themeId)
                       .arg(result.changed ? QStringLiteral("重建") : QStringLiteral("无变化"))
                       .arg(QString::number(m_lastSwitchMs, 'f', 1));
    while (m_switchLog.size() > 50)
        m_switchLog.removeFirst();

    emit fallbackReasonChanged();
    emit currentThemeChanged();
    Log::info(QStringLiteral("主题切换为 %1，耗时 %2ms")
                  .arg(themeId, QString::number(m_lastSwitchMs, 'f', 2)));
    return result;
}

QString ThemeRegistry::cycleTheme()
{
    const QStringList themes = availableThemes();
    const int index = themes.indexOf(m_currentTheme);
    const QString next = themes.at((index + 1) % themes.size());
    switchTo(next);
    return m_currentTheme;
}

QString ThemeRegistry::switchLogText() const
{
    return m_switchLog.join(QLatin1Char('\n'));
}

} // namespace palune
