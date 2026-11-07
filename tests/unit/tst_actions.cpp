#include <QtTest>

#include <QSignalSpy>

#include "ui/ActionRegistry.h"

using namespace palune;

class TestActions : public QObject
{
    Q_OBJECT

private slots:
    void tableHasFourtyUniqueActions()
    {
        ActionRegistry registry;
        QCOMPARE(registry.count(), 40);
        QStringList ids;
        for (const QVariant &item : registry.actions()) {
            const QVariantMap entry = item.toMap();
            const QString id = entry.value(QStringLiteral("id")).toString();
            QVERIFY(!id.isEmpty());
            ids << id;
            QVERIFY(!entry.value(QStringLiteral("zh")).toString().isEmpty());
            QVERIFY(!entry.value(QStringLiteral("en")).toString().isEmpty());
            const QVariantMap keys = entry.value(QStringLiteral("keys")).toMap();
            QCOMPARE(keys.size(), 3);
            QVERIFY(keys.contains(QStringLiteral("windows")));
            QVERIFY(keys.contains(QStringLiteral("macos")));
            QVERIFY(keys.contains(QStringLiteral("linux")));
        }
        QCOMPARE(ids.size(), 40);
        QCOMPARE(ids.removeDuplicates(), 0);
    }

    void bindingsHaveNoConflicts()
    {
        ActionRegistry registry;
        QCOMPARE(registry.conflicts().size(), 0);
    }

    void platformKeySpecRewritesCommandModifier()
    {
        ActionRegistry registry;
        QCOMPARE(registry.keysFor(QStringLiteral("openFile"), QStringLiteral("windows")),
                 QStringLiteral("Ctrl+O"));
        QCOMPARE(registry.keysFor(QStringLiteral("openFile"), QStringLiteral("linux")),
                 QStringLiteral("Ctrl+O"));
        QCOMPARE(registry.keysFor(QStringLiteral("openFile"), QStringLiteral("macos")),
                 QStringLiteral("Meta+O"));
        QCOMPARE(registry.keysFor(QStringLiteral("exit"), QStringLiteral("macos")),
                 QStringLiteral("Meta+Q"));
        QVERIFY(registry.keysFor(QStringLiteral("doesNotExist"), QStringLiteral("windows")).isEmpty());
    }

    void everyActionResolvesFromItsKeySpec()
    {
        ActionRegistry registry;
        const QString platform = ActionRegistry::currentPlatform();
        for (const QVariant &item : registry.actions()) {
            const QVariantMap entry = item.toMap();
            const QString id = entry.value(QStringLiteral("id")).toString();
            const QString spec = entry.value(QStringLiteral("keys")).toMap()
                                     .value(platform).toString();
            int key = 0;
            int modifiers = 0;
            QVERIFY2(ActionRegistry::parseKeySpec(spec, &key, &modifiers), qPrintable(spec));
            QCOMPARE(registry.actionForKey(key, modifiers, platform), id);
        }
    }

    void unknownKeysAndPlatformsResolveToEmpty()
    {
        ActionRegistry registry;
        QVERIFY(registry.actionForKey(Qt::Key_unknown, Qt::NoModifier).isEmpty());
        QVERIFY(registry.actionForKey(Qt::Key_Space, Qt::NoModifier,
                                      QStringLiteral("beos")).isEmpty());
        QVERIFY(registry.actionForKey(Qt::Key_Return, Qt::NoModifier).isEmpty());
        // macOS 只认 Meta，Ctrl 组合不再命中。
        QVERIFY(registry.actionForKey(Qt::Key_O, Qt::ControlModifier,
                                      QStringLiteral("macos")).isEmpty());
        QCOMPARE(registry.actionForKey(Qt::Key_O, Qt::MetaModifier, QStringLiteral("macos")),
                 QStringLiteral("openFile"));
        QCOMPARE(registry.actionForKey(Qt::Key_Right, Qt::ShiftModifier),
                 QStringLiteral("seekForward60"));
        QCOMPARE(registry.actionForKey(Qt::Key_F11, Qt::NoModifier),
                 QStringLiteral("toggleFullscreen"));
        QCOMPARE(registry.actionForKey(Qt::Key_BracketLeft, Qt::NoModifier),
                 QStringLiteral("speedDown"));
    }

    void keyTokenCoversNamedAndPunctuationKeys()
    {
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Z), QStringLiteral("Z"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_7), QStringLiteral("7"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_F1), QStringLiteral("F1"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_F12), QStringLiteral("F12"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Space), QStringLiteral("Space"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Escape), QStringLiteral("Escape"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Tab), QStringLiteral("Tab"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Backspace), QStringLiteral("Backspace"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_PageUp), QStringLiteral("PageUp"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_PageDown), QStringLiteral("PageDown"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Period), QStringLiteral("."));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Comma), QStringLiteral(","));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Semicolon), QStringLiteral(";"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Apostrophe), QStringLiteral("'"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Slash), QStringLiteral("/"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Backslash), QStringLiteral("\\"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Equal), QStringLiteral("="));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Minus), QStringLiteral("-"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Plus), QStringLiteral("+"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Less), QStringLiteral("<"));
        QCOMPARE(ActionRegistry::keyToken(Qt::Key_Greater), QStringLiteral(">"));
        QVERIFY(ActionRegistry::keyToken(Qt::Key_unknown).isEmpty());
    }

    void parseKeySpecHandlesModifiersAndRejectsGarbage()
    {
        int key = 0;
        int modifiers = 0;
        QVERIFY(ActionRegistry::parseKeySpec(QStringLiteral("Ctrl+O"), &key, &modifiers));
        QCOMPARE(key, static_cast<int>(Qt::Key_O));
        QCOMPARE(modifiers, static_cast<int>(Qt::ControlModifier));

        QVERIFY(ActionRegistry::parseKeySpec(QStringLiteral("Shift+="), &key, &modifiers));
        QCOMPARE(key, static_cast<int>(Qt::Key_Equal));
        QCOMPARE(modifiers, static_cast<int>(Qt::ShiftModifier));

        QVERIFY(ActionRegistry::parseKeySpec(QStringLiteral("F11"), &key, &modifiers));
        QCOMPARE(key, static_cast<int>(Qt::Key_F11));
        QCOMPARE(modifiers, 0);

        QVERIFY(ActionRegistry::parseKeySpec(QStringLiteral("Alt+V"), &key, &modifiers));
        QCOMPARE(key, static_cast<int>(Qt::Key_V));
        QCOMPARE(modifiers, static_cast<int>(Qt::AltModifier));

        QVERIFY(!ActionRegistry::parseKeySpec(QString(), &key, &modifiers));
        QVERIFY(!ActionRegistry::parseKeySpec(QStringLiteral("Hyper+O"), &key, &modifiers));
        QVERIFY(!ActionRegistry::parseKeySpec(QStringLiteral("Ctrl+Nope"), &key, &modifiers));
    }

    void keyTokenAndParseKeySpecRoundTrip()
    {
        // 覆盖 keyToken / parseKeySpec 的全部命名键与标点分支。
        const QVector<int> keys = {
            Qt::Key_A, Qt::Key_Z, Qt::Key_0, Qt::Key_9,
            Qt::Key_F1, Qt::Key_F12, Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Escape, Qt::Key_Tab, Qt::Key_Backspace, Qt::Key_Up, Qt::Key_Down,
            Qt::Key_Left, Qt::Key_Right, Qt::Key_Home, Qt::Key_End, Qt::Key_PageUp,
            Qt::Key_PageDown, Qt::Key_Period, Qt::Key_Comma, Qt::Key_Semicolon,
            Qt::Key_Apostrophe, Qt::Key_Slash, Qt::Key_Backslash, Qt::Key_BracketLeft,
            Qt::Key_BracketRight, Qt::Key_Equal, Qt::Key_Minus, Qt::Key_Plus,
            Qt::Key_Less, Qt::Key_Greater,
        };
        for (int expected : keys) {
            const QString token = ActionRegistry::keyToken(expected);
            QVERIFY2(!token.isEmpty(), qPrintable(QString::number(expected)));
            int key = 0;
            int modifiers = 99;
            QVERIFY2(ActionRegistry::parseKeySpec(token, &key, &modifiers), qPrintable(token));
            QCOMPARE(key, expected);
            QCOMPARE(modifiers, 0);
        }
    }

    void hasRecognisesRegisteredIds()
    {
        ActionRegistry registry;
        QVERIFY(registry.has(QStringLiteral("playPause")));
        QVERIFY(registry.has(QStringLiteral("exit")));
        QVERIFY(!registry.has(QStringLiteral("nope")));
        QVERIFY(!registry.has(QString()));
    }

    void dispatchEmitsSignalAndInvokesCallback()
    {
        ActionRegistry registry;
        QSignalSpy spy(&registry, &ActionRegistry::dispatched);
        QVERIFY(registry.dispatch(QStringLiteral("playPause")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("playPause"));

        QVariant received;
        bool called = false;
        registry.setInvoker([&](const QString &id, const QVariant &value) {
            called = true;
            received = value;
            QCOMPARE(id, QStringLiteral("volumeUp"));
        });
        QVERIFY(registry.dispatch(QStringLiteral("volumeUp"), 12));
        QVERIFY(called);
        QCOMPARE(received.toInt(), 12);
        QCOMPARE(spy.count(), 2);

        QVERIFY(!registry.dispatch(QStringLiteral("nope")));
        QCOMPARE(spy.count(), 2);
    }

    void handleKeyDispatchesRegisteredShortcut()
    {
        ActionRegistry registry;
        QString invoked;
        registry.setInvoker([&](const QString &id, const QVariant &) { invoked = id; });
        QVERIFY(registry.handleKey(Qt::Key_Space, Qt::NoModifier));
        QCOMPARE(invoked, QStringLiteral("playPause"));
        QVERIFY(registry.handleKey(Qt::Key_S, Qt::NoModifier));
        QCOMPARE(invoked, QStringLiteral("stop"));
        QVERIFY(!registry.handleKey(Qt::Key_Return, Qt::NoModifier));
        QCOMPARE(invoked, QStringLiteral("stop"));
    }
};

QTEST_GUILESS_MAIN(TestActions)
#include "tst_actions.moc"
