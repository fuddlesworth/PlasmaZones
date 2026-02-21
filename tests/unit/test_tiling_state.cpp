// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>

#include "autotile/TilingState.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for TilingState
 *
 * Tests cover:
 * - Window order management (add, remove, move, swap)
 * - Master management (promote, moveToFront, insertAfterFocused)
 * - Window rotation (clockwise, counterclockwise, with floating)
 * - Master count and split ratio (clamping, signals)
 * - Per-window floating state
 * - Focus tracking
 * - Serialization roundtrip (toJson/fromJson)
 * - clear() method
 * - Signal emissions via QSignalSpy
 */
class TestTilingState : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // addWindow / removeWindow
    // ═══════════════════════════════════════════════════════════════════════════

    void testAddWindow_basic()
    {
        TilingState state(QStringLiteral("screen0"));
        QCOMPARE(state.screenName(), QStringLiteral("screen0"));
        QCOMPARE(state.windowCount(), 0);

        QVERIFY(state.addWindow(QStringLiteral("win1")));
        QCOMPARE(state.windowCount(), 1);
        QVERIFY(state.containsWindow(QStringLiteral("win1")));

        QVERIFY(state.addWindow(QStringLiteral("win2")));
        QCOMPARE(state.windowCount(), 2);

        // Default insertion is at end
        QCOMPARE(state.windowOrder(), QStringList({QStringLiteral("win1"), QStringLiteral("win2")}));
    }

    void testAddWindow_duplicate()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(state.addWindow(QStringLiteral("win1")));

        // Adding same window again should fail
        QVERIFY(!state.addWindow(QStringLiteral("win1")));
        QCOMPARE(state.windowCount(), 1);
    }

    void testAddWindow_emptyString()
    {
        TilingState state(QStringLiteral("test"));

        // Empty string should be rejected
        QVERIFY(!state.addWindow(QString()));
        QVERIFY(!state.addWindow(QStringLiteral("")));
        QCOMPARE(state.windowCount(), 0);
    }

    void testAddWindow_positionInsertion()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // Insert at position 0 (front)
        QVERIFY(state.addWindow(QStringLiteral("win0"), 0));
        QCOMPARE(state.windowOrder().first(), QStringLiteral("win0"));
        QCOMPARE(state.windowCount(), 4);

        // Insert at position 2 (middle)
        QVERIFY(state.addWindow(QStringLiteral("winMiddle"), 2));
        QCOMPARE(state.windowIndex(QStringLiteral("winMiddle")), 2);
    }

    void testAddWindow_positionOutOfRange()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        // Position beyond size should append to end
        QVERIFY(state.addWindow(QStringLiteral("win2"), 100));
        QCOMPARE(state.windowOrder().last(), QStringLiteral("win2"));

        // Negative position should also append to end
        QVERIFY(state.addWindow(QStringLiteral("win3"), -1));
        QCOMPARE(state.windowOrder().last(), QStringLiteral("win3"));
    }

    void testAddWindow_signal()
    {
        TilingState state(QStringLiteral("test"));
        QSignalSpy countSpy(&state, &TilingState::windowCountChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.addWindow(QStringLiteral("win1"));
        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testRemoveWindow_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        QVERIFY(state.removeWindow(QStringLiteral("win2")));
        QCOMPARE(state.windowCount(), 2);
        QVERIFY(!state.containsWindow(QStringLiteral("win2")));
        QCOMPARE(state.windowOrder(), QStringList({QStringLiteral("win1"), QStringLiteral("win3")}));
    }

    void testRemoveWindow_nonExistent()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QVERIFY(!state.removeWindow(QStringLiteral("nonexistent")));
        QCOMPARE(state.windowCount(), 1);
    }

    void testRemoveWindow_clearsFocused()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.setFocusedWindow(QStringLiteral("win1"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("win1"));

        QSignalSpy focusSpy(&state, &TilingState::focusedWindowChanged);
        state.removeWindow(QStringLiteral("win1"));
        QVERIFY(state.focusedWindow().isEmpty());
        QCOMPARE(focusSpy.count(), 1);
    }

    void testRemoveWindow_clearsFloating()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.setFloating(QStringLiteral("win1"), true);
        QVERIFY(state.isFloating(QStringLiteral("win1")));

        state.removeWindow(QStringLiteral("win1"));
        // After removal, floating status should be cleaned up
        QVERIFY(!state.isFloating(QStringLiteral("win1")));
    }

    void testRemoveWindow_signal()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy countSpy(&state, &TilingState::windowCountChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.removeWindow(QStringLiteral("win1"));
        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // moveWindow / swapWindows / swapWindowsById
    // ═══════════════════════════════════════════════════════════════════════════

    void testMoveWindow_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        // Move C (index 2) to front (index 0)
        QVERIFY(state.moveWindow(2, 0));
        QCOMPARE(state.windowOrder(), QStringList({QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")}));
    }

    void testMoveWindow_sameIndex()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        QSignalSpy orderSpy(&state, &TilingState::windowOrderChanged);

        // Moving to same position is a no-op but returns success
        QVERIFY(state.moveWindow(0, 0));
        // No signal should be emitted for no-op
        QCOMPARE(orderSpy.count(), 0);
    }

    void testMoveWindow_invalidIndex()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(!state.moveWindow(-1, 0));
        QVERIFY(!state.moveWindow(0, -1));
        QVERIFY(!state.moveWindow(5, 0));
        QVERIFY(!state.moveWindow(0, 5));
    }

    void testMoveWindow_signal()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        QSignalSpy orderSpy(&state, &TilingState::windowOrderChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.moveWindow(0, 1);
        QCOMPARE(orderSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testSwapWindows_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QVERIFY(state.swapWindows(0, 2));
        QCOMPARE(state.windowOrder(), QStringList({QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("A")}));
    }

    void testSwapWindows_sameIndex()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QSignalSpy orderSpy(&state, &TilingState::windowOrderChanged);
        QVERIFY(state.swapWindows(0, 0));
        QCOMPARE(orderSpy.count(), 0);
    }

    void testSwapWindows_invalidIndex()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(!state.swapWindows(-1, 0));
        QVERIFY(!state.swapWindows(0, 5));
    }

    void testSwapWindowsById_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QVERIFY(state.swapWindowsById(QStringLiteral("A"), QStringLiteral("C")));
        QCOMPARE(state.windowIndex(QStringLiteral("A")), 2);
        QCOMPARE(state.windowIndex(QStringLiteral("C")), 0);
    }

    void testSwapWindowsById_nonExistent()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(!state.swapWindowsById(QStringLiteral("A"), QStringLiteral("B")));
        QVERIFY(!state.swapWindowsById(QStringLiteral("X"), QStringLiteral("Y")));
    }

    void testSwapWindowsById_sameWindow()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        // Swapping same window with itself is a no-op success
        QVERIFY(state.swapWindowsById(QStringLiteral("A"), QStringLiteral("A")));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // promoteToMaster / moveToFront / insertAfterFocused
    // ═══════════════════════════════════════════════════════════════════════════

    void testPromoteToMaster_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QVERIFY(state.promoteToMaster(QStringLiteral("C")));
        QCOMPARE(state.windowOrder().first(), QStringLiteral("C"));
        QCOMPARE(state.windowIndex(QStringLiteral("C")), 0);
    }

    void testPromoteToMaster_alreadyFirst()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        QSignalSpy orderSpy(&state, &TilingState::windowOrderChanged);
        QVERIFY(state.promoteToMaster(QStringLiteral("A")));
        // No signal when already at position 0
        QCOMPARE(orderSpy.count(), 0);
    }

    void testPromoteToMaster_nonExistent()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(!state.promoteToMaster(QStringLiteral("nonexistent")));
    }

    void testMoveToFront_aliasForPromote()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QVERIFY(state.moveToFront(QStringLiteral("B")));
        QCOMPARE(state.windowOrder().first(), QStringLiteral("B"));
    }

    void testInsertAfterFocused_withFocus()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFocusedWindow(QStringLiteral("A"));

        // Insert after focused (A at index 0) -> should go to index 1
        QVERIFY(state.insertAfterFocused(QStringLiteral("D")));
        QCOMPARE(state.windowIndex(QStringLiteral("D")), 1);
        QCOMPARE(state.windowCount(), 4);
    }

    void testInsertAfterFocused_noFocus()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        // No focused window set

        // Without focus, should append to end
        QVERIFY(state.insertAfterFocused(QStringLiteral("C")));
        QCOMPARE(state.windowOrder().last(), QStringLiteral("C"));
    }

    void testInsertAfterFocused_duplicate()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        // Cannot insert an already-tracked window
        QVERIFY(!state.insertAfterFocused(QStringLiteral("A")));
    }

    void testInsertAfterFocused_emptyId()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        QVERIFY(!state.insertAfterFocused(QString()));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // rotateWindows
    // ═══════════════════════════════════════════════════════════════════════════

    void testRotateWindows_clockwise()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        // Clockwise: [A, B, C] -> [C, A, B]
        QVERIFY(state.rotateWindows(true));
        QCOMPARE(state.windowOrder(), QStringList({QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")}));
    }

    void testRotateWindows_counterclockwise()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        // Counter-clockwise: [A, B, C] -> [B, C, A]
        QVERIFY(state.rotateWindows(false));
        QCOMPARE(state.windowOrder(), QStringList({QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("A")}));
    }

    void testRotateWindows_singleWindow()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        // Cannot rotate with only 1 window
        QVERIFY(!state.rotateWindows(true));
        QVERIFY(!state.rotateWindows(false));
    }

    void testRotateWindows_noWindows()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(!state.rotateWindows(true));
    }

    void testRotateWindows_withFloatingWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B")); // will be floating
        state.addWindow(QStringLiteral("C"));
        state.addWindow(QStringLiteral("D"));
        state.setFloating(QStringLiteral("B"), true);

        // Tiled windows: [A, C, D] (B is floating at index 1)
        // Clockwise rotation of tiled: [A, C, D] -> [D, A, C]
        // Full order becomes: [D, B(floating), A, C]
        QVERIFY(state.rotateWindows(true));

        QStringList expected = {QStringLiteral("D"), QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("C")};
        QCOMPARE(state.windowOrder(), expected);

        // B should still be floating
        QVERIFY(state.isFloating(QStringLiteral("B")));
    }

    void testRotateWindows_allFloatingExceptOne()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFloating(QStringLiteral("A"), true);

        // Only 1 tiled window (B), cannot rotate
        QVERIFY(!state.rotateWindows(true));
    }

    void testRotateWindows_signal()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        QSignalSpy orderSpy(&state, &TilingState::windowOrderChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.rotateWindows(true);
        QCOMPARE(orderSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // masterCount
    // ═══════════════════════════════════════════════════════════════════════════

    void testMasterCount_default()
    {
        TilingState state(QStringLiteral("test"));
        QCOMPARE(state.masterCount(), AutotileDefaults::DefaultMasterCount);
    }

    void testMasterCount_setAndGet()
    {
        TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 5; ++i) {
            state.addWindow(QStringLiteral("win%1").arg(i));
        }

        state.setMasterCount(3);
        QCOMPARE(state.masterCount(), 3);
    }

    void testMasterCount_clampToMin()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        state.setMasterCount(0);
        QCOMPARE(state.masterCount(), AutotileDefaults::MinMasterCount);

        state.setMasterCount(-5);
        QCOMPARE(state.masterCount(), AutotileDefaults::MinMasterCount);
    }

    void testMasterCount_clampToMax()
    {
        TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 3; ++i) {
            state.addWindow(QStringLiteral("win%1").arg(i));
        }

        // masterCount clamps to MaxMasterCount (absolute limit), not window count.
        // Algorithms clamp operationally when calculating zones.
        state.setMasterCount(10);
        QCOMPARE(state.masterCount(), AutotileDefaults::MaxMasterCount);
    }

    void testMasterCount_clampToMaxConstant()
    {
        TilingState state(QStringLiteral("test"));
        for (int i = 0; i < 20; ++i) {
            state.addWindow(QStringLiteral("win%1").arg(i));
        }

        // With many windows, should clamp to MaxMasterCount
        state.setMasterCount(100);
        QCOMPARE(state.masterCount(), AutotileDefaults::MaxMasterCount);
    }

    void testMasterCount_signal()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        QSignalSpy masterSpy(&state, &TilingState::masterCountChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.setMasterCount(2);
        QCOMPARE(masterSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testMasterCount_noSignalOnSameValue()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy masterSpy(&state, &TilingState::masterCountChanged);
        // Default is 1, setting to 1 again should not emit
        state.setMasterCount(1);
        QCOMPARE(masterSpy.count(), 0);
    }

    void testIsMaster_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setMasterCount(1);

        QVERIFY(state.isMaster(QStringLiteral("A")));
        QVERIFY(!state.isMaster(QStringLiteral("B")));
        QVERIFY(!state.isMaster(QStringLiteral("C")));
    }

    void testIsMaster_floatingExcluded()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFloating(QStringLiteral("A"), true);

        // A is floating, so B becomes master
        QVERIFY(!state.isMaster(QStringLiteral("A")));
        QVERIFY(state.isMaster(QStringLiteral("B")));
    }

    void testMasterWindows_and_stackWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.addWindow(QStringLiteral("D"));
        state.setMasterCount(2);

        QCOMPARE(state.masterWindows(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
        QCOMPARE(state.stackWindows(), QStringList({QStringLiteral("C"), QStringLiteral("D")}));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // splitRatio
    // ═══════════════════════════════════════════════════════════════════════════

    void testSplitRatio_default()
    {
        TilingState state(QStringLiteral("test"));
        QCOMPARE(state.splitRatio(), AutotileDefaults::DefaultSplitRatio);
    }

    void testSplitRatio_setAndGet()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        QVERIFY(qFuzzyCompare(state.splitRatio(), 0.7));
    }

    void testSplitRatio_clampMin()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.01); // Below MinSplitRatio (0.1)
        QVERIFY(qFuzzyCompare(state.splitRatio(), AutotileDefaults::MinSplitRatio));
    }

    void testSplitRatio_clampMax()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.99); // Above MaxSplitRatio (0.9)
        QVERIFY(qFuzzyCompare(state.splitRatio(), AutotileDefaults::MaxSplitRatio));
    }

    void testSplitRatio_increase()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        state.increaseSplitRatio(0.1);
        QVERIFY(qFuzzyCompare(state.splitRatio(), 0.6));
    }

    void testSplitRatio_decrease()
    {
        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        state.decreaseSplitRatio(0.1);
        QVERIFY(qFuzzyCompare(state.splitRatio(), 0.4));
    }

    void testSplitRatio_signal()
    {
        TilingState state(QStringLiteral("test"));
        QSignalSpy ratioSpy(&state, &TilingState::splitRatioChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.setSplitRatio(0.7);
        QCOMPARE(ratioSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testSplitRatio_noSignalOnSameValue()
    {
        TilingState state(QStringLiteral("test"));
        // Default is 0.6
        QSignalSpy ratioSpy(&state, &TilingState::splitRatioChanged);
        state.setSplitRatio(0.6);
        QCOMPARE(ratioSpy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating state
    // ═══════════════════════════════════════════════════════════════════════════

    void testFloating_setAndCheck()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QVERIFY(!state.isFloating(QStringLiteral("win1")));
        state.setFloating(QStringLiteral("win1"), true);
        QVERIFY(state.isFloating(QStringLiteral("win1")));
        state.setFloating(QStringLiteral("win1"), false);
        QVERIFY(!state.isFloating(QStringLiteral("win1")));
    }

    void testFloating_untrackedWindow()
    {
        TilingState state(QStringLiteral("test"));

        // Setting floating on untracked window should be ignored
        state.setFloating(QStringLiteral("nonexistent"), true);
        QVERIFY(!state.isFloating(QStringLiteral("nonexistent")));
    }

    void testFloating_toggle()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        bool result = state.toggleFloating(QStringLiteral("win1"));
        QVERIFY(result);
        QVERIFY(state.isFloating(QStringLiteral("win1")));

        result = state.toggleFloating(QStringLiteral("win1"));
        QVERIFY(!result);
        QVERIFY(!state.isFloating(QStringLiteral("win1")));
    }

    void testFloating_toggleUntracked()
    {
        TilingState state(QStringLiteral("test"));

        // Toggle on untracked should return false and do nothing
        bool result = state.toggleFloating(QStringLiteral("nonexistent"));
        QVERIFY(!result);
    }

    void testFloating_tiledWindowCount()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QCOMPARE(state.windowCount(), 3);
        QCOMPARE(state.tiledWindowCount(), 3);

        state.setFloating(QStringLiteral("B"), true);
        QCOMPARE(state.windowCount(), 3);       // Total unchanged
        QCOMPARE(state.tiledWindowCount(), 2);   // One floating

        state.setFloating(QStringLiteral("C"), true);
        QCOMPARE(state.tiledWindowCount(), 1);
    }

    void testFloating_tiledWindows()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("B"), true);

        QCOMPARE(state.tiledWindows(), QStringList({QStringLiteral("A"), QStringLiteral("C")}));
    }

    void testFloating_floatingWindowsList()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("A"), true);
        state.setFloating(QStringLiteral("C"), true);

        QStringList floating = state.floatingWindows();
        QCOMPARE(floating.size(), 2);
        QVERIFY(floating.contains(QStringLiteral("A")));
        QVERIFY(floating.contains(QStringLiteral("C")));
    }

    void testFloating_signal()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy floatSpy(&state, &TilingState::floatingChanged);
        QSignalSpy countSpy(&state, &TilingState::windowCountChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.setFloating(QStringLiteral("win1"), true);
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(countSpy.count(), 1); // Tiled count changed
        QCOMPARE(stateSpy.count(), 1);

        // Verify signal arguments
        auto args = floatSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("win1"));
        QCOMPARE(args.at(1).toBool(), true);
    }

    void testFloating_noSignalOnSameValue()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy floatSpy(&state, &TilingState::floatingChanged);
        // Already not floating, setting to false should be no-op
        state.setFloating(QStringLiteral("win1"), false);
        QCOMPARE(floatSpy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Focus tracking
    // ═══════════════════════════════════════════════════════════════════════════

    void testFocusedWindow_default()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(state.focusedWindow().isEmpty());
    }

    void testFocusedWindow_setAndGet()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("A"));

        state.setFocusedWindow(QStringLiteral("B"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("B"));
    }

    void testFocusedWindow_untrackedIgnored()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        // Setting focus to untracked window should be ignored
        state.setFocusedWindow(QStringLiteral("nonexistent"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("A"));
    }

    void testFocusedWindow_clearFocus()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        // Setting empty string clears focus
        state.setFocusedWindow(QString());
        QVERIFY(state.focusedWindow().isEmpty());
    }

    void testFocusedWindow_signal()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QSignalSpy focusSpy(&state, &TilingState::focusedWindowChanged);
        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(focusSpy.count(), 1);
    }

    void testFocusedWindow_noSignalOnSameValue()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        QSignalSpy focusSpy(&state, &TilingState::focusedWindowChanged);
        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(focusSpy.count(), 0);
    }

    void testFocusedTiledIndex_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        state.setFocusedWindow(QStringLiteral("B"));
        QCOMPARE(state.focusedTiledIndex(), 1);

        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(state.focusedTiledIndex(), 0);
    }

    void testFocusedTiledIndex_noFocus()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QCOMPARE(state.focusedTiledIndex(), -1);
    }

    void testFocusedTiledIndex_floatingFocused()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFocusedWindow(QStringLiteral("A"));
        state.setFloating(QStringLiteral("A"), true);

        // Focused window is floating, so focusedTiledIndex returns -1
        QCOMPARE(state.focusedTiledIndex(), -1);
    }

    void testFocusedTiledIndex_skipsFloating()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B")); // will be floating
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("B"), true);

        // Tiled windows: [A, C], C is at tiled index 1
        state.setFocusedWindow(QStringLiteral("C"));
        QCOMPARE(state.focusedTiledIndex(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Serialization roundtrip
    // ═══════════════════════════════════════════════════════════════════════════

    void testSerialization_roundtrip()
    {
        TilingState state(QStringLiteral("monitor1"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("B"), true);
        state.setFocusedWindow(QStringLiteral("A"));
        state.setMasterCount(2);
        state.setSplitRatio(0.7);

        QJsonObject json = state.toJson();

        TilingState *restored = TilingState::fromJson(json);
        QVERIFY(restored != nullptr);

        QCOMPARE(restored->screenName(), QStringLiteral("monitor1"));
        QCOMPARE(restored->windowCount(), 3);
        QCOMPARE(restored->windowOrder(), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QVERIFY(restored->isFloating(QStringLiteral("B")));
        QVERIFY(!restored->isFloating(QStringLiteral("A")));
        QCOMPARE(restored->focusedWindow(), QStringLiteral("A"));
        QCOMPARE(restored->masterCount(), 2);
        QVERIFY(qFuzzyCompare(restored->splitRatio(), 0.7));

        delete restored;
    }

    void testSerialization_emptyState()
    {
        TilingState state(QStringLiteral("empty"));
        QJsonObject json = state.toJson();

        TilingState *restored = TilingState::fromJson(json);
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->screenName(), QStringLiteral("empty"));
        QCOMPARE(restored->windowCount(), 0);
        QCOMPARE(restored->masterCount(), AutotileDefaults::DefaultMasterCount);
        QVERIFY(qFuzzyCompare(restored->splitRatio(), AutotileDefaults::DefaultSplitRatio));

        delete restored;
    }

    void testSerialization_invalidJson()
    {
        // Missing screenName should return nullptr
        QJsonObject invalidJson;
        TilingState *result = TilingState::fromJson(invalidJson);
        QVERIFY(result == nullptr);
    }

    void testSerialization_clampsBadValues()
    {
        QJsonObject json;
        json[QStringLiteral("screenName")] = QStringLiteral("test");
        json[QStringLiteral("windowOrder")] = QJsonArray({QStringLiteral("A"), QStringLiteral("B")});
        json[QStringLiteral("floatingWindows")] = QJsonArray();
        json[QStringLiteral("focusedWindow")] = QString();
        json[QStringLiteral("masterCount")] = 99; // Way too high
        json[QStringLiteral("splitRatio")] = 5.0; // Way too high

        TilingState *restored = TilingState::fromJson(json);
        QVERIFY(restored != nullptr);

        // masterCount should be clamped to MaxMasterCount (absolute limit, not window count)
        QCOMPARE(restored->masterCount(), AutotileDefaults::MaxMasterCount);
        // splitRatio should be clamped to MaxSplitRatio (0.9)
        QVERIFY(qFuzzyCompare(restored->splitRatio(), AutotileDefaults::MaxSplitRatio));

        delete restored;
    }

    void testSerialization_invalidFloatingIgnored()
    {
        QJsonObject json;
        json[QStringLiteral("screenName")] = QStringLiteral("test");
        json[QStringLiteral("windowOrder")] = QJsonArray({QStringLiteral("A")});
        // Reference a window not in windowOrder
        json[QStringLiteral("floatingWindows")] = QJsonArray({QStringLiteral("nonexistent")});
        json[QStringLiteral("focusedWindow")] = QStringLiteral("alsoNonexistent");
        json[QStringLiteral("masterCount")] = 1;
        json[QStringLiteral("splitRatio")] = 0.5;

        TilingState *restored = TilingState::fromJson(json);
        QVERIFY(restored != nullptr);

        // Invalid floating window should be ignored
        QCOMPARE(restored->floatingWindows().size(), 0);
        // Invalid focused window should be ignored
        QVERIFY(restored->focusedWindow().isEmpty());

        delete restored;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // clear()
    // ═══════════════════════════════════════════════════════════════════════════

    void testClear_resetsAll()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFloating(QStringLiteral("A"), true);
        state.setFocusedWindow(QStringLiteral("B"));
        state.setMasterCount(2);
        state.setSplitRatio(0.8);

        state.clear();

        QCOMPARE(state.windowCount(), 0);
        QCOMPARE(state.tiledWindowCount(), 0);
        QVERIFY(state.windowOrder().isEmpty());
        QVERIFY(state.floatingWindows().isEmpty());
        QVERIFY(state.focusedWindow().isEmpty());
        QCOMPARE(state.masterCount(), AutotileDefaults::DefaultMasterCount);
        QVERIFY(qFuzzyCompare(state.splitRatio(), AutotileDefaults::DefaultSplitRatio));
    }

    void testClear_emitsSignals()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setSplitRatio(0.8);

        QSignalSpy countSpy(&state, &TilingState::windowCountChanged);
        QSignalSpy focusSpy(&state, &TilingState::focusedWindowChanged);
        QSignalSpy masterSpy(&state, &TilingState::masterCountChanged);
        QSignalSpy ratioSpy(&state, &TilingState::splitRatioChanged);
        QSignalSpy stateSpy(&state, &TilingState::stateChanged);

        state.clear();

        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(focusSpy.count(), 1);
        // masterCount was already default, but clear() still emits
        QCOMPARE(masterSpy.count(), 1);
        QCOMPARE(ratioSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testClear_noSignalIfAlreadyDefault()
    {
        TilingState state(QStringLiteral("test"));
        // State is already at defaults

        QSignalSpy stateSpy(&state, &TilingState::stateChanged);
        state.clear();
        QCOMPARE(stateSpy.count(), 0); // No change, no signal
    }

    void testClear_preservesScreenName()
    {
        TilingState state(QStringLiteral("myScreen"));
        state.addWindow(QStringLiteral("A"));
        state.clear();

        // Screen name is immutable
        QCOMPARE(state.screenName(), QStringLiteral("myScreen"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Calculated zones
    // ═══════════════════════════════════════════════════════════════════════════

    void testCalculatedZones_setAndGet()
    {
        TilingState state(QStringLiteral("test"));
        QVector<QRect> zones = {QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)};

        state.setCalculatedZones(zones);
        QCOMPARE(state.calculatedZones(), zones);
    }

    void testCalculatedZones_defaultEmpty()
    {
        TilingState state(QStringLiteral("test"));
        QVERIFY(state.calculatedZones().isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // windowIndex / containsWindow / windowPosition
    // ═══════════════════════════════════════════════════════════════════════════

    void testWindowIndex_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        QCOMPARE(state.windowIndex(QStringLiteral("A")), 0);
        QCOMPARE(state.windowIndex(QStringLiteral("B")), 1);
        QCOMPARE(state.windowIndex(QStringLiteral("C")), -1);
    }

    void testWindowPosition_aliasForIndex()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("X"));

        QCOMPARE(state.windowPosition(QStringLiteral("X")), state.windowIndex(QStringLiteral("X")));
        QCOMPARE(state.windowPosition(QStringLiteral("nope")), -1);
    }

    void testContainsWindow_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(state.containsWindow(QStringLiteral("A")));
        QVERIFY(!state.containsWindow(QStringLiteral("B")));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // moveToPosition
    // ═══════════════════════════════════════════════════════════════════════════

    void testMoveToPosition_basic()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QVERIFY(state.moveToPosition(QStringLiteral("C"), 0));
        QCOMPARE(state.windowIndex(QStringLiteral("C")), 0);
    }

    void testMoveToPosition_nonExistent()
    {
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(!state.moveToPosition(QStringLiteral("nonexistent"), 0));
    }
};

QTEST_MAIN(TestTilingState)
#include "test_tiling_state.moc"
