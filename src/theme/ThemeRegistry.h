#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

class QQuickItem;

namespace palune {

class Settings;

// L3：主题注册表。负责主题枚举、合法性校验、资源存在性校验、持久化与回退。
// 回退规则（M1 完成标志 10）：缺键 / 非法值 / 主题资源缺失 → A，且不阻塞启动。
class ThemeRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY currentThemeChanged)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY fallbackReasonChanged)
    Q_PROPERTY(QStringList themes READ themes CONSTANT)
    Q_PROPERTY(QUrl layoutSource READ layoutSource NOTIFY currentThemeChanged)
    Q_PROPERTY(double lastSwitchMs READ lastSwitchMs NOTIFY currentThemeChanged)

public:
    using ResourceChecker = std::function<bool(const QString &qrcPath)>;

    struct SwitchResult {
        bool ok = false;
        QString theme;
        QString error;
        double elapsedMs = 0.0;
        bool changed = false;
    };

    explicit ThemeRegistry(Settings *settings, ResourceChecker checker = {}, QObject *parent = nullptr);

    static QStringList availableThemes() { return {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}; }
    static QUrl layoutUrl(const QString &themeId);
    static bool isValidTheme(const QString &themeId);

    QString currentTheme() const { return m_currentTheme; }
    QString fallbackReason() const { return m_fallbackReason; }
    QStringList themes() const { return availableThemes(); }
    QUrl layoutSource() const { return layoutUrl(m_currentTheme); }
    double lastSwitchMs() const { return m_lastSwitchMs; }
    QStringList switchLog() const { return m_switchLog; }

    // 校验 + 资源检查 + 持久化 + 发出信号。耗时包含校验与持久化。
    SwitchResult switchTo(const QString &themeId);
    Q_INVOKABLE QString cycleTheme();
    Q_INVOKABLE QString switchLogText() const;

signals:
    void currentThemeChanged();
    void fallbackReasonChanged();

private:
    void resolveInitialTheme();
    bool resourceExists(const QString &themeId) const;
    static QString qrcLayoutPath(const QString &themeId);

    Settings *m_settings = nullptr;
    ResourceChecker m_checker;
    QString m_currentTheme = QStringLiteral("A");
    QString m_fallbackReason;
    double m_lastSwitchMs = 0.0;
    QStringList m_switchLog;
};

} // namespace palune
