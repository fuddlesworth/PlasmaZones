// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SnapState's layer-side focus memories (the switch verb's remembered
// targets): noteFocused classification, the clear-on-layer-change sites
// (setFloating, setFloatingOnScreen, assignWindowToZones' snap-clears-float
// arm, clearZoneAssignment), removal, cross-screen migration (memories stay
// behind and clear, never seed the target), and clear()/isEmpty() covering
// the memory fields.

#include <PhosphorSnapEngine/SnapState.h>

#include <QTest>

using PhosphorSnapEngine::SnapState;

class TestSnapStateFocusMemory : public QObject
{
    Q_OBJECT

    static const inline QString kScreen = QStringLiteral("DP-1");
    static const inline QString kSnapped = QStringLiteral("app|snapped");
    static const inline QString kFloat = QStringLiteral("app|float");
    static const inline QString kFree = QStringLiteral("app|free");

private Q_SLOTS:
    void noteFocusedClassifiesBySide()
    {
        SnapState state(kScreen);
        state.assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        state.setFloating(kFloat, true);
        state.recordResidence(kFree, kScreen, 1);

        state.noteFocused(kSnapped);
        QCOMPARE(state.lastSnappedFocus(), kSnapped);
        QVERIFY(state.lastFloatingFocus().isEmpty());

        state.noteFocused(kFloat);
        QCOMPARE(state.lastFloatingFocus(), kFloat);
        // The snapped memory survives the float taking focus — its point.
        QCOMPARE(state.lastSnappedFocus(), kSnapped);

        // Residence-only windows are on neither layer: neither memory moves.
        state.noteFocused(kFree);
        QCOMPARE(state.lastSnappedFocus(), kSnapped);
        QCOMPARE(state.lastFloatingFocus(), kFloat);
    }

    void setFloatingClearsTheDepartedSide()
    {
        SnapState state(kScreen);
        state.assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        state.noteFocused(kSnapped);
        QCOMPARE(state.lastSnappedFocus(), kSnapped);

        // unsnap-then-float is the production shape; the float bit alone
        // must already clear the snapped memory. (This leaves the window in
        // both the zone map and the float set, a state production never
        // produces — the point here is the departed-side clear, and the
        // noteFocused below incidentally exercises its float-first
        // precedence in that artificial state.)
        state.setFloating(kSnapped, true);
        QVERIFY(state.lastSnappedFocus().isEmpty());

        state.noteFocused(kSnapped);
        QCOMPARE(state.lastFloatingFocus(), kSnapped);
        state.setFloating(kSnapped, false);
        QVERIFY(state.lastFloatingFocus().isEmpty());
    }

    void setFloatingOnScreenClearsTheSnappedMemory()
    {
        SnapState state(kScreen);
        state.assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        state.noteFocused(kSnapped);

        state.setFloatingOnScreen(kSnapped, kScreen, 1);
        QVERIFY(state.lastSnappedFocus().isEmpty());
    }

    void snapClearsTheFloatMemory()
    {
        // assignWindowToZone on a floating window clears the float bit AND
        // the float-side memory naming it (the snap-clears-float arm).
        SnapState state(kScreen);
        state.setFloating(kFloat, true);
        state.noteFocused(kFloat);
        QCOMPARE(state.lastFloatingFocus(), kFloat);

        state.assignWindowToZone(kFloat, QStringLiteral("zone-1"), kScreen, 1);
        QVERIFY(state.lastFloatingFocus().isEmpty());
        QVERIFY(state.isWindowSnapped(kFloat));
    }

    void unsnapClearsTheSnappedMemory()
    {
        SnapState state(kScreen);
        state.assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        state.noteFocused(kSnapped);

        state.unassignWindow(kSnapped);
        QVERIFY(state.lastSnappedFocus().isEmpty());
    }

    void removalClearsTheMemories()
    {
        SnapState state(kScreen);
        state.assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        state.setFloating(kFloat, true);
        state.noteFocused(kSnapped);
        state.noteFocused(kFloat);

        state.removeWindowData(kSnapped);
        QVERIFY(state.lastSnappedFocus().isEmpty());
        QCOMPARE(state.lastFloatingFocus(), kFloat);
        state.removeWindowData(kFloat);
        QVERIFY(state.lastFloatingFocus().isEmpty());
    }

    void migrationClearsAndNeverSeedsTheTarget()
    {
        // Cross-screen migration: focus memories stay behind and CLEAR on
        // the source; the target store starts fresh and is armed only by
        // the next focus report there.
        SnapState source(kScreen);
        SnapState target(QStringLiteral("DP-2"));
        source.assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        source.noteFocused(kSnapped);
        QCOMPARE(source.lastSnappedFocus(), kSnapped);

        source.migrateWindowTo(&target, kSnapped, QStringLiteral("DP-2"));
        QVERIFY(source.lastSnappedFocus().isEmpty());
        QVERIFY(target.lastSnappedFocus().isEmpty());
        QVERIFY(target.lastFloatingFocus().isEmpty());
    }

    void clearCoversTheMemories()
    {
        SnapState state(kScreen);
        // Full teardown clears an armed memory alongside everything else.
        // (The memory can never be the ONLY surviving state through the
        // public API — every path that empties the layer sets also clears
        // the matching memory — so isEmpty()'s memory terms are pure
        // defence and cannot be pinned from here; this slot pins clear()'s
        // own resets.)
        state.setFloating(kFloat, true);
        state.noteFocused(kFloat);
        QVERIFY(!state.isEmpty());
        state.clear();
        QVERIFY(state.lastFloatingFocus().isEmpty());
        QVERIFY(state.lastSnappedFocus().isEmpty());
        QVERIFY(state.isEmpty());
    }
};

QTEST_MAIN(TestSnapStateFocusMemory)
#include "test_snap_state_focus_memory.moc"
