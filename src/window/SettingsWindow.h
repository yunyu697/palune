#pragma once

#include <QQuickView>
#include <QRect>
#include <QString>

namespace palune {

class WindowController;
class ThemeRegistry;
class MpvClient;
class UpdateService;
class Settings;
class CompactController;

// 设置窗口：真正的顶层窗口（Tool + 瞬态父窗），不再作为主窗内的 QML 弹层。
// 原因：主窗是透明无边框窗且带原生 Region 裁剪，任何子级弹层都会被主窗边界裁掉，
// 也会被视频子窗的 Z 序压住；设置必须脱离这条父子/裁剪链。
class SettingsWindow : public QQuickView
{
    Q_OBJECT

public:
    SettingsWindow(WindowController *controller,
                   ThemeRegistry *theme,
                   MpvClient *player,
                   UpdateService *updater,
                   Settings *settings,
                   CompactController *compact,
                   QQuickView *owner,
                   QObject *parent = nullptr);

    // 按主题决定尺寸（A 1020×660 / B 1080×660 / C 560×452）。
    static QSize sizeForTheme(const QString &theme);
    // 面板圆角（A/C 18、B 8），与设计稿 --radius-panel 一致。
    static int radiusForTheme(const QString &theme);

    void open(const QRect &ownerLogical);
    void reposition(const QRect &ownerLogical);
    // 主题变化后重新取尺寸/圆角并回到锚点。
    void refreshForTheme(const QRect &ownerLogical);
    void closeSelf();

    bool isOpen() const { return isVisible(); }

    Q_INVOKABLE void requestClose() { closeSelf(); }

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyNativeRegion();

    WindowController *m_controller = nullptr;
    ThemeRegistry *m_theme = nullptr;
    QQuickView *m_owner = nullptr;
};

} // namespace palune
