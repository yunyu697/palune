#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVector>

#include <functional>
#include <map>

namespace palune {

// M2：40 个 P0 动作的唯一注册表与键盘分发器。
// - 每套主题的控件只声明 actionId，键位由这里统一绑定；
// - 默认键位按平台给出（macOS 把 Ctrl 换成 Meta），启动时检测冲突，默认冲突必须为 0；
// - dispatch 走 WindowController::action，QML 不直接调用播放器。
class ActionRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count CONSTANT)

public:
    struct Action {
        QString id;
        QString zh;
        QString en;
        QString group;
        QString scope;   // global | window | ui
        QString keys;    // 默认键位（Ctrl 写法，macOS 自动替换为 Meta）
    };

    using Invoker = std::function<void(const QString &id, const QVariant &value)>;

    explicit ActionRegistry(QObject *parent = nullptr);

    void setInvoker(Invoker invoker) { m_invoker = std::move(invoker); }

    int count() const;
    static const QVector<Action> &actionTable();

    Q_INVOKABLE QVariantList actions() const;
    Q_INVOKABLE QVariantList conflicts() const;
    Q_INVOKABLE QString keysFor(const QString &id, const QString &platform) const;
    Q_INVOKABLE QString actionForKey(int key, int modifiers,
                                     const QString &platform = currentPlatform()) const;
    Q_INVOKABLE bool handleKey(int key, int modifiers);
    Q_INVOKABLE bool dispatch(const QString &id, const QVariant &value = QVariant());
    Q_INVOKABLE bool has(const QString &id) const;

    static QString currentPlatform(); // windows | macos | linux
    static QString keyToken(int key); // 键码 → "A"/"Space"/"F11" 等
    static bool parseKeySpec(const QString &spec, int *key, int *modifiers);

signals:
    void dispatched(const QString &id, const QVariant &value);

private:
    void buildBindings();

    Invoker m_invoker;
    std::map<QString, std::map<QString, QString>> m_bindings; // platform -> keySpec -> actionId
};

} // namespace palune
