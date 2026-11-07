#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace palune {

// settings.toml 读写（M1）。文件位置由 AppPaths 决定，平台目录已冻结。
// 读取失败不阻塞启动：调用方记录日志后按默认值继续。
class Settings
{
public:
    explicit Settings(QString path = QString());

    QString path() const { return m_path; }
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);
    bool isFirstRun() const { return m_firstRun; }
    bool parseFailed() const { return m_parseFailed; }

    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    void setValue(const QString &key, const QVariant &value);

    // 主题原始值：空字符串表示缺键（由 ThemeRegistry 判定是否回退）。
    QString rawTheme() const;
    bool setTheme(const QString &themeId);
    QString rawLogLevel() const;

    bool hasWindowGeometry() const;
    QRect windowGeometry() const;
    void setWindowGeometry(const QRect &rect);
    void setWindowMaximized(bool maximized);
    bool windowMaximized() const;

    QStringList recentFiles() const;
    void addRecentFile(const QString &path, int limit = 10);
    void clearRecentFiles();

    // 每主题独立的检查器/抽屉宽度（UiStateStore 持久化用）。
    int panelWidth(const QString &themeId) const;
    void setPanelWidth(const QString &themeId, int width);

    // 播放位置记忆（按文件路径哈希存，强杀后续播用）。
    double resumePosition(const QString &path) const;
    void setResumePosition(const QString &path, double seconds);

private:
    QString m_path;
    QVariantMap m_values;
    bool m_firstRun = true;
    bool m_parseFailed = false;
};

} // namespace palune
