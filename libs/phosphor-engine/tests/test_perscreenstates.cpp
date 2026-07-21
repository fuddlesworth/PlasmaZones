// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/PerScreenStates.h>

#include <memory>
#include <vector>

#include <QTest>

using PhosphorEngine::PerScreenStates;
using PhosphorEngine::PlacementStateKey;

/// Minimal IPlacementState for exercising PerScreenStates. Owns nothing; the
/// test manages lifetime (mirrors the engine's Qt-parent ownership in prod).
class FakeState : public PhosphorEngine::IPlacementState
{
public:
    explicit FakeState(QString screen)
        : m_screen(std::move(screen))
    {
    }

    QString screenId() const override
    {
        return m_screen;
    }
    int windowCount() const override
    {
        return m_windows.size();
    }
    QStringList managedWindows() const override
    {
        return m_windows;
    }
    bool containsWindow(const QString& windowId) const override
    {
        return m_windows.contains(windowId);
    }
    bool isFloating(const QString&) const override
    {
        return false;
    }
    QStringList floatingWindows() const override
    {
        return {};
    }
    QString placementIdForWindow(const QString&) const override
    {
        return {};
    }
    QStringList m_windows;

private:
    QString m_screen;
};

static PlacementStateKey key(const QString& screen, int desktop = 1, const QString& activity = QString())
{
    return PlacementStateKey{screen, desktop, activity};
}

class TestPerScreenStates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void forKey_lazyCreateAndFactoryNull();
    void reverseMap_basics();
    void forWindow_resolvesStateAndKey();
    void migrate_movesReverseMapOnly();
    void rekeyWindows_rewritesMatching();
    void removeStatesIf_lockstepWithHook();
    void removeWindowsIf_byPredicate();

private:
    std::vector<std::unique_ptr<FakeState>> m_owned;

    FakeState* makeState(const QString& screen)
    {
        m_owned.push_back(std::make_unique<FakeState>(screen));
        return m_owned.back().get();
    }
};

void TestPerScreenStates::init()
{
    m_owned.clear();
}

void TestPerScreenStates::cleanup()
{
    m_owned.clear();
}

void TestPerScreenStates::forKey_lazyCreateAndFactoryNull()
{
    PerScreenStates<FakeState> states;
    const auto k = key(QStringLiteral("S1"), 2);

    QVERIFY(!states.containsKey(k));
    QCOMPARE(states.stateForKey(k), nullptr);

    int factoryCalls = 0;
    FakeState* created = states.forKey(k, [&] {
        ++factoryCalls;
        return makeState(QStringLiteral("S1"));
    });
    QVERIFY(created != nullptr);
    QCOMPARE(factoryCalls, 1);
    QVERIFY(states.containsKey(k));
    QCOMPARE(states.stateCount(), 1);

    // Second call hits the existing state — factory NOT invoked again.
    FakeState* again = states.forKey(k, [&] {
        ++factoryCalls;
        return makeState(QStringLiteral("S1"));
    });
    QCOMPARE(again, created);
    QCOMPARE(factoryCalls, 1);

    // A factory returning nullptr (engine rejected the key) inserts nothing.
    const auto bad = key(QStringLiteral("bogus"));
    FakeState* none = states.forKey(bad, [] {
        return static_cast<FakeState*>(nullptr);
    });
    QCOMPARE(none, nullptr);
    QVERIFY(!states.containsKey(bad));
    QCOMPARE(states.stateCount(), 1);
}

void TestPerScreenStates::reverseMap_basics()
{
    PerScreenStates<FakeState> states;
    const auto k = key(QStringLiteral("S1"), 3);

    QVERIFY(!states.hasWindow(QStringLiteral("w1")));
    QVERIFY(!states.windowKey(QStringLiteral("w1")).has_value());
    QVERIFY(states.keyForWindow(QStringLiteral("w1")).screenId.isEmpty());

    states.setKeyForWindow(QStringLiteral("w1"), k);
    QVERIFY(states.hasWindow(QStringLiteral("w1")));
    QCOMPARE(states.keyForWindow(QStringLiteral("w1")), k);
    QVERIFY(states.windowKey(QStringLiteral("w1")).has_value());
    QCOMPARE(states.windowKey(QStringLiteral("w1")).value(), k);

    QCOMPARE(states.takeWindow(QStringLiteral("w1")), k);
    QVERIFY(!states.hasWindow(QStringLiteral("w1")));

    states.setKeyForWindow(QStringLiteral("w2"), k);
    states.removeWindow(QStringLiteral("w2"));
    QVERIFY(!states.hasWindow(QStringLiteral("w2")));
}

void TestPerScreenStates::forWindow_resolvesStateAndKey()
{
    PerScreenStates<FakeState> states;
    const auto k = key(QStringLiteral("S1"), 4);
    FakeState* s = states.forKey(k, [&] {
        return makeState(QStringLiteral("S1"));
    });
    states.setKeyForWindow(QStringLiteral("w1"), k);

    PlacementStateKey out;
    QCOMPARE(states.forWindow(QStringLiteral("w1"), &out), s);
    QCOMPARE(out, k);

    QCOMPARE(states.forWindow(QStringLiteral("missing")), nullptr);
}

void TestPerScreenStates::migrate_movesReverseMapOnly()
{
    PerScreenStates<FakeState> states;
    const auto oldKey = key(QStringLiteral("S1"), 1);
    const auto newKey = key(QStringLiteral("S2"), 1);
    states.setKeyForWindow(QStringLiteral("w1"), oldKey);

    states.migrate(QStringLiteral("w1"), oldKey, newKey);
    QCOMPARE(states.keyForWindow(QStringLiteral("w1")), newKey);
}

void TestPerScreenStates::rekeyWindows_rewritesMatching()
{
    PerScreenStates<FakeState> states;
    const auto oldKey = key(QStringLiteral("S1"), 1);
    const auto newKey = key(QStringLiteral("S1"), 5);
    const auto otherKey = key(QStringLiteral("S2"), 1);
    states.setKeyForWindow(QStringLiteral("a"), oldKey);
    states.setKeyForWindow(QStringLiteral("b"), oldKey);
    states.setKeyForWindow(QStringLiteral("c"), otherKey);

    states.rekeyWindows(oldKey, newKey);
    QCOMPARE(states.keyForWindow(QStringLiteral("a")), newKey);
    QCOMPARE(states.keyForWindow(QStringLiteral("b")), newKey);
    QCOMPARE(states.keyForWindow(QStringLiteral("c")), otherKey);
}

void TestPerScreenStates::removeStatesIf_lockstepWithHook()
{
    PerScreenStates<FakeState> states;
    const auto k1 = key(QStringLiteral("S1"), 1);
    const auto k2 = key(QStringLiteral("S1"), 2);
    states.forKey(k1, [&] {
        return makeState(QStringLiteral("S1"));
    });
    states.forKey(k2, [&] {
        return makeState(QStringLiteral("S1"));
    });
    QCOMPARE(states.stateCount(), 2);

    QList<int> removedDesktops;
    states.removeStatesIf(
        [](const PlacementStateKey& k, FakeState*) {
            return k.desktop == 2;
        },
        [&](const PlacementStateKey& k, FakeState*) {
            removedDesktops.append(k.desktop);
        });

    QCOMPARE(states.stateCount(), 1);
    QVERIFY(states.containsKey(k1));
    QVERIFY(!states.containsKey(k2));
    QCOMPARE(removedDesktops, QList<int>{2});
}

void TestPerScreenStates::removeWindowsIf_byPredicate()
{
    PerScreenStates<FakeState> states;
    states.setKeyForWindow(QStringLiteral("keep"), key(QStringLiteral("S1"), 1));
    states.setKeyForWindow(QStringLiteral("drop"), key(QStringLiteral("S1"), 9));

    states.removeWindowsIf([](const QString&, const PlacementStateKey& k) {
        return k.desktop == 9;
    });
    QVERIFY(states.hasWindow(QStringLiteral("keep")));
    QVERIFY(!states.hasWindow(QStringLiteral("drop")));
}

QTEST_MAIN(TestPerScreenStates)
#include "test_perscreenstates.moc"
