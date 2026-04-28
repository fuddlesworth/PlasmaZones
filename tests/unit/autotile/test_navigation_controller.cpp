// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/AutotileTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Unit tests for NavigationController (via AutotileEngine forwarding)
 *
 * NavigationController is a stateless helper accessed through AutotileEngine's
 * public API. Tests exercise navigation operations through the engine interface
 * and verify signals/feedback via QSignalSpy.
 *
 * Tests cover:
 * - Focus next/previous wrap-around
 * - Swap focused with master (already master feedback)
 * - Rotate window order feedback
 * - Swap focused in direction wrap-around
 * - Move focused to position (1-based to 0-based mapping)
 * - Increase master ratio propagation
 * - tiledWindowsForFocusedScreen primary screen fallback
 * - resolveActiveScreen fallback
 */
class TestNavigationController : public QObject
{
    Q_OBJECT

private:
    /**
     * @brief Create an engine with a single autotile screen and N windows.
     *
     * Delegates to TestHelpers::createEngineWithWindows which uses
     * engine->windowOpened() + processEvents() to register windows through
     * the proper lifecycle (populating m_windowToScreen), rather than adding
     * directly to PhosphorTiles::TilingState which leaves internal maps empty.
     */
    AutotileEngine* createEngineWithWindows(const QString& screen, int windowCount,
                                            const QString& focusedWindow = QString())
    {
        return TestHelpers::createEngineWithWindows(screen, windowCount, focusedWindow);
    }

private Q_SLOTS:

    void initTestCase()
    {
        PlasmaZones::TestHelpers::testRegistry();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Focus cycling
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_focusNext_wrapsAround()
    {
        const QString screen = QStringLiteral("eDP-1");
        QScopedPointer<AutotileEngine> engine(createEngineWithWindows(screen, 3, QStringLiteral("win3")));

        QSignalSpy focusSpy(engine.data(), &AutotileEngine::activateWindowRequested);

        // Focused on win3 (last window), focusNext should wrap to win1
        engine->focusNext();

        QCOMPARE(focusSpy.count(), 1);
        QCOMPARE(focusSpy.first().first().toString(), QStringLiteral("win1"));
    }

    void testNavigation_focusPrevious_wrapsAround()
    {
        const QString screen = QStringLiteral("eDP-1");
        QScopedPointer<AutotileEngine> engine(createEngineWithWindows(screen, 3, QStringLiteral("win1")));

        QSignalSpy focusSpy(engine.data(), &AutotileEngine::activateWindowRequested);

        // Focused on win1 (first window), focusPrevious should wrap to win3
        engine->focusPrevious();

        QCOMPARE(focusSpy.count(), 1);
        QCOMPARE(focusSpy.first().first().toString(), QStringLiteral("win3"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Swap with master
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_swapFocusedWithMaster_alreadyMaster()
    {
        const QString screen = QStringLiteral("eDP-1");
        QScopedPointer<AutotileEngine> engine(createEngineWithWindows(screen, 3, QStringLiteral("win1")));

        QSignalSpy feedbackSpy(engine.data(), &AutotileEngine::navigationFeedback);

        // win1 is already at position 0 (master). moveToTiledPosition returns true
        // for no-op moves (fromIndex == toIndex is treated as success in moveWindow),
        // so the "promoted" branch executes, emitting "master" with success=true.
        engine->swapFocusedWithMaster();

        QVERIFY(feedbackSpy.count() >= 1);
        // Find the swap_master feedback
        bool foundMasterFeedback = false;
        for (const auto& args : feedbackSpy) {
            if (args.at(1).toString() == QStringLiteral("swap_master")
                && args.at(2).toString() == QStringLiteral("master")) {
                foundMasterFeedback = true;
                QCOMPARE(args.at(0).toBool(), true); // success=true (no-op move is success)
                break;
            }
        }
        QVERIFY(foundMasterFeedback);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Rotate
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_rotateWindowOrder_emitsFeedback()
    {
        const QString screen = QStringLiteral("eDP-1");
        QScopedPointer<AutotileEngine> engine(createEngineWithWindows(screen, 3, QStringLiteral("win1")));

        QSignalSpy feedbackSpy(engine.data(), &AutotileEngine::navigationFeedback);

        engine->rotateWindowOrder(true);

        // Should emit rotate feedback with success=true
        bool foundRotateFeedback = false;
        for (const auto& args : feedbackSpy) {
            if (args.at(1).toString() == QStringLiteral("rotate")) {
                foundRotateFeedback = true;
                QCOMPARE(args.at(0).toBool(), true); // success
                QVERIFY(args.at(2).toString().contains(QStringLiteral("clockwise")));
                break;
            }
        }
        QVERIFY(foundRotateFeedback);

        // Verify the window order actually rotated: [win1,win2,win3] -> [win3,win1,win2]
        PhosphorTiles::TilingState* state = engine->tilingStateForScreen(screen);
        QStringList expected = {QStringLiteral("win3"), QStringLiteral("win1"), QStringLiteral("win2")};
        QCOMPARE(state->windowOrder(), expected);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Directional swap
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_swapFocusedInDirection_wrapsAround()
    {
        const QString screen = QStringLiteral("eDP-1");
        QScopedPointer<AutotileEngine> engine(createEngineWithWindows(screen, 3, QStringLiteral("win1")));

        QSignalSpy feedbackSpy(engine.data(), &AutotileEngine::navigationFeedback);

        // win1 is at index 0. Swapping "left" (backward) should wrap to win3.
        engine->swapFocusedInDirection(QStringLiteral("left"), QStringLiteral("move"));

        PhosphorTiles::TilingState* state = engine->tilingStateForScreen(screen);
        // After swap: win1 and win3 exchange positions
        // [win1, win2, win3] -> swap(0, 2) -> [win3, win2, win1]
        QCOMPARE(state->windowIndex(QStringLiteral("win1")), 2);
        QCOMPARE(state->windowIndex(QStringLiteral("win3")), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Move to position
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_moveFocusedToPosition_oneBased()
    {
        const QString screen = QStringLiteral("eDP-1");
        QScopedPointer<AutotileEngine> engine(createEngineWithWindows(screen, 3, QStringLiteral("win3")));

        QSignalSpy feedbackSpy(engine.data(), &AutotileEngine::navigationFeedback);

        // Position 1 should map to index 0 (one-based to zero-based)
        engine->moveFocusedToPosition(1);

        PhosphorTiles::TilingState* state = engine->tilingStateForScreen(screen);
        // win3 was at index 2, should now be at tiled position 0
        QCOMPARE(state->tiledWindowIndex(QStringLiteral("win3")), 0);

        // Check feedback
        bool foundSnapFeedback = false;
        for (const auto& args : feedbackSpy) {
            if (args.at(1).toString() == QStringLiteral("snap")) {
                foundSnapFeedback = true;
                QCOMPARE(args.at(0).toBool(), true); // success
                break;
            }
        }
        QVERIFY(foundSnapFeedback);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Master ratio propagation
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_increaseMasterRatio_updatesFocusedScreenOnly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});

        // Use windowOpened to properly set up m_windowToStateKey mappings
        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen2, 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);

        // Set a known initial ratio
        state1->setSplitRatio(0.5);
        state2->setSplitRatio(0.5);

        // Use windowFocused to properly set m_activeScreen on the engine
        engine.windowFocused(QStringLiteral("win1"), screen1);

        engine.increaseMasterRatio(0.1);

        // Only the focused screen (eDP-1) should change
        QVERIFY(qFuzzyCompare(state1->splitRatio(), 0.6));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), 0.5));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Fallback behavior
    // ═══════════════════════════════════════════════════════════════════════════

    void testNavigation_tiledWindowsForFocusedScreen_fallbackToPrimary()
    {
        // When no window has focus, focusNext/focusPrevious should still work
        // if there is a screen with tiled windows. Without a Phosphor::Screens::ScreenManager,
        // the fallback returns empty — no crash expected.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        // No focused window set, no m_activeScreen set

        QSignalSpy focusSpy(&engine, &AutotileEngine::activateWindowRequested);

        // Without Phosphor::Screens::ScreenManager, the primary screen fallback in
        // tiledWindowsForFocusedScreen returns empty. No crash expected.
        engine.focusNext();

        // No focus request since no focused window and no primary screen
        QCOMPARE(focusSpy.count(), 0);
    }

    void testNavigation_resolveActiveScreen_fallback()
    {
        // When m_activeScreen is empty, resolveActiveScreen falls back to
        // the first autotile screen. Verify by checking feedback screen name.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        state->setFocusedWindow(QStringLiteral("win1"));
        // m_activeScreen is NOT set (no windowFocused call)

        QSignalSpy feedbackSpy(&engine, &AutotileEngine::navigationFeedback);

        engine.increaseMasterRatio(0.05);

        // Feedback should use HDMI-1 (the only autotile screen) as fallback
        QVERIFY(feedbackSpy.count() >= 1);
        const auto& args = feedbackSpy.first();
        // screenName is the 6th argument (index 5)
        QCOMPARE(args.at(5).toString(), screen);
    }
};

QTEST_MAIN(TestNavigationController)
#include "test_navigation_controller.moc"
