#include "ui/ActionRegistry.h"

#include <QMap>
#include <QStringList>
#include <QVector>

namespace palune {

namespace {

QString modifierPrefix(int modifiers)
{
    QStringList parts;
    if (modifiers & Qt::ControlModifier)
        parts << QStringLiteral("Ctrl");
    if (modifiers & Qt::ShiftModifier)
        parts << QStringLiteral("Shift");
    if (modifiers & Qt::AltModifier)
        parts << QStringLiteral("Alt");
    if (modifiers & Qt::MetaModifier)
        parts << QStringLiteral("Meta");
    return parts.join(QLatin1Char('+'));
}

QString platformKeySpec(const QString &keys, const QString &platform)
{
    // 默认键位统一用 Ctrl 书写；macOS 的等价修饰键是 Meta（Command）。
    if (platform == QLatin1String("macos"))
        return QString(keys).replace(QLatin1String("Ctrl+"), QLatin1String("Meta+"));
    return keys;
}

} // namespace

const QVector<ActionRegistry::Action> &ActionRegistry::actionTable()
{
    static const QVector<Action> table = {
        // 传输（14）
        {QStringLiteral("playPause"), QStringLiteral("播放/暂停"), QStringLiteral("Play/Pause"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Space")},
        {QStringLiteral("stop"), QStringLiteral("停止"), QStringLiteral("Stop"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("S")},
        {QStringLiteral("seekForward10"), QStringLiteral("快进 10 秒"), QStringLiteral("Seek forward 10s"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Right")},
        {QStringLiteral("seekBack10"), QStringLiteral("后退 10 秒"), QStringLiteral("Seek back 10s"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Left")},
        {QStringLiteral("seekForward60"), QStringLiteral("快进 60 秒"), QStringLiteral("Seek forward 60s"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Shift+Right")},
        {QStringLiteral("seekBack60"), QStringLiteral("后退 60 秒"), QStringLiteral("Seek back 60s"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Shift+Left")},
        {QStringLiteral("seekForward1"), QStringLiteral("快进 1 秒"), QStringLiteral("Seek forward 1s"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral(".")},
        {QStringLiteral("seekBack1"), QStringLiteral("后退 1 秒"), QStringLiteral("Seek back 1s"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral(",")},
        {QStringLiteral("frameStepForward"), QStringLiteral("下一帧"), QStringLiteral("Step frame forward"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("F")},
        {QStringLiteral("frameStepBack"), QStringLiteral("上一帧"), QStringLiteral("Step frame back"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Shift+F")},
        {QStringLiteral("nextEpisode"), QStringLiteral("下一集"), QStringLiteral("Next episode"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("N")},
        {QStringLiteral("prevEpisode"), QStringLiteral("上一集"), QStringLiteral("Previous episode"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("P")},
        {QStringLiteral("seekToStart"), QStringLiteral("回到开头"), QStringLiteral("Go to start"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("Home")},
        {QStringLiteral("seekToEnd"), QStringLiteral("跳到结尾"), QStringLiteral("Go to end"), QStringLiteral("transport"), QStringLiteral("global"), QStringLiteral("End")},
        // 音量与轨道（5）
        {QStringLiteral("volumeUp"), QStringLiteral("音量 +5"), QStringLiteral("Volume +5"), QStringLiteral("audio"), QStringLiteral("global"), QStringLiteral("Up")},
        {QStringLiteral("volumeDown"), QStringLiteral("音量 -5"), QStringLiteral("Volume -5"), QStringLiteral("audio"), QStringLiteral("global"), QStringLiteral("Down")},
        {QStringLiteral("toggleMute"), QStringLiteral("静音"), QStringLiteral("Mute"), QStringLiteral("audio"), QStringLiteral("global"), QStringLiteral("M")},
        {QStringLiteral("cycleAudio"), QStringLiteral("切换音轨"), QStringLiteral("Cycle audio track"), QStringLiteral("audio"), QStringLiteral("global"), QStringLiteral("A")},
        {QStringLiteral("cycleSubtitle"), QStringLiteral("切换字幕"), QStringLiteral("Cycle subtitle track"), QStringLiteral("subtitle"), QStringLiteral("global"), QStringLiteral("V")},
        // 字幕（3）
        {QStringLiteral("toggleSubtitle"), QStringLiteral("显示/隐藏字幕"), QStringLiteral("Toggle subtitles"), QStringLiteral("subtitle"), QStringLiteral("global"), QStringLiteral("Shift+V")},
        {QStringLiteral("subtitleDelayUp"), QStringLiteral("字幕提前 0.1s"), QStringLiteral("Subtitle delay +0.1s"), QStringLiteral("subtitle"), QStringLiteral("global"), QStringLiteral("Shift+Z")},
        {QStringLiteral("subtitleDelayDown"), QStringLiteral("字幕延后 0.1s"), QStringLiteral("Subtitle delay -0.1s"), QStringLiteral("subtitle"), QStringLiteral("global"), QStringLiteral("Shift+X")},
        // 速度（3）
        {QStringLiteral("speedUp"), QStringLiteral("加速"), QStringLiteral("Speed up"), QStringLiteral("speed"), QStringLiteral("global"), QStringLiteral("]")},
        {QStringLiteral("speedDown"), QStringLiteral("减速"), QStringLiteral("Speed down"), QStringLiteral("speed"), QStringLiteral("global"), QStringLiteral("[")},
        {QStringLiteral("speedReset"), QStringLiteral("恢复 1x"), QStringLiteral("Reset speed"), QStringLiteral("speed"), QStringLiteral("global"), QStringLiteral("0")},
        // 主题与窗口（9）
        {QStringLiteral("themeA"), QStringLiteral("主题 A"), QStringLiteral("Theme A"), QStringLiteral("theme"), QStringLiteral("ui"), QStringLiteral("1")},
        {QStringLiteral("themeB"), QStringLiteral("主题 B"), QStringLiteral("Theme B"), QStringLiteral("theme"), QStringLiteral("ui"), QStringLiteral("2")},
        {QStringLiteral("themeC"), QStringLiteral("主题 C"), QStringLiteral("Theme C"), QStringLiteral("theme"), QStringLiteral("ui"), QStringLiteral("3")},
        {QStringLiteral("cycleTheme"), QStringLiteral("切换布局"), QStringLiteral("Cycle layout"), QStringLiteral("theme"), QStringLiteral("ui"), QStringLiteral("T")},
        {QStringLiteral("toggleChrome"), QStringLiteral("显示/隐藏控件"), QStringLiteral("Toggle chrome"), QStringLiteral("window"), QStringLiteral("ui"), QStringLiteral("C")},
        {QStringLiteral("toggleFullscreen"), QStringLiteral("全屏"), QStringLiteral("Fullscreen"), QStringLiteral("window"), QStringLiteral("window"), QStringLiteral("F11")},
        {QStringLiteral("toggleMaximize"), QStringLiteral("最大化"), QStringLiteral("Maximize"), QStringLiteral("window"), QStringLiteral("window"), QStringLiteral("F10")},
        {QStringLiteral("windowScaleUp"), QStringLiteral("窗口放大"), QStringLiteral("Scale window up"), QStringLiteral("window"), QStringLiteral("window"), QStringLiteral("Shift+=")},
        {QStringLiteral("windowScaleDown"), QStringLiteral("窗口缩小"), QStringLiteral("Scale window down"), QStringLiteral("window"), QStringLiteral("window"), QStringLiteral("Shift+-")},
        // 打开与面板（6）
        {QStringLiteral("openFile"), QStringLiteral("打开文件"), QStringLiteral("Open file"), QStringLiteral("file"), QStringLiteral("ui"), QStringLiteral("Ctrl+O")},
        {QStringLiteral("openFolder"), QStringLiteral("打开文件夹"), QStringLiteral("Open folder"), QStringLiteral("file"), QStringLiteral("ui"), QStringLiteral("Shift+O")},
        {QStringLiteral("openRecent"), QStringLiteral("最近打开"), QStringLiteral("Recent files"), QStringLiteral("file"), QStringLiteral("ui"), QStringLiteral("Ctrl+R")},
        {QStringLiteral("openSettings"), QStringLiteral("设置"), QStringLiteral("Settings"), QStringLiteral("ui"), QStringLiteral("ui"), QStringLiteral("Ctrl+,")},
        {QStringLiteral("openLibrary"), QStringLiteral("媒体库"), QStringLiteral("Library"), QStringLiteral("ui"), QStringLiteral("ui"), QStringLiteral("Ctrl+L")},
        {QStringLiteral("exit"), QStringLiteral("退出"), QStringLiteral("Exit"), QStringLiteral("ui"), QStringLiteral("ui"), QStringLiteral("Ctrl+Q")},
    };
    return table;
}

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
{
    buildBindings();
}

int ActionRegistry::count() const
{
    return actionTable().size();
}

QString ActionRegistry::currentPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QVariantList ActionRegistry::actions() const
{
    QVariantList list;
    for (const Action &action : actionTable()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), action.id);
        entry.insert(QStringLiteral("zh"), action.zh);
        entry.insert(QStringLiteral("en"), action.en);
        entry.insert(QStringLiteral("group"), action.group);
        entry.insert(QStringLiteral("scope"), action.scope);
        QVariantMap keys;
        for (const QString &platform : {QStringLiteral("windows"), QStringLiteral("macos"),
                                        QStringLiteral("linux")})
            keys.insert(platform, platformKeySpec(action.keys, platform));
        entry.insert(QStringLiteral("keys"), keys);
        list.append(entry);
    }
    return list;
}

QVariantList ActionRegistry::conflicts() const
{
    QVariantList result;
    for (const QString &platform : {QStringLiteral("windows"), QStringLiteral("macos"),
                                    QStringLiteral("linux")}) {
        QMap<QString, QString> seen; // scope|key -> id
        for (const Action &action : actionTable()) {
            const QString key = platformKeySpec(action.keys, platform);
            const QString slot = action.scope + QLatin1Char('|') + key;
            const QString previous = seen.value(slot);
            if (!previous.isEmpty()) {
                QVariantMap conflict;
                conflict.insert(QStringLiteral("platform"), platform);
                conflict.insert(QStringLiteral("keys"), key);
                conflict.insert(QStringLiteral("scope"), action.scope);
                conflict.insert(QStringLiteral("first"), previous);
                conflict.insert(QStringLiteral("second"), action.id);
                result.append(conflict);
            } else {
                seen.insert(slot, action.id);
            }
        }
    }
    return result;
}

QString ActionRegistry::keysFor(const QString &id, const QString &platform) const
{
    for (const Action &action : actionTable()) {
        if (action.id == id)
            return platformKeySpec(action.keys, platform);
    }
    return {};
}

void ActionRegistry::buildBindings()
{
    for (const QString &platform : {QStringLiteral("windows"), QStringLiteral("macos"),
                                    QStringLiteral("linux")}) {
        for (const Action &action : actionTable())
            m_bindings[platform][platformKeySpec(action.keys, platform)] = action.id;
    }
}

QString ActionRegistry::keyToken(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar(key));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar(key));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);

    switch (key) {
    case Qt::Key_Space: return QStringLiteral("Space");
    case Qt::Key_Return: return QStringLiteral("Return");
    case Qt::Key_Enter: return QStringLiteral("Enter");
    case Qt::Key_Escape: return QStringLiteral("Escape");
    case Qt::Key_Tab: return QStringLiteral("Tab");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Up: return QStringLiteral("Up");
    case Qt::Key_Down: return QStringLiteral("Down");
    case Qt::Key_Left: return QStringLiteral("Left");
    case Qt::Key_Right: return QStringLiteral("Right");
    case Qt::Key_Home: return QStringLiteral("Home");
    case Qt::Key_End: return QStringLiteral("End");
    case Qt::Key_PageUp: return QStringLiteral("PageUp");
    case Qt::Key_PageDown: return QStringLiteral("PageDown");
    case Qt::Key_Period: return QStringLiteral(".");
    case Qt::Key_Comma: return QStringLiteral(",");
    case Qt::Key_Semicolon: return QStringLiteral(";");
    case Qt::Key_Apostrophe: return QStringLiteral("'");
    case Qt::Key_Slash: return QStringLiteral("/");
    case Qt::Key_Backslash: return QStringLiteral("\\");
    case Qt::Key_BracketLeft: return QStringLiteral("[");
    case Qt::Key_BracketRight: return QStringLiteral("]");
    case Qt::Key_Equal: return QStringLiteral("=");
    case Qt::Key_Minus: return QStringLiteral("-");
    case Qt::Key_Plus: return QStringLiteral("+");
    case Qt::Key_Less: return QStringLiteral("<");
    case Qt::Key_Greater: return QStringLiteral(">");
    default: return {};
    }
}

bool ActionRegistry::parseKeySpec(const QString &spec, int *key, int *modifiers)
{
    QStringList parts = spec.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    int mods = 0;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString part = parts.at(i);
        if (part == QLatin1String("Ctrl"))
            mods |= Qt::ControlModifier;
        else if (part == QLatin1String("Shift"))
            mods |= Qt::ShiftModifier;
        else if (part == QLatin1String("Alt"))
            mods |= Qt::AltModifier;
        else if (part == QLatin1String("Meta"))
            mods |= Qt::MetaModifier;
        else
            return false;
    }
    const QString token = parts.last();
    int code = 0;
    if (token.size() == 1 && token.at(0).isLetter())
        code = Qt::Key_A + (token.at(0).toUpper().unicode() - 'A');
    else if (token.size() == 1 && token.at(0).isDigit())
        code = Qt::Key_0 + (token.at(0).unicode() - '0');
    else if (token.startsWith(QLatin1Char('F')) && token.size() <= 4)
        code = Qt::Key_F1 + token.mid(1).toInt() - 1;
    else if (token == QLatin1String("Space"))
        code = Qt::Key_Space;
    else if (token == QLatin1String("Return"))
        code = Qt::Key_Return;
    else if (token == QLatin1String("Enter"))
        code = Qt::Key_Enter;
    else if (token == QLatin1String("Escape"))
        code = Qt::Key_Escape;
    else if (token == QLatin1String("Tab"))
        code = Qt::Key_Tab;
    else if (token == QLatin1String("Backspace"))
        code = Qt::Key_Backspace;
    else if (token == QLatin1String("Up"))
        code = Qt::Key_Up;
    else if (token == QLatin1String("Down"))
        code = Qt::Key_Down;
    else if (token == QLatin1String("Left"))
        code = Qt::Key_Left;
    else if (token == QLatin1String("Right"))
        code = Qt::Key_Right;
    else if (token == QLatin1String("Home"))
        code = Qt::Key_Home;
    else if (token == QLatin1String("End"))
        code = Qt::Key_End;
    else if (token == QLatin1String("."))
        code = Qt::Key_Period;
    else if (token == QLatin1String(","))
        code = Qt::Key_Comma;
    else if (token == QLatin1String(";"))
        code = Qt::Key_Semicolon;
    else if (token == QLatin1String("'"))
        code = Qt::Key_Apostrophe;
    else if (token == QLatin1String("/"))
        code = Qt::Key_Slash;
    else if (token == QLatin1String("\\"))
        code = Qt::Key_Backslash;
    else if (token == QLatin1String("["))
        code = Qt::Key_BracketLeft;
    else if (token == QLatin1String("]"))
        code = Qt::Key_BracketRight;
    else if (token == QLatin1String("="))
        code = Qt::Key_Equal;
    else if (token == QLatin1String("-"))
        code = Qt::Key_Minus;
    else if (token == QLatin1String("+"))
        code = Qt::Key_Plus;
    else if (token == QLatin1String("<"))
        code = Qt::Key_Less;
    else if (token == QLatin1String(">"))
        code = Qt::Key_Greater;
    else
        return false;

    if (key)
        *key = code;
    if (modifiers)
        *modifiers = mods;
    return true;
}

QString ActionRegistry::actionForKey(int key, int modifiers, const QString &platform) const
{
    const QString mods = modifierPrefix(modifiers);
    const QString token = keyToken(key);
    if (token.isEmpty())
        return {};
    const QString spec = mods.isEmpty() ? token : mods + QLatin1Char('+') + token;
    const auto it = m_bindings.find(platform);
    if (it == m_bindings.end())
        return {};
    return it->second.count(spec) ? QString(it->second.at(spec)) : QString();
}

bool ActionRegistry::has(const QString &id) const
{
    for (const Action &action : actionTable()) {
        if (action.id == id)
            return true;
    }
    return false;
}

bool ActionRegistry::handleKey(int key, int modifiers)
{
    const QString id = actionForKey(key, modifiers, currentPlatform());
    if (id.isEmpty())
        return false;
    return dispatch(id, QVariant());
}

bool ActionRegistry::dispatch(const QString &id, const QVariant &value)
{
    if (!has(id))
        return false;
    emit dispatched(id, value);
    if (m_invoker)
        m_invoker(id, value);
    return true;
}

} // namespace palune
