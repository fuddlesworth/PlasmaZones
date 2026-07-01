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
 * moved to monitor B must not re-snap to A's stale zone when later unfloated
 * (e.g. via the snap minimize->unminimize float driver):
 * 5. crossMonitorFloatHandoffClearsStalePreFloat: the cross-engine handoff that
 *    carries a floating window to another monitor clears the source-monitor
 *    pre-float zone/screen.
 * 6. unfloatDoesNotRestoreAcrossMonitors: resolveUnfloatGeometry refuses to
 *    restore to a pre-float zone whose screen differs from the unfloat screen.
 * 7. unfloatRestoresWithinSamePhysicalMonitorAcrossIdForms: the guard compares
 *    by physical monitor (samePhysical), so a virtual-vs-bare id form of the
 *    same monitor still restores rather than being refused.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QRectF>
#include <QGuiApplication>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

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

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsCrossModeFloat(nullptr);
        m_zoneDetector = new StubZoneDetectorCrossModeFloat(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr);
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
    // A window floated in AUTOTILE mode must NOT be floating in SNAPPING mode,
    // and vice versa. The harness wires only the SnapEngine, so we model the
    // two engines' authoritative float stores with in-test maps and drive WTS
    // through the injected resolver/writer exactly as the daemon does. This
    // asserts the WTS contract: isWindowFloating / setWindowFloating route to
    // the engine owning the window's CURRENT mode, with no shared bit.
    // =====================================================================
    void testPerEngineFloatIndependence()
    {
        const QString winA = QStringLiteral("firefox|dddddddd-0000-0000-0000-000000000004");

        // Per-engine float stores, keyed by windowId, modelling SnapState and
        // TilingState. The mode flag selects which one WTS sees as "current".
        QSet<QString> snapFloats;
        QSet<QString> autotileFloats;
        bool autotileMode = false; // start in snapping mode

        m_service->setEngineFloatResolver([&](const QString& w) -> bool {
            return autotileMode ? autotileFloats.contains(w) : snapFloats.contains(w);
        });
        m_service->setEngineFloatWriter([&](const QString& w, bool floating) {
            QSet<QString>& store = autotileMode ? autotileFloats : snapFloats;
            if (floating) {
                store.insert(w);
            } else {
                store.remove(w);
            }
        });

        // Float in SNAPPING mode.
        autotileMode = false;
        m_service->setWindowFloating(winA, true);
        QVERIFY(m_service->isWindowFloating(winA)); // floating in snap
        QVERIFY(snapFloats.contains(winA));
        QVERIFY(!autotileFloats.contains(winA)); // snap float did NOT leak into autotile

        // Switch the window's screen to AUTOTILE mode: it must NOT be floating
        // there — the snap float bit is independent.
        autotileMode = true;
        QVERIFY(!m_service->isWindowFloating(winA)); // not floating in autotile

        // Float it in AUTOTILE mode now.
        m_service->setWindowFloating(winA, true);
        QVERIFY(m_service->isWindowFloating(winA)); // floating in autotile
        QVERIFY(autotileFloats.contains(winA));

        // Back to SNAPPING mode: still floating there from before — autotile
        // float did NOT clear the snap float.
        autotileMode = false;
        QVERIFY(m_service->isWindowFloating(winA));
        QVERIFY(snapFloats.contains(winA));

        // Unfloat in SNAPPING mode: autotile float must SURVIVE.
        m_service->setWindowFloating(winA, false);
        QVERIFY(!m_service->isWindowFloating(winA)); // not floating in snap
        QVERIFY(!snapFloats.contains(winA));
        autotileMode = true;
        QVERIFY(m_service->isWindowFloating(winA)); // still floating in autotile
        QVERIFY(autotileFloats.contains(winA));

        // Clear injected hooks so cleanup() and other tests use the fallback.
        m_service->setEngineFloatResolver({});
        m_service->setEngineFloatWriter({});
    }

    // =====================================================================
    // Test 5 (Discussion #724): cross-MONITOR float handoff clears the stale
    // source-monitor pre-float zone/screen.
    //
    // A window snapped on monitor A, floated (drag-out), then moved to monitor
    // B while floating must not re-snap to A's old zone when later unfloated
    // (the snap minimize->unminimize float driver unfloats on restore). The
    // cross-engine handoff that carries the floating window to B must drop the
    // pre-float zone/screen that named monitor A.
    // =====================================================================
    void testCrossMonitorFloatHandoffClearsStalePreFloat()
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
        ctx.fromEngineId = QStringLiteral("snap");
        ctx.wasFloating = true;
        m_engine->handoffReceive(ctx);

        // The stale pre-float zone/screen (monitor A) must be gone.
        QVERIFY2(m_service->preFloatZone(windowId).isEmpty(),
                 "cross-monitor float handoff must clear the stale pre-float zone");
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // Unfloating on monitor B must therefore NOT restore A's zone.
        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, monitorB);
        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
    }

    // =====================================================================
    // Test 6 (Discussion #724): resolveUnfloatGeometry refuses a cross-monitor
    // restore even if a stale pre-float zone/screen survives.
    //
    // Backstop for move routes that never reach the handoff clear above: with
    // the pre-float state left intact (pointing at monitor A), an unfloat asked
    // for on monitor B must return not-found — no cross-monitor teleport —
    // while a same-monitor unfloat still restores normally.
    // =====================================================================
    void testUnfloatDoesNotRestoreAcrossMonitors()
    {
        const QString windowId = QStringLiteral("dolphin|ffffffff-0000-0000-0000-000000000006");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], monitorA, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorA);

        // Pre-float state intact (monitor A) — an unfloat requested on monitor B
        // must be refused by the cross-monitor guard.
        UnfloatResult crossMonitor = m_engine->resolveUnfloatGeometry(windowId, monitorB);
        QCOMPARE(crossMonitor.found, false);
        QVERIFY(crossMonitor.zoneIds.isEmpty());

        // The guard only blocks a monitor change: a same-monitor unfloat still
        // resolves the pre-float zone. Geometry needs a real QScreen, so gate the
        // positive assertion on availability like the normal-cycle test above.
        UnfloatResult sameMonitor = m_engine->resolveUnfloatGeometry(windowId, monitorA);
        if (QGuiApplication::screens().size() > 0) {
            QVERIFY2(sameMonitor.found, "same-monitor unfloat must still restore the pre-float zone");
            QCOMPARE(sameMonitor.zoneIds, QStringList{m_zoneIds[0]});
        }
    }

    // =====================================================================
    // Test 7 (Discussion #724): the cross-monitor guard compares by PHYSICAL
    // monitor (VirtualScreenId::samePhysical), NOT screensMatch.
    //
    // A window floated on a virtual-screen id ("DP-1/vs:0") and unfloated on the
    // bare physical id ("DP-1") of the SAME monitor must NOT be refused. This
    // pins the samePhysical choice: screensMatch would treat the virtual-vs-bare
    // pair as a monitor change and wrongly refuse the restore, so swapping the
    // guard back to screensMatch would make this test's positive restore fail.
    // =====================================================================
    void testUnfloatRestoresWithinSamePhysicalMonitorAcrossIdForms()
    {
        const QString windowId = QStringLiteral("dolphin|00000000-0000-0000-0000-000000000007");
        const QString virtualId = QStringLiteral("DP-1/vs:0");
        const QString physicalId = QStringLiteral("DP-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], virtualId, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), virtualId);

        // Unfloat on the bare physical id of the same monitor: samePhysical is true,
        // so the guard must NOT refuse. Geometry needs a real QScreen, so gate the
        // positive assertion like the sibling same-monitor case. Under screensMatch
        // this would be refused (found == false) and the assertion would fail.
        UnfloatResult samePhysMonitor = m_engine->resolveUnfloatGeometry(windowId, physicalId);
        if (QGuiApplication::screens().size() > 0) {
            QVERIFY2(samePhysMonitor.found,
                     "unfloat within the same physical monitor (virtual vs bare id) must not be refused");
        }
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
