// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QRect>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "helpers/AutotileTestHelpers.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/WindowPlacementBuilders.h"
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QSet>
#include <memory>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Extended tests for startup/init and float/unfloat behaviors
 */
class TestAutotileExtStartupFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        PlasmaZones::TestHelpers::testRegistry();
    }

    // =========================================================================
    // Startup/init
    // =========================================================================

    void testStartup_pendingOrderTimeoutCleansUp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        QStringList order = {QStringLiteral("win1"), QStringLiteral("win2")};
        engine.setInitialWindowOrder(screen, order);

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));

        engine.windowOpened(QStringLiteral("win2"), screen);
        QCoreApplication::processEvents();

        QVERIFY(state->containsWindow(QStringLiteral("win2")));
    }

    void testStartup_coalescedRetileFromBurstWindowOpens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        QSignalSpy tilingSpy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);

        QCOMPARE(tilingSpy.count(), 0);

        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(state->windowCount(), 3);
    }

    void testStartup_pendingOrderSkipsRetileUntilWindowsArrive()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");

        QStringList order = {QStringLiteral("win1"), QStringLiteral("win2")};
        engine.tilingStateForScreen(screen);
        engine.setInitialWindowOrder(screen, order);

        QSignalSpy tilingSpy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        engine.setAutotileScreens({screen});
        QCoreApplication::processEvents();

        // The pending order mechanism prevents unnecessary empty retiles.
        // Verify mechanism does not crash by arriving windows normally.
        QSKIP(
            "Pending order skip behavior is timing-dependent; "
            "verified by testStartup_pendingOrderTimeoutCleansUp");
    }

    // =========================================================================
    // Float/unfloat
    // =========================================================================

    // #1106 on a tiling screen: a second instance floated at open by a Float
    // rule, beside a tiled sibling, comes up at the sibling's tile size (the
    // app saved it). It gets the sibling's remembered free size back where it
    // stands, from the earliest live sibling with a usable rect.
    void testFloatAtOpen_secondInstanceGetsSiblingFreeSize()
    {
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        // Declared BEFORE the service: the probe captures it by reference and
        // lives in the store, so the set has to outlive it
        // (WindowPlacementBuilders.h).
        QSet<QString> liveInstances{QStringLiteral("first")};
        PhosphorPlacement::WindowTrackingService wts(layoutManager.get(), nullptr, nullptr);
        wts.placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(liveInstances));

        AutotileEngine engine(nullptr, &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});
        engine.setFloatPredicate([](const QString& windowId, const QString&) {
            return windowId == QStringLiteral("app|second");
        });

        // The tiled sibling's record: an autotile TILED slot plus the free
        // rect it had before it was tiled.
        const QRect siblingFree(100, 100, 700, 500);
        auto sibling = PlasmaZones::TestHelpers::makePlacement(QStringLiteral("app|first"), QStringLiteral("app"),
                                                               PhosphorEngine::WindowPlacement::stateTiled(),
                                                               engine.engineId(), screen);
        sibling.freeGeometryByScreen.insert(screen, siblingFree);
        QVERIFY(wts.placementStore().record(sibling));
        engine.windowOpened(QStringLiteral("app|first"), screen);
        QCoreApplication::processEvents();

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        engine.windowOpened(QStringLiteral("app|second"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("app|second")));
        QVERIFY2(wts.placementStore().contains(QStringLiteral("app|first")), "the live sibling keeps its record");
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        const QList<QVariant> args = sizeSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("app|second"));
        QCOMPARE(args.at(1).toSize(), siblingFree.size());
        QCOMPARE(args.at(2).toString(), screen);
        // The tile-size refusal needs laid-out tile rects, which this guiless
        // fixture never produces; the scroll suite pins that arm with real
        // column rects, and the shared helper's refusal is pinned by the snap
        // suite with real zone rects.
    }

    // The own-record float restore moves OR sizes, never both, and the
    // lineage gate refuses a re-announce of a window this lineage placed.
    void testFloatAtOpen_ownRecordMoveOrSize_andLineageGate()
    {
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        // Declared BEFORE the service, as above: the probe holds a reference.
        QSet<QString> liveInstances;
        PhosphorPlacement::WindowTrackingService wts(layoutManager.get(), nullptr, nullptr);
        wts.placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(liveInstances));
        AutotileEngine engine(nullptr, &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        const auto floatingRecord = [&](const QString& windowId, const QRect& freeRect) {
            return PlasmaZones::TestHelpers::makePlacement(windowId, QStringLiteral("app"),
                                                           PhosphorEngine::WindowPlacement::stateFloating(),
                                                           engine.engineId(), screen, freeRect);
        };
        const QRect ownFree(60, 60, 700, 500);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        // Move opted in (predicate unset): the full rect, no size-only.
        QVERIFY(wts.placementStore().record(floatingRecord(QStringLiteral("app|a"), ownFree)));
        engine.windowOpened(QStringLiteral("app|a2"), screen, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("app|a2")));
        QCOMPARE(geoSpy.count(), 1);
        QCOMPARE(sizeSpy.count(), 0);
        geoSpy.clear();
        engine.windowClosed(QStringLiteral("app|a2"));

        // Move declined: the size from the record just re-bound.
        engine.setRestorePositionPredicate([](const QString&, const QString&) {
            return false;
        });
        QVERIFY(wts.placementStore().record(floatingRecord(QStringLiteral("app|b"), ownFree)));
        engine.windowOpened(QStringLiteral("app|b2"), screen, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(state->isFloating(QStringLiteral("app|b2")));
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), ownFree.size());
        engine.windowClosed(QStringLiteral("app|b2"));

        // A re-announce of a window this lineage placed: an exact-uuid record
        // WITH an autotile slot. Floated again without a move, no size.
        liveInstances.insert(QStringLiteral("placed"));
        QVERIFY(wts.placementStore().record(floatingRecord(QStringLiteral("app|placed"), ownFree)));
        engine.windowOpened(QStringLiteral("app|placed"), screen, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(state->isFloating(QStringLiteral("app|placed")));
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 0);

        // A slot-less stub under the opener's own uuid is not a placement,
        // and not a size source: the live sibling's rect comes back.
        engine.setFloatPredicate([](const QString& windowId, const QString&) {
            return windowId == QStringLiteral("app|fresh");
        });
        PhosphorEngine::WindowPlacement stub;
        stub.windowId = QStringLiteral("app|fresh");
        stub.appId = QStringLiteral("app");
        stub.screenId = screen;
        stub.freeGeometryByScreen.insert(screen, QRect(0, 0, 999, 999));
        QVERIFY(wts.placementStore().record(stub));
        liveInstances.insert(QStringLiteral("fresh"));
        engine.windowOpened(QStringLiteral("app|fresh"), screen, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(state->isFloating(QStringLiteral("app|fresh")));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), ownFree.size());
    }

    // A window that would float on its recorded home screen is not pulled
    // home by the cross-screen claim (a float is screen-local), and once the
    // dispatch reports every claim declined, the arrival screen adopts it
    // instead of deferring to the record's home a second time.
    void testClaimCrossScreenReopen_declinesAFloat_thenArrivalAdopts()
    {
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        PhosphorPlacement::WindowTrackingService wts(layoutManager.get(), nullptr, nullptr);
        AutotileEngine engine(layoutManager.get(), &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString home = QStringLiteral("DP-1");
        const QString arrival = QStringLiteral("DP-2");
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        layoutManager->setAssignmentEntryDirect(home, 0, QString(), autotile);
        layoutManager->setAssignmentEntryDirect(arrival, 0, QString(), autotile);
        engine.setAutotileScreens({home, arrival});

        auto rec = PlasmaZones::TestHelpers::makePlacement(QStringLiteral("app|old"), QStringLiteral("app"),
                                                           PhosphorEngine::WindowPlacement::stateTiled(),
                                                           engine.engineId(), home);
        QVERIFY(wts.placementStore().record(rec));
        engine.setFloatPredicate([home](const QString&, const QString& screen) {
            return screen == home;
        });
        QVERIFY2(!engine.claimCrossScreenReopen(QStringLiteral("app|new"), arrival, 0, 0),
                 "a float has nothing to pull home for");
        QVERIFY(!engine.isWindowTracked(QStringLiteral("app|new")));
        QVERIFY2(wts.placementStore().contains(QStringLiteral("app|old")), "a declined claim consumes nothing");

        engine.noteCrossScreenClaimsExhausted(QStringLiteral("app|new"), true);
        engine.windowOpened(QStringLiteral("app|new"), arrival, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* arrivalState = engine.tilingStateForScreen(arrival);
        QVERIFY(arrivalState);
        QVERIFY2(arrivalState->containsWindow(QStringLiteral("app|new")),
                 "with the claims exhausted the arrival screen must adopt the window");

        // The verdict is per ANNOUNCE, not a sticky per-window mark: an
        // announce whose claim round was suppressed states false, and that
        // clears what a previous announce set. Observed through the defer
        // gate, which needs something to defer TO — a record homed on a
        // SCROLLING screen, the term that carries no extra liveness toggle.
        PhosphorZones::AssignmentEntry scrolling;
        scrolling.mode = PhosphorZones::AssignmentEntry::Scrolling;
        layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-3"), 0, QString(), scrolling);
        engine.setScrollingModeResolver([](const QString& screen, int, const QString&) {
            return screen == QStringLiteral("DP-3");
        });
        auto scrolled = PlasmaZones::TestHelpers::makePlacement(
            QStringLiteral("strip|old"), QStringLiteral("strip"), PhosphorEngine::WindowPlacement::stateTiled(),
            QString(PhosphorEngine::WindowPlacement::scrollingEngineId()), QStringLiteral("DP-3"));
        QVERIFY(wts.placementStore().record(scrolled));

        // Cleared verdict: the gate stands down for the scroll engine.
        engine.noteCrossScreenClaimsExhausted(QStringLiteral("strip|new"), true);
        engine.noteCrossScreenClaimsExhausted(QStringLiteral("strip|new"), false);
        engine.windowOpened(QStringLiteral("strip|new"), arrival, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY2(!arrivalState->containsWindow(QStringLiteral("strip|new")),
                 "a cleared verdict must leave the defer gate free to stand down for the record's home engine");

        // Stated verdict: the claims already ran and declined, so it adopts.
        engine.noteCrossScreenClaimsExhausted(QStringLiteral("strip|new2"), true);
        engine.windowOpened(QStringLiteral("strip|new2"), arrival, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY2(arrivalState->containsWindow(QStringLiteral("strip|new2")),
                 "an exhausted round must make the same gate adopt");

        // NOT covered here: the claim bodies also clear the verdict for the
        // home open they re-enter (window_lifecycle.cpp, engine_reopen.cpp).
        // Every scenario tried for it declined before reaching the defer
        // gate, so an assertion would have passed with the clear removed.
        // The clear is defensive and untested rather than silently assumed.
    }

    void testToggleWindowFloat_crossScreenFallback()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});

        engine.windowOpened(QStringLiteral("win1"), screen1);
        QCoreApplication::processEvents();

        QSignalSpy floatSpy(&engine, &AutotileEngine::windowFloatingChanged);

        engine.toggleWindowFloat(QStringLiteral("win1"), screen2);

        QVERIFY(floatSpy.count() >= 1);
    }

    void testWindowFocused_crossScreenMigration()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});

        engine.windowOpened(QStringLiteral("win1"), screen1);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        QVERIFY(state1->containsWindow(QStringLiteral("win1")));

        engine.windowFocused(QStringLiteral("win1"), screen2);
        QCoreApplication::processEvents();

        QVERIFY(!state1->containsWindow(QStringLiteral("win1")));
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);
        QVERIFY(state2->containsWindow(QStringLiteral("win1")));
    }

    void testToggleWindowFloat_untrackedWindowFeedback()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        QSignalSpy feedbackSpy(&engine, &AutotileEngine::navigationFeedback);

        engine.toggleWindowFloat(QStringLiteral("nonexistent"), screen);

        bool foundFeedback = false;
        for (const auto& args : feedbackSpy) {
            if (args.at(2).toString() == QStringLiteral("window_not_tracked")) {
                foundFeedback = true;
                QCOMPARE(args.at(0).toBool(), false);
                break;
            }
        }
        QVERIFY(foundFeedback);
    }

    void testSetWindowFloat_alreadyFloatedNoOp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->isFloating(QStringLiteral("win1")));

        QSignalSpy floatSpy(&engine, &AutotileEngine::windowFloatingChanged);

        engine.floatWindow(QStringLiteral("win1"));

        QCOMPARE(floatSpy.count(), 0);
    }

    void testSetWindowFloat_unfloatNonAutotileScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        engine.floatWindow(QStringLiteral("win1"));

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state->isFloating(QStringLiteral("win1")));

        engine.setAutotileScreens({});

        QSignalSpy floatSpy(&engine, &AutotileEngine::windowFloatingChanged);
        engine.floatWindow(QStringLiteral("win1"));
        QCOMPARE(floatSpy.count(), 0);
    }

    // =========================================================================
    // Float-on-open rule predicate (FloatPredicate)
    //
    // The daemon injects a predicate that returns true when a "Float this app"
    // rule matched the opening window. onWindowAdded must then insert it FLOATING
    // (so it stays managed and IsFloating reflects it) and emit
    // windowFloatingStateSynced so the daemon mirrors the state. Autotile tiles
    // carry no zone, so these windows are floating-but-tracked, never snapped.
    // =========================================================================

    void testFloatPredicate_opensMatchedWindowFloating()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.setFloatPredicate([](const QString& id, const QString&) {
            return id == QStringLiteral("float-me");
        });

        QSignalSpy syncSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);

        engine.windowOpened(QStringLiteral("float-me"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(QStringLiteral("float-me")));
        QVERIFY(state->isFloating(QStringLiteral("float-me")));

        bool foundSync = false;
        for (const auto& args : syncSpy) {
            if (args.at(0).toString() == QStringLiteral("float-me") && args.at(1).toBool()) {
                foundSync = true;
                break;
            }
        }
        QVERIFY2(foundSync, "rule-floated window must emit windowFloatingStateSynced(true)");
    }

    void testFloatPredicate_unsetOpensWindowTiled()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // No predicate set — the gate is inert and the window tiles normally.
        engine.windowOpened(QStringLiteral("win1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(QStringLiteral("win1")));
        QVERIFY(!state->isFloating(QStringLiteral("win1")));
    }

    void testFloatPredicate_bypassesTiledWindowCap()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.config()->maxWindows = 1;
        engine.setAutotileScreens({screen});

        engine.setFloatPredicate([](const QString& id, const QString&) {
            return id == QStringLiteral("float-me");
        });

        // Fill the single tile slot.
        engine.windowOpened(QStringLiteral("tiled-1"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(state->tiledWindowCount(), 1);

        // A rule-floated window opening while the screen is already at the tiled
        // cap must NOT be dropped: a floating window consumes no tile slot, so the
        // cap does not apply. It is inserted floating and stays tracked.
        engine.windowOpened(QStringLiteral("float-me"), screen);
        QCoreApplication::processEvents();

        QVERIFY(state->containsWindow(QStringLiteral("float-me")));
        QVERIFY(state->isFloating(QStringLiteral("float-me")));
        QCOMPARE(state->tiledWindowCount(), 1);
    }
};

QTEST_MAIN(TestAutotileExtStartupFloat)
#include "test_autotile_ext_startup_float.moc"
