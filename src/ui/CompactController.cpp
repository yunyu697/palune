#include "ui/CompactController.h"

#include "diag/Log.h"
#include "settings/Settings.h"

namespace palune {

namespace {
QString breakpointKey(const QString &theme)
{
    return QStringLiteral("ui.layout.%1.compactBelow").arg(theme);
}
} // namespace

CompactController::CompactController(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDefaultDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &CompactController::applyPending);
    loadBreakpoint();
}

bool CompactController::parseBreakpoint(const QVariant &raw, int *out, QString *notice)
{
    bool ok = false;
    const int value = raw.toInt(&ok);
    if (ok && value >= kMinBreakpoint && value <= kMaxBreakpoint) {
        if (out)
            *out = value;
        if (notice)
            notice->clear();
        return true;
    }
    if (out)
        *out = kDefaultBreakpoint;
    if (notice) {
        *notice = QStringLiteral("compactBelow 非法值 \"%1\"，已回退 %2")
                      .arg(raw.toString())
                      .arg(kDefaultBreakpoint);
    }
    return false;
}

void CompactController::reloadBreakpoint()
{
    loadBreakpoint();
}

void CompactController::setTheme(const QString &themeId)
{
    if (m_theme == themeId)
        return;
    m_theme = themeId;
    loadBreakpoint();
}

void CompactController::loadBreakpoint()
{
    if (!m_settings) {
        m_breakpoint = kDefaultBreakpoint;
        return;
    }
    const QVariant raw = m_settings->value(breakpointKey(m_theme));
    int value = kDefaultBreakpoint;
    QString notice;
    if (raw.isValid()) {
        if (!parseBreakpoint(raw, &value, &notice)) {
            if (!m_noticeLogged) {
                Log::warn(QStringLiteral("紧凑模式：%1").arg(notice));
                m_noticeLogged = true;
            }
        } else {
            m_noticeLogged = false;
        }
    } else {
        notice.clear();
        m_noticeLogged = false;
    }
    if (m_breakpoint != value) {
        m_breakpoint = value;
        emit breakpointChanged();
    }
    if (m_notice != notice) {
        m_notice = notice;
        emit configNoticeChanged();
    }
}

void CompactController::feedWidth(int width)
{
    if (width <= 0)
        return;
    m_pendingWidth = width;
    m_debounce.start();
}

void CompactController::flush()
{
    m_debounce.stop();
    applyPending();
}

void CompactController::applyPending()
{
    if (m_pendingWidth <= 0)
        return;
    const int width = m_pendingWidth;
    m_pendingWidth = -1;
    if (!m_compact && width <= m_breakpoint - kDefaultHysteresis)
        setCompact(true);
    else if (m_compact && width >= m_breakpoint + kDefaultHysteresis)
        setCompact(false);
}

void CompactController::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    ++m_switches;
    emit compactChanged();
    emit switchCountChanged();
    Log::info(QStringLiteral("紧凑模式：%1（第 %2 次切换）")
                  .arg(compact ? QStringLiteral("紧凑") : QStringLiteral("常规"))
                  .arg(m_switches));
}

void CompactController::reset()
{
    m_debounce.stop();
    m_pendingWidth = -1;
    m_switches = 0;
    m_compact = false;
    emit compactChanged();
    emit switchCountChanged();
}

} // namespace palune
