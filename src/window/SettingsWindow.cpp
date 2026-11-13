#include "window/SettingsWindow.h"

#include "media/MediaLibraryService.h"
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScreen>
#include <QShowEvent>

#include "mpv/MpvClient.h"
#include "settings/Preferences.h"
#include "theme/ThemeRegistry.h"
#include "ui/CompactController.h"
#include "update/UpdateService.h"
#include "window/WindowController.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace palune {

namespace {
constexpr int kToolWindowExStyle = 0x00000080; // WS_EX_TOOLWINDOW
}

SettingsWindow::SettingsWindow(WindowController *controller,
                               ThemeRegistry *theme,
                               MpvClient *player,
                               UpdateService *updater,
                               Settings *settings,
                               CompactController *compact,
                               QQuickView *owner,
                               QObject *parent)
    : QQuickView(nullptr)
{
    Q_UNUSED(parent);
    m_controller = controller;
    m_theme = theme;
    m_owner = owner;

    // 真正的独立顶层窗口：不设瞬态父窗、不使用 Qt::Tool。
    // 早期用 Tool + setTransientParent 会让设置窗在系统层面从属于主窗：主窗最小化/切走时
    // 设置窗跟着被盖住，用户没法“只开设置窗调参数”。现在它是普通顶层窗口，
    // 仅通过原生 WS_EX_TOOLWINDOW 隐藏任务栏条目，位置不再跟随主窗。
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setTitle(QStringLiteral("Palune 设置"));

    QQmlContext *ctx = engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("win"), controller);
    ctx->setContextProperty(QStringLiteral("themeRegistry"), theme);
    ctx->setContextProperty(QStringLiteral("player"), player);
    ctx->setContextProperty(QStringLiteral("updater"), updater);
    ctx->setContextProperty(QStringLiteral("compactMode"), compact);
    ctx->setContextProperty(QStringLiteral("settingsWindow"), this);
    ctx->setContextProperty(QStringLiteral("prefs"), controller ? controller->preferences() : nullptr);
    ctx->setContextProperty(QStringLiteral("library"), controller ? controller->library() : nullptr);
    const QString current = theme ? theme->currentTheme() : QStringLiteral("A");
    ctx->setContextProperty(QStringLiteral("settingsPanelRadius"), radiusForTheme(current));

    setSource(QUrl(QStringLiteral("qrc:/qml/SettingsWindow.qml")));
    resize(sizeForTheme(current));
}

QSize SettingsWindow::sizeForTheme(const QString &theme)
{
    if (theme == QLatin1String("C"))
        return QSize(560, 452);
    if (theme == QLatin1String("B"))
        return QSize(1080, 660);
    return QSize(1020, 660);
}

int SettingsWindow::radiusForTheme(const QString &theme)
{
    return theme == QLatin1String("B") ? 8 : 18;
}

void SettingsWindow::open(const QRect &ownerLogical)
{
    if (!m_theme)
        return;
    // refreshForTheme -> reposition 内部有 isVisible 守卫，窗口首次 show 之前会被跳过，
    // 导致 C 主题（右下角弹出卡）按 Qt 默认居中。这里先 show 再定位。
    refreshForTheme(ownerLogical);
    show();
    reposition(ownerLogical);
    raise();
    requestActivate();
}

void SettingsWindow::reposition(const QRect &ownerLogical)
{
    Q_UNUSED(ownerLogical);
    if (!isVisible())
        return;
    const QSize target = sizeForTheme(m_theme->currentTheme());
    QScreen *scr = m_owner ? m_owner->screen() : screen();
    // 独立窗口：只按当前屏幕居中一次，不再贴着主窗右下角或随主窗移动。
    QRect rect = scr ? scr->availableGeometry() : QRect(0, 0, target.width(), target.height());
    rect = QRect(rect.x() + (rect.width() - target.width()) / 2,
                 rect.y() + (rect.height() - target.height()) / 2,
                 target.width(), target.height());


    if (scr) {
        const QRect available = scr->availableGeometry();
        if (rect.right() > available.right())
            rect.moveRight(available.right());
        if (rect.bottom() > available.bottom())
            rect.moveBottom(available.bottom());
        if (rect.left() < available.left())
            rect.moveLeft(available.left());
        if (rect.top() < available.top())
            rect.moveTop(available.top());
    }
    setGeometry(rect);
}

void SettingsWindow::refreshForTheme(const QRect &ownerLogical)
{
    if (!m_theme)
        return;
    const QString theme = m_theme->currentTheme();
    const int radius = radiusForTheme(theme);
    if (QQmlContext *ctx = engine() ? engine()->rootContext() : nullptr)
        ctx->setContextProperty(QStringLiteral("settingsPanelRadius"), radius);
    resize(sizeForTheme(theme));
    applyNativeRegion();
    reposition(ownerLogical);
}

void SettingsWindow::closeSelf()
{
    if (m_controller)
        m_controller->closeSettingsWindow();
    else {
        hide();
        if (m_owner)
            m_owner->requestActivate();
    }
}

void SettingsWindow::setPage(int page)
{
    if (QQuickItem *item = rootObject())
        item->setProperty("page", qBound(0, page, 7));
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    QQuickView::showEvent(event);
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | kToolWindowExStyle);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~WS_THICKFRAME; // 设置窗固定尺寸，不保留系统缩放边框。
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    const DWORD corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
#endif
    applyNativeRegion();
}

void SettingsWindow::applyNativeRegion()
{
#ifdef Q_OS_WIN
    if (width() <= 0 || height() <= 0)
        return;
    const QString theme = m_theme ? m_theme->currentTheme() : QStringLiteral("A");
    const double dpr = devicePixelRatio();
    const int physicalRadius = qRound(radiusForTheme(theme) * dpr);
    const int w = qRound(width() * dpr);
    const int h = qRound(height() * dpr);
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, physicalRadius * 2, physicalRadius * 2);
    SetWindowRgn(reinterpret_cast<HWND>(winId()), region, TRUE);
#endif
}

} // namespace palune
