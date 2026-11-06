#pragma once

#include <QQuickView>

namespace palune {

// UI 覆盖窗口：应用窗口身份（任务栏、Alt-Tab、焦点），透明背景，
// 视频由下方的独立 VideoWindow 呈现，两窗几何严格同步。
class OverlayWindow : public QQuickView
{
    Q_OBJECT
public:
    explicit OverlayWindow(QObject *parent = nullptr);

signals:
    void closed();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void applyNativeStyles();
};

} // namespace palune
