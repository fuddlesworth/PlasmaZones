// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Tests for the IPlacementEngine cross-engine handoff contract on
 *        AutotileEngine: handoffReceive / handoffRelease / screenForTrackedWindow.
 *
 * The handoff API replaces the opportunistic adopt branch in toggleWindowFloat
 * (PR #366) with an explicit two-step transaction the daemon orchestrates.
 * These tests pin the engine-local invariants the daemon depends on:
 *  - receive populates per-state tracking + m_windowToStateKey
 *  - release drops tracking without mutating geometry
 *  - cross-screen receive on the same engine releases prior tracking first
 *  - screenForTrackedWindow follows ownership transfers
 */
class TestAutotileHandoff : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;
    static constexpr auto Screen1 = "eDP-1";
    static constexpr auto Screen2 = "HDMI-1";

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    // =========================================================================
    // handoffReceive — adoption + initial state
    // =========================================================================

    void testHandoffReceive_addsWindowAsFloating()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-floating");
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.fromEngineId = PhosphorEngine::WindowPlacement::snapEngineId();
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(windowId));
        QVERIFY(state->isFloating(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
    }

    void testHandoffReceive_addsWindowAsTiled()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-tiled");
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.fromEngineId = PhosphorEngine::WindowPlacement::autotileEngineId();
        ctx.wasFloating = false;
        engine.handoffReceive(ctx);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(windowId));
        QVERIFY(!state->isFloating(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
    }

    void testHandoffReceive_rejectsNonAutotileScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QStringLiteral("win-x");
        ctx.toScreenId = QLatin1String(Screen2); // not an autotile screen
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);

        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("win-x")), QString());
    }

    void testHandoffReceive_emptyArgsIsNoop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QString();
        ctx.toScreenId = QLatin1String(Screen1);
        engine.handoffReceive(ctx);

        ctx.windowId = QStringLiteral("win-x");
        ctx.toScreenId = QString();
        engine.handoffReceive(ctx);

        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("win-x")), QString());
    }

    // =========================================================================
    // handoffReceive — already-tracked / cross-screen edge cases
    // =========================================================================

    void testHandoffReceive_alreadyTrackedSameScreenIsNoop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-existing");
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();

        // Idempotent receive: floating disposition shouldn't overwrite the
        // existing tiled state.
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(windowId));
        QVERIFY(!state->isFloating(windowId));
    }

    void testHandoffReceive_crossScreenReleasesPreviousState()
    {
        // The PR review identified an orphaned-state bug: receiving a
        // window that's already tracked on a DIFFERENT autotile screen
        // would overwrite m_windowToStateKey to point at the new screen
        // while leaving the old screen's PhosphorTiles::TilingState holding the
        // window. Fix: release the previous state first.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});

        const QString windowId = QStringLiteral("win-cross");
        engine.windowOpened(windowId, s1);
        QCoreApplication::processEvents();

        // Hand off to the OTHER autotile screen.
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = s2;
        ctx.wasFloating = false;
        engine.handoffReceive(ctx);
        QCoreApplication::processEvents();

        // s2 has it.
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(s2);
        QVERIFY(state2);
        QVERIFY(state2->containsWindow(windowId));
        // s1 must NOT have it — that's the orphan we're guarding against.
        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(s1);
        QVERIFY(state1);
        QVERIFY(!state1->containsWindow(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), s2);
    }

    // =========================================================================
    // handoffRelease — drops tracking without mutating geometry
    // =========================================================================

    void testHandoffRelease_clearsTracking()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-rel");
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();
        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);

        engine.handoffRelease(windowId);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(!state->containsWindow(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), QString());
        QVERIFY(!engine.isWindowTracked(windowId));
    }

    void testHandoffRelease_untrackedWindowIsNoop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});

        engine.handoffRelease(QStringLiteral("never-tracked"));
        engine.handoffRelease(QString());
        // Did not crash; nothing tracked.
        QVERIFY(!engine.isWindowTracked(QStringLiteral("never-tracked")));
    }

    // =========================================================================
    // engineId — stable identity used in HandoffContext.fromEngineId
    // =========================================================================

    void testEngineId_returnsAutotile()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QCOMPARE(engine.engineId(), QStringLiteral("autotile"));
    }

    // =========================================================================
    // screenForTrackedWindow — symmetry with snap-side
    // =========================================================================

    void testScreenForTrackedWindow_returnsEmptyForUnknown()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("never-seen")), QString());
    }

    // =========================================================================
    // Cross-engine coordination: a window that opens on an autotile screen but
    // carries a SNAPPED record whose recorded screen is in snapping mode is being
    // cross-restored by snap. Autotile must NOT track or tile it (or both engines
    // claim the same window), and must NOT consume the record (snap is the
    // consumer). Reciprocal of SnapEngine::resolveWindowRestore's recorded-screen
    // ownership gate.
    // =========================================================================

    void testWindowOpened_defersCrossScreenSnapRestoreToSnap()
    {
        // root parents the registry so it outlives the engine/wts that borrow it.
        QObject root;
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        PhosphorZones::LayoutRegistry* layout =
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), &root);
        PlasmaZones::StubZoneDetector zoneDet;
        PhosphorPlacement::WindowTrackingService wts(layout, &zoneDet, nullptr, nullptr);
        AutotileEngine engine(layout, &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());

        layout->setSnappingPreferredProvider([] {
            return true;
        }); // snapping globally enabled

        const QString autotileScreen = QStringLiteral("DP-2");
        const QString snapScreen = QStringLiteral("DP-1"); // snapping by default
        engine.setAutotileScreens({autotileScreen});

        // DP-2 is autotile mode; DP-1 stays snapping (the registry default).
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        layout->setAssignmentEntryDirect(autotileScreen, 0, QString(), autotile);

        // A window snapped on the SNAP monitor DP-1, recorded in the store.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = snapScreen;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        wts.placementStore().record(rec);

        // KWin reopens it (new uuid) on the AUTOTILE monitor DP-2.
        engine.windowOpened(QStringLiteral("app|new"), autotileScreen);
        QCoreApplication::processEvents();

        QVERIFY2(!engine.isWindowTracked(QStringLiteral("app|new")),
                 "autotile must not track a window snap will cross-restore");
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(autotileScreen);
        QVERIFY2(!state || !state->containsWindow(QStringLiteral("app|new")),
                 "the deferred window must not be tiled on the autotile screen");
        QVERIFY2(wts.placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "deferring must not consume the snapped record — snap is the consumer");

        // Control: a window with NO cross-screen snap record IS tracked/tiled.
        engine.windowOpened(QStringLiteral("other|1"), autotileScreen);
        QCoreApplication::processEvents();
        QVERIFY2(engine.isWindowTracked(QStringLiteral("other|1")),
                 "a normal window on the autotile screen must still be tiled");
    }

    void testWindowOpened_globalSnappingDisabled_tilesInsteadOfOrphaning()
    {
        // When snapping is globally disabled, snap's resolveWindowRestore returns
        // early and never claims the window — so autotile must NOT defer, or the
        // window would be stranded unmanaged. Autotile keeps and tiles it.
        QObject root;
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        PhosphorZones::LayoutRegistry* layout =
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), &root);
        PlasmaZones::StubZoneDetector zoneDet;
        PhosphorPlacement::WindowTrackingService wts(layout, &zoneDet, nullptr, nullptr);
        AutotileEngine engine(layout, &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());

        layout->setSnappingPreferredProvider([] {
            return false;
        }); // snapping globally DISABLED

        const QString autotileScreen = QStringLiteral("DP-2");
        engine.setAutotileScreens({autotileScreen});
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        layout->setAssignmentEntryDirect(autotileScreen, 0, QString(), autotile);

        // Snapped record on the (still Snapping-assigned) DP-1.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        wts.placementStore().record(rec);

        engine.windowOpened(QStringLiteral("app|new"), autotileScreen);
        QCoreApplication::processEvents();

        QVERIFY2(engine.isWindowTracked(QStringLiteral("app|new")),
                 "with snapping globally disabled, autotile must tile the window — not defer to a dead snap");
    }

    // =========================================================================
    // Floated-position restore gate (autotileRestoreFloatedWindowsOnLogin /
    // RestorePosition rule, injected by the daemon as a predicate). Mirrors snap:
    // the float STATE is always restored; only the geometry MOVE is gated. The
    // stored free geometry is a literal rect (no QScreen needed), so the gate is
    // directly observable here.
    // =========================================================================

    // Predicate UNSET → historical behaviour: the floated window's recorded
    // position is always re-applied.
    void testFloatRestore_predicateUnset_appliesMove()
    {
        const FloatRestoreOutcome r = runFloatRestoreGate([](AutotileEngine&) { });
        QVERIFY(r.tracked);
        QCOMPARE(r.moveCount, 1);
    }

    // Predicate ALLOWS → the move fires (e.g. autotileRestoreFloatedWindowsOnLogin
    // ON, or a RestorePosition(true) rule).
    void testFloatRestore_predicateAllows_appliesMove()
    {
        const FloatRestoreOutcome r = runFloatRestoreGate([](AutotileEngine& e) {
            e.setRestorePositionPredicate([](const QString&) {
                return true;
            });
        });
        QVERIFY(r.tracked);
        QCOMPARE(r.moveCount, 1);
    }

    // Predicate DENIES → the window still comes back floating, but stays where the
    // compositor placed it: no geometry move (the setting OFF / RestorePosition(false)).
    void testFloatRestore_predicateDenies_skipsMove()
    {
        const FloatRestoreOutcome r = runFloatRestoreGate([](AutotileEngine& e) {
            e.setRestorePositionPredicate([](const QString&) {
                return false;
            });
        });
        // Float MARK is unconditional — only the geometry move is gated off.
        QVERIFY(r.tracked);
        QCOMPARE(r.moveCount, 0);
    }

    // Predicate ALLOWS, but the only recorded free geometry is on a DIFFERENT screen
    // than restoreScreen → the move is skipped (no anyFreeGeometry() cross-screen
    // fallback). The free rect is in global coords; applying another screen's rect
    // while the float tracking points at restoreScreen would teleport the window to a
    // third monitor with the state disagreeing. Mirrors snap's resolveWindowRestore.
    void testFloatRestore_geometryOnOtherScreen_skipsMoveNoCrossScreenFallback()
    {
        QObject root;
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        PhosphorZones::LayoutRegistry* layout =
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), &root);
        PlasmaZones::StubZoneDetector zoneDet;
        PhosphorPlacement::WindowTrackingService wts(layout, &zoneDet, nullptr, nullptr);
        AutotileEngine engine(layout, &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString autotileScreen = QStringLiteral("DP-2");
        engine.setAutotileScreens({autotileScreen});
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        layout->setAssignmentEntryDirect(autotileScreen, 0, QString(), autotile);

        // Floating record with EMPTY screenId (so take() consumes it on any opening
        // screen), and the only recorded rect is on DP-3 — NOT the DP-2 the window
        // reopens on, so restoreScreen (DP-2) has no screen-local rect.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QString();
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        rec.engines.insert(PhosphorEngine::WindowPlacement::autotileEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-3"), QRect(10, 10, 800, 600));
        wts.placementStore().record(rec);

        // Opt-in ON, so the gate is NOT what suppresses the move — only the
        // screen-local resolution is.
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        engine.windowOpened(QStringLiteral("app|new"), autotileScreen);
        QCoreApplication::processEvents();

        // Still restored floating (mark is unconditional)...
        QVERIFY(engine.isWindowTracked(QStringLiteral("app|new")));
        // ...but NO move — DP-3's rect must not be resurrected for a DP-2 restore.
        QCOMPARE(geoSpy.count(), 0);
    }

private:
    struct FloatRestoreOutcome
    {
        int moveCount = 0; // geometryRestoreRequested emissions
        bool tracked = false; // window restored floating (mark is unconditional)
    };

    // Build an autotile engine on DP-2 with a FLOATING record for "app" carrying a
    // stored free rect, then reopen the window. @p wirePredicate controls the
    // injected gate. Returns the geometry-move count + whether the window was
    // tracked (floating mark), so the void test slots can assert both with QVERIFY.
    static FloatRestoreOutcome runFloatRestoreGate(const std::function<void(AutotileEngine&)>& wirePredicate)
    {
        QObject root;
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        PhosphorZones::LayoutRegistry* layout =
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), &root);
        PlasmaZones::StubZoneDetector zoneDet;
        PhosphorPlacement::WindowTrackingService wts(layout, &zoneDet, nullptr, nullptr);
        AutotileEngine engine(layout, &wts, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString autotileScreen = QStringLiteral("DP-2");
        engine.setAutotileScreens({autotileScreen});
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        layout->setAssignmentEntryDirect(autotileScreen, 0, QString(), autotile);

        // A floated autotile record with a stored free rect on the autotile screen.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = autotileScreen;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        rec.engines.insert(PhosphorEngine::WindowPlacement::autotileEngineId(), slot);
        rec.freeGeometryByScreen.insert(autotileScreen, QRect(100, 100, 800, 600));
        wts.placementStore().record(rec);

        wirePredicate(engine);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        engine.windowOpened(QStringLiteral("app|new"), autotileScreen);
        QCoreApplication::processEvents();

        return {static_cast<int>(geoSpy.count()), engine.isWindowTracked(QStringLiteral("app|new"))};
    }
};

QTEST_MAIN(TestAutotileHandoff)
#include "test_autotile_handoff.moc"
