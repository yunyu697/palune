#include "settings/Settings.h"

#include "core/AppPaths.h"
#include "settings/Toml.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace palune {

namespace {
const QString kTheme = QStringLiteral("ui.theme");
const QString kLogLevel = QStringLiteral("diag.logLevel");
const QString kWinX = QStringLiteral("window.x");
const QString kWinY = QStringLiteral("window.y");
const QString kWinW = QStringLiteral("window.width");
const QString kWinH = QStringLiteral("window.height");
const QString kWinMax = QStringLiteral("window.maximized");
const QString kRecent = QStringLiteral("files.recent");

QString defaultTheme()
{
    // 缺省主题：A（回退目标，静态检查与单测依赖这个约定）。
    return QStringLiteral("A");
}
} // namespace

Settings::Settings(QString path)
    : m_path(path.isEmpty() ? AppPaths::settingsFile() : std::move(path))
{
}

bool Settings::load(QString *error)
{
    m_values.clear();
    m_firstRun = true;
    m_parseFailed = false;
    QFile file(m_path);
    if (!file.exists()) {
        m_firstRun = true;
        if (error) *error = QString();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_parseFailed = true;
        if (error) *error = QStringLiteral("无法读取 %1：%2").arg(m_path, file.errorString());
        return false;
    }
    QString parseError;
    const QVariantMap parsed = Toml::parse(QString::fromUtf8(file.readAll()), &parseError);
    file.close();
    if (!parseError.isEmpty()) {
        m_parseFailed = true;
        if (error) *error = QStringLiteral("settings.toml 解析失败：%1").arg(parseError);
        return false;
    }
    m_values = parsed;
    m_firstRun = false;
    if (error) *error = QString();
    return true;
}

bool Settings::save(QString *error)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QString text = QStringLiteral("# Palune 配置文件（M1 起格式冻结，未知键会保留）\n");
    text += Toml::serialize(m_values);
    // 原子写：先写临时文件再替换，避免断电/崩溃留下半份配置。
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入 %1：%2").arg(m_path, file.errorString());
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        if (error) *error = QStringLiteral("保存失败：%1").arg(file.errorString());
        return false;
    }
    m_firstRun = false;
    return true;
}

QVariant Settings::value(const QString &key, const QVariant &fallback) const
{
    auto it = m_values.constFind(key);
    return it == m_values.constEnd() ? fallback : it.value();
}

void Settings::setValue(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
}

QString Settings::rawTheme() const
{
    return value(kTheme).toString();
}

bool Settings::setTheme(const QString &themeId)
{
    const QString current = rawTheme();
    setValue(kTheme, themeId);
    return current != themeId;
}

QString Settings::rawLogLevel() const
{
    return value(kLogLevel, QStringLiteral("info")).toString();
}

bool Settings::hasWindowGeometry() const
{
    return value(kWinW).toInt() > 0 && value(kWinH).toInt() > 0;
}

QRect Settings::windowGeometry() const
{
    return QRect(value(kWinX).toInt(), value(kWinY).toInt(),
                 value(kWinW).toInt(), value(kWinH).toInt());
}

void Settings::setWindowGeometry(const QRect &rect)
{
    setValue(kWinX, rect.x());
    setValue(kWinY, rect.y());
    setValue(kWinW, rect.width());
    setValue(kWinH, rect.height());
}

void Settings::setWindowMaximized(bool maximized)
{
    setValue(kWinMax, maximized);
}

bool Settings::windowMaximized() const
{
    return value(kWinMax, false).toBool();
}

QStringList Settings::recentFiles() const
{
    return value(kRecent).toStringList();
}

void Settings::addRecentFile(const QString &path, int limit)
{
    if (path.isEmpty())
        return;
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > qMax(1, limit))
        files.removeLast();
    setValue(kRecent, files);
}

void Settings::clearRecentFiles()
{
    setValue(kRecent, QStringList());
}

int Settings::panelWidth(const QString &themeId) const
{
    const int width = value(QStringLiteral("ui.layout.%1.panelWidth").arg(themeId)).toInt();
    return width > 0 ? width : 320;
}

void Settings::setPanelWidth(const QString &themeId, int width)
{
    setValue(QStringLiteral("ui.layout.%1.panelWidth").arg(themeId), qMax(0, width));
}

double Settings::resumePosition(const QString &path) const
{
    if (path.isEmpty())
        return 0.0;
    // 调用方可能传 Windows 原生分隔符（--open）或 QUrl 的正斜杠路径，统一归一化后再哈希。
    const QString normalized = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    const QString id = QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
    // 序列化时 "resume.<id>" 会写成 [resume] 表；解析后键又拼回扁平形式。
    return value(QStringLiteral("resume.") + id).toDouble();
}

void Settings::setResumePosition(const QString &path, double seconds)
{
    if (path.isEmpty() || seconds <= 0.0)
        return;
    const QString normalized = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    const QString id = QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
    setValue(QStringLiteral("resume.") + id, seconds);
}

} // namespace palune
