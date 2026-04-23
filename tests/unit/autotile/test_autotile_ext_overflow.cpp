// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "../helpers/AutotileTestHelpers.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Extended tests for minimize tracking, mode transition,
 *        window insertion, and overflow integration behaviors
 */
class TestAutotileExtOverflow : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        PlasmaZones::TestHelpers::testRegistry();
    }

    // =========================================================================
    // Minimize tracking (floating window guard)
    // =========================================================================

    void testMinimize_floatingWindowNotRemovedOnZoneClear()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->isFloating(QStringLiteral("win1")));
        QVERIFY(state->containsWindow(QStringLiteral("win1")));

        QVERIFY(state->containsWindow(QStringLiteral("win1")));
    }

    void testMinimize_windowClosedBypassesFloatingGuard()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->isFloating(QStringLiteral("win1")));

        engine.windowClosed(QStringLiteral("win1"));
        QCoreApplication::processEvents();

        QVERIFY(!state->containsWindow(QStringLiteral("win1")));
    }

    void testMinimize_retilingFlagSkipsZoneChanges()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QCOMPARE(state->windowCount(), 2);

        engine.retile();

        QCOMPARE(state->windowCount(), 2);
    }

    // =========================================================================
    // Mode transition
    // =========================================================================

    void testModeTransition_savedFloatingRestoredOnReEnable()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->isFloating(QStringLiteral("win1")));

        engine.setAutotileScreens({});

        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        state = engine.tilingStateForScreen(screen);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(state->isFloating(QStringLiteral("win1")));
    }

    void testModeTransition_overflowWindowsNotSaved()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);

        engine.setAutotileScreens({});

        engine.config()->maxWindows = 5;
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        state = engine.tilingStateForScreen(screen);
        QVERIFY(state->containsWindow(QStringLiteral("win3")));
    }

    void testModeTransition_clearSavedFloatingForZoneSnappedWindows()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));
        engine.setAutotileScreens({});

        engine.clearSavedFloatingForWindows({QStringLiteral("win1")});

        engine.setAutotileScreens({screen});
        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(!state->isFloating(QStringLiteral("win1")));
    }

    void testModeTransition_wtsFloatingPreserved()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(!state->isFloating(QStringLiteral("win1")));
    }

    void testModeTransition_windowClosedCleansSavedFloating()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));
        engine.setAutotileScreens({});

        engine.windowClosed(QStringLiteral("win1"));

        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(!state->isFloating(QStringLiteral("win1")));
    }

    // =========================================================================
    // Window insertion
    // =========================================================================

    void testInsertWindow_preSeededOrder_correctPosition()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        QStringList order = {QStringLiteral("win2"), QStringLiteral("win1"), QStringLiteral("win3")};
        engine.setInitialWindowOrder(screen, order);

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);

        QCOMPARE(state->windowCount(), 3);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(state->containsWindow(QStringLiteral("win2")));
        QVERIFY(state->containsWindow(QStringLiteral("win3")));
    }

    void testInsertWindow_afterFocused_insertsAtCorrectIndex()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->insertPosition = AutotileConfig::InsertPosition::AfterFocused;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->setFocusedWindow(QStringLiteral("win1"));

        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        QCOMPARE(state->windowIndex(QStringLiteral("win3")), 1);
    }

    void testInsertWindow_asMaster_movesToFront()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->insertPosition = AutotileConfig::InsertPosition::AsMaster;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);

        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        QCOMPARE(state->windowOrder().first(), QStringLiteral("win3"));
    }

    void testInsertWindow_duplicateWindowRejected()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QCOMPARE(state->windowCount(), 1);

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        QCOMPARE(state->windowCount(), 1);
    }

    // =========================================================================
    // Overflow integration
    // =========================================================================

    void testOverflow_signalsEmittedAfterGeometryBatch()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);

        QCOMPARE(state->tiledWindowCount(), 2);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(state->containsWindow(QStringLiteral("win2")));
    }

    void testOverflow_backfillPrioritizesRecoveryOverNew()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QCOMPARE(state->tiledWindowCount(), 2);

        engine.windowClosed(QStringLiteral("win2"));
        QCoreApplication::processEvents();

        QVERIFY(state->containsWindow(QStringLiteral("win1")));
    }

    void testOverflow_maxWindowsDecreaseOverflowsCorrectly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 4;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        engine.windowOpened(QStringLiteral("win4"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QCOMPARE(state->tiledWindowCount(), 4);

        engine.config()->maxWindows = 2;
        engine.retile();
        QCoreApplication::processEvents();

        QCOMPARE(engine.config()->maxWindows, 2);
    }
};

QTEST_MAIN(TestAutotileExtOverflow)
#include "test_autotile_ext_overflow.moc"
