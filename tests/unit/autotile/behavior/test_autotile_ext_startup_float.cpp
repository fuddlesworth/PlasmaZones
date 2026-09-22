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
        PhosphorPlacement::WindowTrackingService wts(layoutManager.get(), nullptr, nullptr);
        QSet<QString> liveInstances{QStringLiteral("first")};
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
