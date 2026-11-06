#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

namespace palune {

class Settings;

// M2：紧凑模式断点判定。三套布局共用一个判定器：
// - 断点来自 settings.toml 的 [ui.layout.<theme>] compactBelow，非法值回退默认并记录诊断日志；
// - 滞回带默认 ±24px，宽度在带内不切换，避免阈值附近来回抖动；
// - 60ms 防抖，10Hz 连续拖动不会每个样本都改变模式。
class CompactController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool compact READ compact NOTIFY compactChanged)
    Q_PROPERTY(int breakpoint READ breakpoint NOTIFY breakpointChanged)
    Q_PROPERTY(int hysteresis READ hysteresis CONSTANT)
    Q_PROPERTY(int switchCount READ switchCount NOTIFY switchCountChanged)
    Q_PROPERTY(QString configNotice READ configNotice NOTIFY configNoticeChanged)

public:
    static constexpr int kDefaultBreakpoint = 560;
    static constexpr int kDefaultHysteresis = 24;
    static constexpr int kDefaultDebounceMs = 60;
    static constexpr int kMinBreakpoint = 320;
    static constexpr int kMaxBreakpoint = 2400;

    explicit CompactController(Settings *settings = nullptr, QObject *parent = nullptr);

    // 切换主题时重新读取该主题自己的断点。
    void setTheme(const QString &themeId);
    QString themeId() const { return m_theme; }

    bool compact() const { return m_compact; }
    int breakpoint() const { return m_breakpoint; }
    int hysteresis() const { return kDefaultHysteresis; }
    int switchCount() const { return m_switches; }
    QString configNotice() const { return m_notice; }

    // 视口宽度输入；立即返回，防抖后按滞回判定。
    Q_INVOKABLE void feedWidth(int width);
    // 重新读取当前主题的断点（配置变更或证据采集用）。
    void reloadBreakpoint();
    // 跳过防抖直接判定（证据采集与单测用）。
    Q_INVOKABLE void flush();
    Q_INVOKABLE void reset();

    // 断点合法性：320..2400 的整数；否则 out 为默认值，notice 说明回退原因。
    static bool parseBreakpoint(const QVariant &raw, int *out, QString *notice);

signals:
    void compactChanged();
    void breakpointChanged();
    void switchCountChanged();
    void configNoticeChanged();

private:
    void applyPending();
    void setCompact(bool compact);
    void loadBreakpoint();

    Settings *m_settings = nullptr;
    QTimer m_debounce;
    int m_pendingWidth = -1;
    bool m_compact = false;
    int m_breakpoint = kDefaultBreakpoint;
    int m_switches = 0;
    QString m_theme = QStringLiteral("A");
    QString m_notice;
    bool m_noticeLogged = false;
};

} // namespace palune
