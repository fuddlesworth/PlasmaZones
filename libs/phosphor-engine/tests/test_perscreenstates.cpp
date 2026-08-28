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
    void reapDesktop_sweepsBothMaps();
    void renumberDesktops_shiftsKeysCollisionFree();
    void renumberDesktops_skipPredicateExemptsSentinels();
    void renumberDesktopKeyedHash_auxMaps();
    void reapDesktop_nullHookAndSkipPredicate();
    void renumberDesktops_defaultSkipShiftsSentinels();
    void renumberDesktops_rejectsTargetsBelowOne();

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

void TestPerScreenStates::reapDesktop_sweepsBothMaps()
{
    PerScreenStates<FakeState> states;
    states.insertState(key(QStringLiteral("S1"), 2), makeState(QStringLiteral("S1")));
    states.insertState(key(QStringLiteral("S1"), 3), makeState(QStringLiteral("S1")));
    states.setKeyForWindow(QStringLiteral("w2"), key(QStringLiteral("S1"), 2));
    states.setKeyForWindow(QStringLiteral("w3"), key(QStringLiteral("S1"), 3));

    QStringList torn;
    states.reapDesktop(2, [&torn](const PlacementStateKey&, FakeState*) {
        torn.append(QStringLiteral("torn"));
    });

    QCOMPARE(torn.size(), 1);
    QVERIFY(!states.containsKey(key(QStringLiteral("S1"), 2)));
    QVERIFY(states.containsKey(key(QStringLiteral("S1"), 3)));
    QVERIFY(!states.hasWindow(QStringLiteral("w2")));
    QVERIFY(states.hasWindow(QStringLiteral("w3")));
}

void TestPerScreenStates::renumberDesktops_shiftsKeysCollisionFree()
{
    PerScreenStates<FakeState> states;
    // Remove-desktop-2 shape: 3→2, 4→3 — the classic shift where a naive
    // in-place rewrite would collide (3 lands on the not-yet-moved 2 slot if
    // 2 had survived; here 3 and 4 swap through each other's numbers).
    FakeState* s3 = makeState(QStringLiteral("S1"));
    FakeState* s4 = makeState(QStringLiteral("S1"));
    states.insertState(key(QStringLiteral("S1"), 3), s3);
    states.insertState(key(QStringLiteral("S1"), 4), s4);
    states.setKeyForWindow(QStringLiteral("w3"), key(QStringLiteral("S1"), 3));
    states.setKeyForWindow(QStringLiteral("w4"), key(QStringLiteral("S1"), 4));

    QHash<int, int> mapping;
    mapping.insert(3, 2);
    mapping.insert(4, 3);
    states.renumberDesktops(mapping);

    QCOMPARE(states.stateForKey(key(QStringLiteral("S1"), 2)), s3);
    QCOMPARE(states.stateForKey(key(QStringLiteral("S1"), 3)), s4);
    QVERIFY(!states.containsKey(key(QStringLiteral("S1"), 4)));
    QCOMPARE(states.keyForWindow(QStringLiteral("w3")).desktop, 2);
    QCOMPARE(states.keyForWindow(QStringLiteral("w4")).desktop, 3);
    // MUTATION GUARD: after the pass, no key carries a stale pre-shift int.
    for (auto it = states.states().constBegin(); it != states.states().constEnd(); ++it) {
        QVERIFY(it.key().desktop == 2 || it.key().desktop == 3);
    }
}

void TestPerScreenStates::renumberDesktops_skipPredicateExemptsSentinels()
{
    PerScreenStates<FakeState> states;
    // The snap engine's global holder: empty screenId is a sentinel key whose
    // desktop int is not a desktop context and must not shift.
    FakeState* global = makeState(QString());
    FakeState* normal = makeState(QStringLiteral("S1"));
    states.insertState(key(QString(), 2), global);
    states.insertState(key(QStringLiteral("S1"), 2), normal);

    QHash<int, int> mapping;
    mapping.insert(2, 1);
    states.renumberDesktops(mapping, [](const PlacementStateKey& k) {
        return k.screenId.isEmpty();
    });

    QCOMPARE(states.stateForKey(key(QString(), 2)), global);
    QCOMPARE(states.stateForKey(key(QStringLiteral("S1"), 1)), normal);
    QVERIFY(!states.containsKey(key(QStringLiteral("S1"), 2)));

    // The REVERSE map honours the same predicate: a window parked under the
    // sentinel key keeps its desktop int, or its entry stops naming the state
    // the forward map left in place.
    PerScreenStates<FakeState> withWindows;
    withWindows.setKeyForWindow(QStringLiteral("wGlobal"), key(QString(), 2));
    withWindows.setKeyForWindow(QStringLiteral("wScreen"), key(QStringLiteral("S1"), 2));
    withWindows.renumberDesktops(mapping, [](const PlacementStateKey& k) {
        return k.screenId.isEmpty();
    });
    QCOMPARE(withWindows.keyForWindow(QStringLiteral("wGlobal")).desktop, 2);
    QCOMPARE(withWindows.keyForWindow(QStringLiteral("wScreen")).desktop, 1);
}

void TestPerScreenStates::reapDesktop_nullHookAndSkipPredicate()
{
    PerScreenStates<FakeState> states;
    states.insertState(key(QString(), 2), makeState(QString()));
    states.insertState(key(QStringLiteral("S1"), 2), makeState(QStringLiteral("S1")));
    states.setKeyForWindow(QStringLiteral("wGlobal"), key(QString(), 2));
    states.setKeyForWindow(QStringLiteral("wScreen"), key(QStringLiteral("S1"), 2));

    // A null teardown hook is legal (a container with nothing to tear down),
    // and the skip predicate exempts the sentinel key from BOTH maps.
    states.reapDesktop(2, nullptr, [](const PlacementStateKey& k) {
        return k.screenId.isEmpty();
    });

    QVERIFY(states.containsKey(key(QString(), 2)));
    QVERIFY(!states.containsKey(key(QStringLiteral("S1"), 2)));
    QVERIFY(states.hasWindow(QStringLiteral("wGlobal")));
    QVERIFY(!states.hasWindow(QStringLiteral("wScreen")));
}

void TestPerScreenStates::renumberDesktops_defaultSkipShiftsSentinels()
{
    // With no predicate the sentinel key is NOT special — it shifts like any
    // other. An engine holding sentinel keys must pass its own skip; this
    // pins the default so a caller cannot assume protection it did not ask for.
    PerScreenStates<FakeState> states;
    FakeState* global = makeState(QString());
    states.insertState(key(QString(), 2), global);
    states.setKeyForWindow(QStringLiteral("wGlobal"), key(QString(), 2));

    QHash<int, int> mapping;
    mapping.insert(2, 1);
    states.renumberDesktops(mapping);

    QCOMPARE(states.stateForKey(key(QString(), 1)), global);
    QVERIFY(!states.containsKey(key(QString(), 2)));
    QCOMPARE(states.keyForWindow(QStringLiteral("wGlobal")).desktop, 1);
}

void TestPerScreenStates::renumberDesktops_rejectsTargetsBelowOne()
{
    // Desktops are 1-based; a mapping target below 1 poisons the WHOLE mapping,
    // which is then refused entirely. Rejecting only the offending entry would
    // strand desktop 3 on 3 while its sibling 4→3 moved on top of it, which is
    // the very collision the injectivity precondition rules out.
    PerScreenStates<FakeState> states;
    FakeState* s = makeState(QStringLiteral("S1"));
    FakeState* sibling = makeState(QStringLiteral("S1"));
    states.insertState(key(QStringLiteral("S1"), 3), s);
    states.insertState(key(QStringLiteral("S1"), 4), sibling);
    states.setKeyForWindow(QStringLiteral("w3"), key(QStringLiteral("S1"), 3));
    states.setKeyForWindow(QStringLiteral("w4"), key(QStringLiteral("S1"), 4));

    QHash<int, int> poisoned;
    poisoned.insert(3, 0);
    poisoned.insert(4, 3); // valid on its own, and must NOT be applied alone
    states.renumberDesktops(poisoned);
    QCOMPARE(states.stateForKey(key(QStringLiteral("S1"), 3)), s);
    QCOMPARE(states.stateForKey(key(QStringLiteral("S1"), 4)), sibling);
    QCOMPARE(states.keyForWindow(QStringLiteral("w3")).desktop, 3);
    QCOMPARE(states.keyForWindow(QStringLiteral("w4")).desktop, 4);

    // Same rule in the aux-map helper.
    QHash<PlacementStateKey, QString> aux;
    aux.insert(key(QStringLiteral("S1"), 3), QStringLiteral("a"));
    aux.insert(key(QStringLiteral("S1"), 4), QStringLiteral("b"));
    PhosphorEngine::renumberDesktopKeyedHash(aux, poisoned);
    QCOMPARE(aux.value(key(QStringLiteral("S1"), 3)), QStringLiteral("a"));
    QCOMPARE(aux.value(key(QStringLiteral("S1"), 4)), QStringLiteral("b"));
    QCOMPARE(aux.size(), 2);

    // Drop the poisoned entry and the rest applies, collision-free.
    QHash<PlacementStateKey, QString> auxClean;
    auxClean.insert(key(QStringLiteral("S1"), 4), QStringLiteral("b"));
    QHash<int, int> clean;
    clean.insert(4, 3);
    PhosphorEngine::renumberDesktopKeyedHash(auxClean, clean);
    QCOMPARE(auxClean.value(key(QStringLiteral("S1"), 3)), QStringLiteral("b"));
    QCOMPARE(auxClean.size(), 1);
}

void TestPerScreenStates::renumberDesktopKeyedHash_auxMaps()
{
    QHash<PlacementStateKey, QString> aux;
    aux.insert(key(QStringLiteral("S1"), 3), QStringLiteral("a"));
    aux.insert(key(QStringLiteral("S1"), 4), QStringLiteral("b"));
    aux.insert(key(QStringLiteral("S2"), 1), QStringLiteral("c"));

    QHash<int, int> mapping;
    mapping.insert(3, 2);
    mapping.insert(4, 3);
    PhosphorEngine::renumberDesktopKeyedHash(aux, mapping);

    QCOMPARE(aux.value(key(QStringLiteral("S1"), 2)), QStringLiteral("a"));
    QCOMPARE(aux.value(key(QStringLiteral("S1"), 3)), QStringLiteral("b"));
    QCOMPARE(aux.value(key(QStringLiteral("S2"), 1)), QStringLiteral("c"));
    QCOMPARE(aux.size(), 3);
}

QTEST_MAIN(TestPerScreenStates)
#include "test_perscreenstates.moc"
