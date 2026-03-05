// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "autotile/TilingState.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for TilingState window order management.
 *
 * Covers addWindow, removeWindow, moveWindow, swapWindows, promoteToMaster,
 * insertAfterFocused, rotateWindows, and moveToPosition.
 */
class TestTilingStateWindows : public QObject
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

        // Tiled windows before rotation: [A, C, D]
        // Clockwise rotation of tiled: [A, C, D] -> [D, A, C]
        QVERIFY(state.rotateWindows(true));

        // Verify tiled-only order is correct (floating window position is
        // an implementation detail of the merge strategy)
        const QStringList tiledAfter = state.tiledWindows();
        QCOMPARE(tiledAfter, QStringList({QStringLiteral("D"), QStringLiteral("A"), QStringLiteral("C")}));

        // B should still be floating
        QVERIFY(state.isFloating(QStringLiteral("B")));

        // Total window count unchanged
        QCOMPARE(state.windowCount(), 4);
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

QTEST_MAIN(TestTilingStateWindows)
#include "test_tiling_state_windows.moc"
