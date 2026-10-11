// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_crossmode_float.cpp
 * @brief Unit tests for cross-mode float state transitions in WindowTrackingService
 *
 * Tests cover the scenario where stale pre-float snap state was leaking across
 * autotile sessions:
 *
 * 1. preFloatStateClearedOnAutotileFloat: snap -> float -> autotile takes over
 *    -> pre-float state must be cleared
 * 2. crossVsUnfloatDoesNotUseStalePreFloat: snap on VS1 -> float -> autotile
 *    clears pre-float -> unfloat on VS2 must not restore stale state
 * 3. normalSnapFloatUnfloatCyclePreservesState: normal (non-cross-mode) cycle
 *    still works correctly end-to-end
 * 4. perEngineFloatIndependence: a window's float bit in one mode does not
 *    leak into the other mode
 *
 * Cross-MONITOR variants (Discussion #724): a window floated on monitor A then
 * moved to monitor B forgets its home on A, and no unfloat restores across
 * monitors, whatever drives it (see 10-13 for the minimize driver):
 * 5. crossMonitorFloatHandoffForgetsHomeZone: the handoff re-homes the floating
 *    window onto the destination monitor and drops the pre-float zone/screen.
 * 6. unfloatRefusesAHomeOnAnotherMonitor: a stale home naming another monitor
 *    is refused, and a user toggle falls to the fallback-zone tier there.
 * 7. unfloatRestoresWithinSamePhysicalMonitorAcrossIdForms: an id-form difference
 *    (virtual vs bare) of the same monitor still restores.
 * 8. migrateWindowToScreen_leavesTheZoneBehind: the per-monitor migration moves
 *    the reverse map and residence to the destination monitor's store, and the
 *    zone stays behind, unassigned, with the source store's last-used naming it.
 *
 * Phase 4 (per-(screen,desktop,activity) last-used):
 * 9. lastUsedZoneIsPerScreen: last-used zone is tracked per store, so recording it
 *    for a window on monitor A does not disturb monitor B's last-used.
 *
 * Suspension (minimize) unfloats, added for the 3.3.x recurrence of #724,
 * against a stale home a path that does not drop it left behind:
 * 10. suspensionUnfloatConfinedToLiveMonitor: refuses the cross-monitor home,
 *     keeps refusing across the effect's retries, and a USER toggle refuses
 *     it too.
 * 11. suspensionUnfloatSameMonitorStillRestores: a same-monitor round trip is
 *     unaffected.
 * 12. suspensionUnfloatSamePhysicalMonitorAcrossIdForms: the confinement is a
 *     physical-monitor test, so a per-VS home id restores against the bare id.
 * 13. suspensionUnfloatWithFallbackSettingOnStillRefuses: the unconfined
 *     fallback-zone tier must not rescue a refused suspension unfloat.
 */

#include <QTest>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QGuiApplication>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/utils/utils.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PhosphorEngine::UnfloatResult;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

using StubSettingsCrossModeFloat = StubSettings;
using StubZoneDetectorCrossModeFloat = StubZoneDetector;

// =========================================================================
// Test Class
// =========================================================================

class TestWtsCrossModeFloat : public QObject
{
    Q_OBJECT

private:
    // Snap on @p fromScreen, float out (capturing the home zone/screen), then
    // hand the floating window to @p toScreen, which drops that home: the
    // shared preamble of every cross-monitor test here.
    void seedFloatedThenMovedToOtherMonitor(const QString& windowId, const QString& zoneId, const QString& fromScreen,
                                            const QString& toScreen)
    {
        m_service->assignWindowToZone(windowId, zoneId, fromScreen, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), fromScreen);

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = toScreen;
        ctx.fromEngineId = PhosphorEngine::WindowPlacement::snapEngineId();
        ctx.wasFloating = true;
        m_engine->handoffReceive(ctx);
        QCOMPARE(m_engine->screenForTrackedWindow(windowId), toScreen);
        QVERIFY2(m_service->preFloatZones(windowId).isEmpty(), "the move must drop the home on the monitor left");
    }

    // Write a STALE home naming @p homeScreen straight into the window's store:
    // what a cross-monitor path that does not drop the capture leaves behind,
    // which the unfloat refusal is the last line against.
    void seedStaleHome(const QString& windowId, const QString& zoneId, const QString& homeScreen)
    {
        SnapState* state = m_engine->snapState(); // the store the facade reads here
        state->addPreFloatZone(windowId, QStringList{zoneId});
        state->addPreFloatScreen(windowId, homeScreen);
        QCOMPARE(m_service->preFloatScreen(windowId), homeScreen);
    }

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsCrossModeFloat(nullptr);
        m_zoneDetector = new StubZoneDetectorCrossModeFloat(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapState(m_engine->snapState());
        m_service->setSnapEngine(m_engine);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
    }

    void cleanup()
    {
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // Fixture sanity: several slots below gate their load-bearing assertions
    // on a real QScreen being available (zone geometry cannot resolve without
    // one). Pin that precondition explicitly so those gates can never turn
    // into silent no-op passes — mirrors testFixture_zoneGeometryIsValid in
    // the sibling test_snap_unfloat_fallback.cpp.
    void testFixture_zoneGeometryIsValid()
    {
        QVERIFY2(!QGuiApplication::screens().isEmpty(), "the screen-gated assertions below require a real QScreen");
        QVERIFY2(m_service->zoneGeometry(m_zoneIds[0], QStringLiteral("DP-1")).isValid(),
                 "fixture must resolve valid zone geometry, or the gated assertions are meaningless");
    }

    // =====================================================================
    // Test 1: Pre-float state cleared when autotile takes over
    // =====================================================================

    void testPreFloatStateClearedOnAutotileFloat()
    {
        const QString windowId = QStringLiteral("firefox|aaaaaaaa-0000-0000-0000-000000000001");
        const QString screenId = QStringLiteral("DP-1/vs:0");

        // Step 1: Snap window to zone on VS1
        m_service->assignWindowToZone(windowId, m_zoneIds[0], screenId, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[0]);

        // Step 2: Float the window (saves pre-float state)
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));

        // Step 3: Verify pre-float zone was saved
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);
        QCOMPARE(m_service->preFloatScreen(windowId), screenId);

        // Step 4: Simulate autotile taking over the window —
        // clear stale pre-float snap state (autotile-float marking now on AutotileEngine)
        m_service->clearPreFloatZone(windowId);

        // Step 5: Verify pre-float zone is now empty
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // Step 6: resolveUnfloatGeometry should return found=false
        // because there is no pre-float zone to restore to
        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, screenId);
        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
    }

    // =====================================================================
    // Test 2: Cross-VS unfloat does not use stale pre-float state
    // =====================================================================

    void testCrossVsUnfloatDoesNotUseStalePreFloat()
    {
        const QString windowId = QStringLiteral("konsole|bbbbbbbb-0000-0000-0000-000000000002");
        const QString vs0 = QStringLiteral("screen1/vs:0");
        const QString vs1 = QStringLiteral("screen1/vs:1");

        // Step 1: Assign window to zone on VS0
        m_service->assignWindowToZone(windowId, m_zoneIds[1], vs0, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Step 2: Float (saves pre-float state)
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        // Step 3: Verify preFloatScreen is VS0
        QCOMPARE(m_service->preFloatScreen(windowId), vs0);
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[1]);

        // Step 4: Simulate autotile takeover — clears pre-float state
        m_service->clearPreFloatZone(windowId);

        // Step 5: Verify pre-float data is gone
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // Step 6: Attempt unfloat on a different VS — must return found=false
        // because autotile cleared the stale pre-float state
        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, vs1);
        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
    }

    // =====================================================================
    // Test 3: Normal snap float/unfloat cycle preserves state correctly
    // =====================================================================

    void testNormalSnapFloatUnfloatCyclePreservesState()
    {
        // The zone-geometry assertions below need a real QScreen. Under the
        // offscreen QPA the suite runs with there is always one, so this never
        // fires — but without it a screenless environment would report the
        // test as PASSED having asserted nothing, which is worse than skipped.
        if (QGuiApplication::screens().isEmpty()) {
            QSKIP("no QScreen available — this case needs real screen geometry");
        }
        const QString windowId = QStringLiteral("dolphin|cccccccc-0000-0000-0000-000000000003");
        const QString screenId = QStringLiteral("DP-1");

        // Step 1: Assign window to zone
        m_service->assignWindowToZone(windowId, m_zoneIds[2], screenId, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[2]);

        // Step 2: Float window (saves pre-float state via unsnapForFloat)
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));
        QVERIFY(!m_service->isWindowSnapped(windowId));

        // Step 3: Verify pre-float zone is preserved (no autotile interference)
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[2]);
        QCOMPARE(m_service->preFloatScreen(windowId), screenId);

        // Step 4: resolveUnfloatGeometry should find the saved zone when a real
        // QScreen is available. In headless tests, resolveZoneGeometry returns an
        // invalid QRect because there is no physical screen, so found stays false.
        // Gate the full assertion on screen availability; in both cases the
        // pre-float state in the service must remain intact (resolve is read-only).
        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, screenId);
        if (QGuiApplication::screens().size() > 0) {
            QVERIFY2(result.found,
                     "resolveUnfloatGeometry should find pre-float state after snap->float->unfloat cycle");
            QCOMPARE(result.zoneIds, QStringList{m_zoneIds[2]});
            QCOMPARE(result.screenId, screenId);
            QVERIFY(result.geometry.isValid());
        }
        // The pre-float state should still be intact (resolve is read-only)
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[2]);

        // Step 5: Simulate unfloat consuming the state
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);

        // Step 6: Verify clean state — no lingering pre-float data
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // resolveUnfloatGeometry should now return found=false
        UnfloatResult result2 = m_engine->resolveUnfloatGeometry(windowId, screenId);
        QCOMPARE(result2.found, false);
    }

    // =====================================================================
    // Test 4: Per-engine float independence (root fix for the shared-bit defect)
    //
    // WHAT THIS PINS, precisely: that WindowTrackingService itself holds NO
    // float bit of its own and answers purely by delegating to the injected
    // resolver/writer. The three stores below are the TEST's, not the
    // daemon's — this harness wires only a SnapEngine, so the daemon's real
    // mode-routing resolver is not under test here and a bug in THAT would
    // not fail this. What would fail it is any shared or cached bit inside
    // WTS: the float-in-Snapping-then-read-in-Scrolling step reads back
    // false only because the read reached the resolver.
    //
    // Modelled with three modes rather than two because a two-mode bool
    // cannot distinguish "delegates per mode" from "delegates to one of two
    // fixed stores", which is the shape the pre-per-engine model had.
    // =====================================================================
    void testPerEngineFloatIndependence()
    {
        const QString winA = QStringLiteral("firefox|dddddddd-0000-0000-0000-000000000004");

        enum class Mode {
            Snapping,
            Autotile,
            Scrolling
        };
        QHash<int, QSet<QString>> floatsByMode;
        Mode mode = Mode::Snapping;

        m_service->setEngineFloatResolver([&](const QString& w) -> bool {
            return floatsByMode.value(static_cast<int>(mode)).contains(w);
        });
        m_service->setEngineFloatWriter([&](const QString& w, bool floating) {
            QSet<QString>& store = floatsByMode[static_cast<int>(mode)];
            if (floating) {
                store.insert(w);
            } else {
                store.remove(w);
            }
        });

        const auto floatsIn = [&](Mode m) {
            return floatsByMode.value(static_cast<int>(m)).contains(winA);
        };

        // Float in SNAPPING mode. Neither tiling-family engine sees it.
        mode = Mode::Snapping;
        m_service->setWindowFloating(winA, true);
        QVERIFY(m_service->isWindowFloating(winA));
        QVERIFY(floatsIn(Mode::Snapping));
        QVERIFY(!floatsIn(Mode::Autotile));
        QVERIFY(!floatsIn(Mode::Scrolling));

        // The window's screen flips to each tiling mode in turn: the snap
        // float is invisible from both.
        mode = Mode::Autotile;
        QVERIFY(!m_service->isWindowFloating(winA));
        mode = Mode::Scrolling;
        QVERIFY(!m_service->isWindowFloating(winA));

        // Float it in SCROLLING mode. This must not disturb the other two,
        // and in particular must not be readable as an autotile float — the
        // two tiling-family engines are as independent of each other as
        // either is of snapping.
        m_service->setWindowFloating(winA, true);
        QVERIFY(m_service->isWindowFloating(winA));
        QVERIFY(floatsIn(Mode::Scrolling));
        QVERIFY(!floatsIn(Mode::Autotile));
        mode = Mode::Autotile;
        QVERIFY(!m_service->isWindowFloating(winA));

        // Float it in AUTOTILE mode too; all three now hold their own bit.
        m_service->setWindowFloating(winA, true);
        QVERIFY(floatsIn(Mode::Snapping));
        QVERIFY(floatsIn(Mode::Autotile));
        QVERIFY(floatsIn(Mode::Scrolling));

        // Unfloat in SNAPPING mode: the other two must SURVIVE.
        mode = Mode::Snapping;
        m_service->setWindowFloating(winA, false);
        QVERIFY(!m_service->isWindowFloating(winA));
        QVERIFY(!floatsIn(Mode::Snapping));
        mode = Mode::Autotile;
        QVERIFY(m_service->isWindowFloating(winA));
        mode = Mode::Scrolling;
        QVERIFY(m_service->isWindowFloating(winA));

        // Unfloat in SCROLLING mode: autotile still floats.
        m_service->setWindowFloating(winA, false);
        QVERIFY(!floatsIn(Mode::Scrolling));
        QVERIFY(floatsIn(Mode::Autotile));

        // Clear injected hooks so cleanup() and other tests use the fallback.
        m_service->setEngineFloatResolver({});
        m_service->setEngineFloatWriter({});
    }

    // =====================================================================
    // Test 5: a cross-MONITOR float handoff re-homes the window onto the
    // destination monitor's store and FORGETS the pre-float zone/screen.
    //
    // A window snapped on monitor A, floated, then moved to monitor B while
    // floating has left the zone it floated from. An unfloat on B must never
    // throw it back to A, and with the home dropped there is nothing to
    // restore anywhere: the window is placed afresh, or stays floating.
    // =====================================================================
    void testCrossMonitorFloatHandoffForgetsHomeZone()
    {
        const QString windowId = QStringLiteral("dolphin|eeeeeeee-0000-0000-0000-000000000005");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Snap on monitor A, then float — saves the pre-float zone/screen for A.
        m_service->assignWindowToZone(windowId, m_zoneIds[0], monitorA, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorA);
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);

        // Move the floating window to monitor B via the cross-engine handoff.
        // Empty sourceZoneIds + wasFloating enters the floating branch of
        // SnapEngine::handoffReceive.
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = monitorB;
        ctx.fromEngineId = PhosphorEngine::WindowPlacement::snapEngineId();
        ctx.wasFloating = true;
        m_engine->handoffReceive(ctx);

        // The home on monitor A is gone; the window now lives on monitor B.
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_engine->isFloating(windowId));
        QCOMPARE(m_engine->screenForTrackedWindow(windowId), monitorB);

        // Nothing to restore, on B or back on A.
        QVERIFY(!m_engine->resolveUnfloatGeometry(windowId, monitorB).found);
        QVERIFY(!m_engine->resolveUnfloatGeometry(windowId, monitorA).found);
    }

    // =====================================================================
    // Test 8 (Discussion #724): the per-monitor migration MECHANISM.
    //
    // Acceptance test for SnapEngine::migrateWindowToScreen: a window snapped on
    // monitor A moves to monitor B's per-(screen,desktop,activity) store, the
    // reverse map re-points at B, and its live screen (screenForTrackedWindow,
    // the unfloat cross-monitor guard's input) reflects B. The zone names a zone
    // of A's layout, so it is NOT carried: it is unassigned on A, and A's
    // last-used naming it goes with it while B's own last-used is untouched.
    // =====================================================================
    void testMigrateWindowToScreen_leavesTheZoneBehind()
    {
        const QString windowId = QStringLiteral("konsole|dddddddd-0000-0000-0000-000000000009");
        const QString neighbour = QStringLiteral("dolphin|dddddddd-0000-0000-0000-000000000010");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Place the window into monitor A's per-key store (registers the reverse map).
        SnapState* stateA = m_engine->stateForWindowOnScreen(windowId, monitorA);
        QVERIFY(stateA);
        stateA->assignWindowToZone(windowId, m_zoneIds[0], monitorA, 1);
        stateA->restoreLastUsedZone(m_zoneIds[0], monitorA, QString(), 1);
        QVERIFY(stateA->isWindowSnapped(windowId));
        QCOMPARE(stateA->screenId(), monitorA);
        QCOMPARE(m_engine->stateForWindow(windowId), stateA);
        // B's store remembers the same zone id as ITS last-used (a shared layout).
        SnapState* stateBSeed = m_engine->stateForWindowOnScreen(neighbour, monitorB);
        QVERIFY(stateBSeed);
        stateBSeed->restoreLastUsedZone(m_zoneIds[0], monitorB, QString(), 1);

        // Migrate to monitor B.
        QVERIFY(m_engine->migrateWindowToScreen(windowId, monitorB));

        // The reverse map now resolves to B's store, and the window resides there.
        SnapState* stateB = m_engine->stateForWindow(windowId);
        QVERIFY(stateB);
        QVERIFY(stateB != stateA);
        QCOMPARE(stateB, stateBSeed);
        QCOMPARE(stateB->screenId(), monitorB);
        QVERIFY(!stateB->isWindowSnapped(windowId));
        QVERIFY(stateB->zonesForWindow(windowId).isEmpty());
        QCOMPARE(stateB->screenForWindow(windowId), monitorB);
        QCOMPARE(stateB->desktopForWindow(windowId), 1);
        QCOMPARE(stateB->lastUsedZoneId(), m_zoneIds[0]);

        // The source store no longer holds the window, nor a last-used naming its zone.
        QVERIFY(!stateA->isWindowSnapped(windowId));
        QVERIFY(stateA->screenForWindow(windowId).isEmpty());
        QVERIFY(stateA->lastUsedZoneId().isEmpty());

        // screenForTrackedWindow (the guard input) reflects the destination monitor.
        QCOMPARE(m_engine->screenForTrackedWindow(windowId), monitorB);

        // Re-migrating to the same monitor is a no-op.
        QVERIFY(!m_engine->migrateWindowToScreen(windowId, monitorB));
    }

    // =====================================================================
    // Test 6: no unfloat restores a home on another monitor. The refusal is
    // cause-independent: a USER toggle refuses it too, and then falls to the
    // fallback-zone tier, which places the window on the monitor it is on when
    // that setting is on and otherwise keeps it floating. The comparison is
    // against the screen the EFFECT threads in as the live output (see
    // SnapEngine::setWindowFloat), not a tracked association.
    // =====================================================================
    void testUnfloatRefusesAHomeOnAnotherMonitor()
    {
        // See testNormalSnapFloatUnfloatCyclePreservesState.
        if (QGuiApplication::screens().isEmpty()) {
            QSKIP("no QScreen available — this case needs real screen geometry");
        }
        const QString windowId = QStringLiteral("dolphin|ffffffff-0000-0000-0000-000000000006");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], monitorA, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorA);

        // On monitor B the home on A is refused; on A it restores.
        QVERIFY2(!m_engine->resolveUnfloatGeometry(windowId, monitorB).found,
                 "an unfloat must not restore a home on another monitor");
        UnfloatResult sameMonitor = m_engine->resolveUnfloatGeometry(windowId, monitorA);
        QVERIFY2(sameMonitor.found, "same-monitor unfloat must restore the pre-float zone");
        QCOMPARE(sameMonitor.zoneIds, QStringList{m_zoneIds[0]});

        // A user toggle on B with the fallback setting off keeps it floating.
        QSignalSpy applySpy(m_engine, &SnapEngine::applyGeometryRequested);
        m_engine->setWindowFloat(windowId, false, monitorB);
        QCOMPARE(applySpy.count(), 0);
        QVERIFY(m_engine->isFloating(windowId));

        // With it on, the refusal falls through to the fallback-zone tier and
        // the window is placed afresh, never back into its home zone on A.
        // (Which screen the fallback resolves on is that tier's own contract,
        // pinned in test_snap_unfloat_fallback; HDMI-1 is no live output here.)
        m_settings->setSnapUnfloatFallbackToZone(true);
        m_engine->setWindowFloat(windowId, false, monitorB);
        m_settings->setSnapUnfloatFallbackToZone(false);
        QCOMPARE(applySpy.count(), 1);
        QVERIFY(!m_engine->isFloating(windowId));
    }

    // =====================================================================
    // Test 7 (Discussion #724): unfloat restores across virtual/bare id forms of
    // the same physical monitor (the id-form difference must never block a restore).
    // =====================================================================
    void testUnfloatRestoresWithinSamePhysicalMonitorAcrossIdForms()
    {
        // See testNormalSnapFloatUnfloatCyclePreservesState.
        if (QGuiApplication::screens().isEmpty()) {
            QSKIP("no QScreen available — this case needs real screen geometry");
        }
        const QString windowId = QStringLiteral("dolphin|00000000-0000-0000-0000-000000000007");
        const QString virtualId = QStringLiteral("DP-1/vs:0");
        const QString physicalId = QStringLiteral("DP-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], virtualId, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), virtualId);

        // Unfloat on the bare physical id of the same monitor still restores the
        // home zone. Geometry needs a real QScreen, so gate the positive assertion.
        UnfloatResult samePhysMonitor = m_engine->resolveUnfloatGeometry(windowId, physicalId);
        if (QGuiApplication::screens().size() > 0) {
            QVERIFY2(samePhysMonitor.found,
                     "unfloat within the same physical monitor (virtual vs bare id) must restore");
            // Pin the restored zone: the id-form difference must resolve the SAME home
            // zone, not merely produce some result.
            QCOMPARE(samePhysMonitor.zoneIds, QStringList{m_zoneIds[0]});
        }
    }

    // =====================================================================
    // Test 10 (Discussion #724, 3.3.x regression): a SUSPENSION (minimize)
    // unfloat is confined to the window's live monitor. A window snapped on
    // monitor A, floated, moved to monitor B, then minimized and unminimized
    // ON B must NOT teleport back to A's zone. The move drops the home, so the
    // stale one is seeded directly, as a path that does not drop it leaves it.
    // The refusal leaves it in place, and a USER toggle refuses it too.
    // =====================================================================
    void testSuspensionUnfloatConfinedToLiveMonitor()
    {
        const QString windowId = QStringLiteral("kate|aaaaaaaa-0000-0000-0000-000000000010");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Snap on A, float out, move to B while floating, and leave a stale
        // home on A behind: the state the confinement defends against.
        seedFloatedThenMovedToOtherMonitor(windowId, m_zoneIds[0], monitorA, monitorB);
        seedStaleHome(windowId, m_zoneIds[0], monitorA);

        // Minimize classifies the float as a suspension; the unminimize unfloat
        // on B must refuse the cross-monitor home and keep the window floating
        // exactly where it is.
        m_service->markSuspensionFloat(windowId);
        m_engine->setWindowFloat(windowId, false, monitorB);
        QVERIFY2(m_engine->isFloating(windowId),
                 "a suspension unfloat must not restore a home zone on another monitor — keep floating");
        QVERIFY2(m_engine->stateForWindow(windowId)->zonesForWindow(windowId).isEmpty(),
                 "the suspension unfloat must not have assigned any zone");

        // RETRY INVARIANT: the effect re-drives the unminimize unfloat (up to
        // three times, 250 ms apart) whenever the window still reads floating.
        // Every retry must refuse identically — a retry that ran unconfined
        // would teleport the window a quarter-second after the unminimize,
        // which is exactly how this defect survived its first fix.
        for (int retry = 0; retry < 3; ++retry) {
            m_engine->setWindowFloat(windowId, false, monitorB);
            QVERIFY2(m_engine->isFloating(windowId), "every suspension unfloat retry must keep refusing");
            QVERIFY2(m_engine->stateForWindow(windowId)->zonesForWindow(windowId).isEmpty(),
                     "no retry may assign a cross-monitor zone");
        }

        // The refusal leaves the capture alone; it is not the refusal's to drop.
        QCOMPARE(m_service->preFloatScreen(windowId), monitorA);
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);

        // A USER unfloat (suspension cleared) refuses the cross-monitor home
        // as well, driven through the real setWindowFloat gate. The emitted
        // geometry request is the discriminator: a refusal with the fallback
        // setting off returns before any commit and emits nothing.
        m_service->clearSuspensionFloat(windowId);
        QSignalSpy applySpy(m_engine, &SnapEngine::applyGeometryRequested);
        m_engine->setWindowFloat(windowId, false, monitorB);
        QCOMPARE(applySpy.count(), 0);
        QVERIFY(m_engine->isFloating(windowId));
    }

    // =====================================================================
    // Test 11 (Discussion #724): the suspension confinement only blocks CROSS-
    // monitor restores — a same-monitor minimize/unminimize round trip still
    // restores the pre-float zone.
    // =====================================================================
    void testSuspensionUnfloatSameMonitorStillRestores()
    {
        const QString windowId = QStringLiteral("kate|bbbbbbbb-0000-0000-0000-000000000011");
        const QString monitorA = QStringLiteral("DP-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[1], monitorA, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        m_service->markSuspensionFloat(windowId);

        m_engine->setWindowFloat(windowId, false, monitorA);
        QVERIFY2(!m_engine->isFloating(windowId), "a same-monitor suspension unfloat must restore the pre-float zone");
        QCOMPARE(m_engine->stateForWindow(windowId)->zonesForWindow(windowId), QStringList{m_zoneIds[1]});
    }

    // =====================================================================
    // Test 12 (Discussion #724): the confinement is a PHYSICAL-monitor test,
    // not a string compare — a per-virtual-screen home id restores against the
    // bare physical id of the same monitor. Without this, every same-monitor
    // unminimize on a VS-subdivided screen would be refused.
    // =====================================================================
    void testSuspensionUnfloatSamePhysicalMonitorAcrossIdForms()
    {
        const QString windowId = QStringLiteral("kate|cccccccc-0000-0000-0000-000000000012");
        const QString virtualId = QStringLiteral("DP-1/vs:0");
        const QString physicalId = QStringLiteral("DP-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[1], virtualId, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), virtualId);
        m_service->markSuspensionFloat(windowId);

        m_engine->setWindowFloat(windowId, false, physicalId);
        QVERIFY2(!m_engine->isFloating(windowId),
                 "a virtual-vs-bare id difference on ONE monitor must not read as cross-monitor");
        QCOMPARE(m_engine->stateForWindow(windowId)->zonesForWindow(windowId), QStringList{m_zoneIds[1]});
    }

    // =====================================================================
    // Test 13 (Discussion #724): the suspension refusal must hold even when
    // the unfloat-fallback setting is on. The fallback tier resolves against
    // the window's TRACKED screen and is unconfined, so a suspension unfloat
    // that fell through to it would snap the window into a fallback zone —
    // a different route to the same cross-monitor teleport.
    // =====================================================================
    void testSuspensionUnfloatWithFallbackSettingOnStillRefuses()
    {
        const QString windowId = QStringLiteral("kate|dddddddd-0000-0000-0000-000000000013");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        m_settings->setSnapUnfloatFallbackToZone(true);
        seedFloatedThenMovedToOtherMonitor(windowId, m_zoneIds[0], monitorA, monitorB);
        seedStaleHome(windowId, m_zoneIds[0], monitorA);

        m_service->markSuspensionFloat(windowId);
        m_engine->setWindowFloat(windowId, false, monitorB);

        QVERIFY2(m_engine->isFloating(windowId),
                 "the fallback tier must not rescue a refused suspension unfloat — keep floating");
        QVERIFY2(m_engine->stateForWindow(windowId)->zonesForWindow(windowId).isEmpty(),
                 "a suspension unfloat must never acquire a fallback zone");
        m_settings->setSnapUnfloatFallbackToZone(false);
    }

    // =====================================================================
    // Phase 4 (Discussion #724): last-used zone is PER-(screen,desktop,activity),
    // not one global scalar. Recording a last-used zone for a window snapped on
    // monitor A must not disturb monitor B's last-used, and vice versa. The
    // facade's screen-agnostic getter returns the most-recently updated store as
    // the single representative persisted to disk.
    // =====================================================================
    void testLastUsedZoneIsPerScreen()
    {
        // Wire the FULL per-key resolver (the single-store convenience used by the
        // fixture routes everything to the global holder, which would hide the
        // per-screen split this test exercises).
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));

        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Record a last-used zone on A, then a different one on B.
        m_service->updateLastUsedZone(m_zoneIds[0], monitorA, QStringLiteral("app"), 0);
        m_service->updateLastUsedZone(m_zoneIds[1], monitorB, QStringLiteral("app"), 0);

        auto* stateA = static_cast<SnapState*>(m_engine->stateForScreen(monitorA));
        auto* stateB = static_cast<SnapState*>(m_engine->stateForScreen(monitorB));
        QVERIFY(stateA);
        QVERIFY(stateB);
        QVERIFY(stateA != stateB);

        // Each screen keeps its OWN last-used; B's update did not overwrite A's.
        QCOMPARE(stateA->lastUsedZoneId(), m_zoneIds[0]);
        QCOMPARE(stateA->lastUsedScreenId(), monitorA);
        QCOMPARE(stateB->lastUsedZoneId(), m_zoneIds[1]);
        QCOMPARE(stateB->lastUsedScreenId(), monitorB);

        // The facade representative is the most-recently updated store (B).
        QCOMPARE(m_service->lastUsedZoneId(), m_zoneIds[1]);
        QCOMPARE(m_service->lastUsedScreenName(), monitorB);

        // Updating A again makes A the representative without touching B.
        m_service->updateLastUsedZone(m_zoneIds[2], monitorA, QStringLiteral("app"), 0);
        QCOMPARE(stateB->lastUsedZoneId(), m_zoneIds[1]);
        QCOMPARE(m_service->lastUsedZoneId(), m_zoneIds[2]);
        QCOMPARE(m_service->lastUsedScreenName(), monitorA);

        // Restore the fixture's single-store wiring for the shared teardown.
        m_service->setSnapState(m_engine->snapState());
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsCrossModeFloat* m_settings = nullptr;
    StubZoneDetectorCrossModeFloat* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsCrossModeFloat)
#include "test_wts_crossmode_float.moc"
