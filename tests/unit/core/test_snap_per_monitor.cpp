// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_per_monitor.cpp
 * @brief End-to-end acceptance tests for the per-monitor snap model (Discussion #724).
 *
 * The prior phases made snap per-(screen,desktop,activity) via PerScreenStates<SnapState>
 * and threaded the unfloat screen deterministically. These tests pin the whole-effort
 * behaviour rather than the individual mechanisms (the mechanism-level cases live in
 * test_wts_crossmode_float.cpp — migrateWindowToScreen, last-used-per-screen, the
 * handoff guard):
 *
 * 1. e2eDeterministicUnfloatAcrossMonitors: the acceptance scenario for the entire
 *    effort. Snap on B, float, migrate to A → the window reports A, the pre-float still
 *    names B (behaviour A), an unfloat asked on A is refused (no teleport back to B), and
 *    migrating back to B restores the original B zone.
 * 2. threadedUnfloatScreenNeverTeleports: SnapEngine::setWindowFloat(id, false, A) with
 *    a stale tracked screen (B) resolves against the PASSED screen A deterministically —
 *    it never re-snaps the window to B's zone.
 * 3. perMonitorSnapIndependence: two windows snapped on two monitors keep independent
 *    per-screen state; floating one does not disturb the other, and same-index zones on
 *    different monitors do not collide.
 * 4. migrationMovesAllPerWindowFields: SnapState::migrateWindowTo carries zone, screen,
 *    desktop, floating bit, pre-float zone/screen and the auto-snap flag to the
 *    destination store and leaves none behind in the source.
 */

#include <QGuiApplication>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"
#include "core/utils.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorEngine::UnfloatResult;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSnapPerMonitor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        // Default single-store wiring; per-key tests call installFullResolver().
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

    // =====================================================================
    // Test 1 (Discussion #724): the full acceptance scenario.
    //
    // A window snapped on monitor B, floated, then re-homed to monitor A via the
    // migration analogue of the activation/drift path must never teleport back to
    // B's zone when unfloated on A — and must still restore B's zone when unfloated
    // back on B. This is the end-to-end determinism the entire effort exists for.
    // =====================================================================
    void e2eDeterministicUnfloatAcrossMonitors()
    {
        installFullResolver();

        const QString windowId = QStringLiteral("firefox|11111111-0000-0000-0000-000000000001");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Snap on monitor B, then float — pre-float becomes (zone0, B).
        m_service->assignWindowToZone(windowId, m_zoneIds[0], monitorB, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->screenForWindow(windowId), monitorB);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorB);

        // The drift/activation analogue: re-home the floating window to monitor A.
        QVERIFY(m_engine->migrateWindowToScreen(windowId, monitorA));

        // (a) The window now lives on A.
        QCOMPARE(m_service->screenForWindow(windowId), monitorA);
        QCOMPARE(m_engine->screenForTrackedWindow(windowId), monitorA);

        // (b) The pre-float home zone/screen still name B (behaviour A: preserved).
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorB);

        // (c) Unfloating on A is refused by the cross-monitor guard — no teleport to B.
        UnfloatResult onA = m_engine->resolveUnfloatGeometry(windowId, monitorA);
        QCOMPARE(onA.found, false);
        QVERIFY(onA.zoneIds.isEmpty());

        // (d) Migrating back to B and unfloating there restores the original B zone.
        QVERIFY(m_engine->migrateWindowToScreen(windowId, monitorB));
        QCOMPARE(m_service->screenForWindow(windowId), monitorB);
        UnfloatResult onB = m_engine->resolveUnfloatGeometry(windowId, monitorB);
        if (QGuiApplication::screens().size() > 0) {
            QVERIFY2(onB.found, "unfloat back on the original monitor must restore the pre-float zone");
            QCOMPARE(onB.zoneIds, QStringList{m_zoneIds[0]});
        }
        // The pre-float bookkeeping still names B regardless of screen availability.
        QCOMPARE(m_service->preFloatScreen(windowId), monitorB);

        restoreFixtureWiring();
    }

    // =====================================================================
    // Test 2 (Discussion #724): the threaded unfloat screen is authoritative.
    //
    // Regardless of a window's (possibly stale) tracked screen, an unfloat driven
    // with an explicit screen must resolve against THAT screen. Here the tracked
    // screen still says B (no windowScreenChanged was ever seen for the drift), yet
    // setWindowFloat(id, false, A) must not re-snap the window to B's zone.
    // =====================================================================
    void threadedUnfloatScreenNeverTeleports()
    {
        installFullResolver();

        const QString windowId = QStringLiteral("konsole|22222222-0000-0000-0000-000000000002");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Snap on B and float: pre-float (zone0, B); the tracked screen stays B
        // (unsnapForFloat preserves it) — this is the stale value a drifted window
        // carries because the daemon never observed the monitor change.
        m_service->assignWindowToZone(windowId, m_zoneIds[0], monitorB, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_engine->screenForTrackedWindow(windowId), monitorB);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorB);

        // Drive the unfloat with the effect's authoritative live screen (A). The
        // threaded screen is preferred over the stale tracked screen, so the
        // cross-monitor guard refuses B's zone: no applyGeometryRequested at all.
        QSignalSpy applySpy(m_engine, &SnapEngine::applyGeometryRequested);
        m_engine->setWindowFloat(windowId, false, monitorA);

        QCOMPARE(applySpy.count(), 0); // no snap commit → no teleport to B
        QVERIFY(m_service->isWindowFloating(windowId)); // kept floating, not limbo
        QVERIFY(!m_service->isWindowSnapped(windowId));
        // The pre-float state is untouched (resolve is read-only under refusal).
        QCOMPARE(m_service->preFloatScreen(windowId), monitorB);
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);

        restoreFixtureWiring();
    }

    // =====================================================================
    // Test 3 (Discussion #724): per-monitor snap independence.
    //
    // Two windows snapped on two monitors own two separate per-(screen,desktop,
    // activity) stores. Their zone/screen state is independent, floating one does
    // not perturb the other, and the SAME zone index snapped on both monitors does
    // not collide (each store tracks its own occupancy).
    // =====================================================================
    void perMonitorSnapIndependence()
    {
        installFullResolver();

        const QString winA = QStringLiteral("firefox|33333333-0000-0000-0000-000000000003");
        const QString winB = QStringLiteral("dolphin|44444444-0000-0000-0000-000000000004");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Same zone INDEX (zone0) on both monitors — must not collide.
        m_service->assignWindowToZone(winA, m_zoneIds[0], monitorA, 1);
        m_service->assignWindowToZone(winB, m_zoneIds[0], monitorB, 1);

        // Each window reports its own monitor.
        QCOMPARE(m_service->screenForWindow(winA), monitorA);
        QCOMPARE(m_service->screenForWindow(winB), monitorB);

        // The two live in separate stores.
        SnapState* stateA = m_engine->stateForWindow(winA);
        SnapState* stateB = m_engine->stateForWindow(winB);
        QVERIFY(stateA);
        QVERIFY(stateB);
        QVERIFY(stateA != stateB);
        QCOMPARE(stateA->screenId(), monitorA);
        QCOMPARE(stateB->screenId(), monitorB);

        // Per-store snapped sets and occupancy are each single-window / single-zone.
        QCOMPARE(stateA->snappedWindows(), QStringList{winA});
        QCOMPARE(stateB->snappedWindows(), QStringList{winB});
        QCOMPARE(stateA->buildOccupiedZoneSet(), (QSet<QString>{m_zoneIds[0]}));
        QCOMPARE(stateB->buildOccupiedZoneSet(), (QSet<QString>{m_zoneIds[0]}));

        // The facade aggregate sees both.
        const QStringList allSnapped = m_service->snappedWindows();
        QCOMPARE(allSnapped.size(), 2);
        QVERIFY(allSnapped.contains(winA));
        QVERIFY(allSnapped.contains(winB));

        // Float winA: winB is undisturbed.
        m_service->unsnapForFloat(winA);
        m_service->setWindowFloating(winA, true);
        QVERIFY(m_service->isWindowFloating(winA));
        QVERIFY(!m_service->isWindowSnapped(winA));

        QVERIFY(m_service->isWindowSnapped(winB));
        QVERIFY(!m_service->isWindowFloating(winB));
        QCOMPARE(m_service->screenForWindow(winB), monitorB);
        QCOMPARE(m_service->zoneForWindow(winB), m_zoneIds[0]);
        QCOMPARE(stateB->snappedWindows(), QStringList{winB});
        QCOMPARE(stateB->buildOccupiedZoneSet(), (QSet<QString>{m_zoneIds[0]}));

        // The aggregate now lists only the still-snapped window.
        QCOMPARE(m_service->snappedWindows(), QStringList{winB});
        // winA's own store no longer counts it as snapped.
        QVERIFY(!stateA->isWindowSnapped(winA));

        restoreFixtureWiring();
    }

    // =====================================================================
    // Test 4 (Discussion #724): migration moves EVERY per-window field.
    //
    // SnapState::migrateWindowTo must carry the window's zone assignment, live
    // screen (rewritten to the destination), desktop, floating bit, pre-float
    // zone/screen and auto-snap flag onto the destination store and leave the
    // source store holding none of them. The engine's migrateWindowToScreen is the
    // driver; this asserts the underlying move's completeness.
    // =====================================================================
    void migrationMovesAllPerWindowFields()
    {
        const QString windowId = QStringLiteral("kate|55555555-0000-0000-0000-000000000005");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");
        const QString homeScreen = QStringLiteral("DVI-1");
        const int desktop = 3;

        // Seed monitor A's store with every per-window field.
        SnapState* stateA = m_engine->stateForWindowOnScreen(windowId, monitorA);
        QVERIFY(stateA);
        stateA->assignWindowToZone(windowId, m_zoneIds[1], monitorA, desktop); // zone + screen + desktop
        stateA->markAsAutoSnapped(windowId); // auto-snap flag
        stateA->setFloating(windowId, true); // floating bit
        stateA->addPreFloatZone(windowId, QStringList{m_zoneIds[2]}); // pre-float zone
        stateA->addPreFloatScreen(windowId, homeScreen); // pre-float screen

        QVERIFY(stateA->isWindowSnapped(windowId));
        QVERIFY(stateA->isAutoSnapped(windowId));
        QVERIFY(stateA->isFloating(windowId));

        // Migrate to monitor B.
        QVERIFY(m_engine->migrateWindowToScreen(windowId, monitorB));
        SnapState* stateB = m_engine->stateForWindow(windowId);
        QVERIFY(stateB);
        QVERIFY(stateB != stateA);
        QCOMPARE(stateB->screenId(), monitorB);

        // Every per-window field is now on B's store.
        QCOMPARE(stateB->zonesForWindow(windowId), QStringList{m_zoneIds[1]});
        QCOMPARE(stateB->screenForWindow(windowId), monitorB); // live screen rewritten to destination
        QCOMPARE(stateB->desktopForWindow(windowId), desktop);
        QVERIFY(stateB->isFloating(windowId));
        QVERIFY(stateB->isAutoSnapped(windowId));
        // Pre-float rides along UNCHANGED (names the source's home context).
        QCOMPARE(stateB->preFloatZones(windowId), QStringList{m_zoneIds[2]});
        QCOMPARE(stateB->preFloatScreen(windowId), homeScreen);

        // The source store retains none of them.
        QVERIFY(stateA->zonesForWindow(windowId).isEmpty());
        QVERIFY(stateA->screenForWindow(windowId).isEmpty());
        QVERIFY(!stateA->isFloating(windowId));
        QVERIFY(!stateA->isAutoSnapped(windowId));
        QVERIFY(stateA->preFloatZones(windowId).isEmpty());
        QVERIFY(stateA->preFloatScreen(windowId).isEmpty());
    }

private:
    /// Install the FULL per-key resolver so the WTS facade and the engine agree on
    /// the same per-(screen,desktop,activity) stores (the default single-store
    /// convenience routes every facade call to the global holder, which hides the
    /// per-screen split these acceptance tests exercise). Mirrors the wiring the
    /// daemon injects and the one testLastUsedZoneIsPerScreen uses.
    void installFullResolver()
    {
        PhosphorPlacement::WindowTrackingService::SnapStateResolver resolver;
        resolver.forWindow = [e = m_engine](const QString& id) {
            return e->stateForWindow(id);
        };
        resolver.forWindowOnScreen = [e = m_engine](const QString& id, const QString& s) {
            return e->stateForWindowOnScreen(id, s);
        };
        resolver.forScreen = [e = m_engine](const QString& s) {
            return static_cast<SnapState*>(e->stateForScreen(s));
        };
        resolver.globals = [e = m_engine]() {
            return e->globalState();
        };
        resolver.allStates = [e = m_engine]() {
            return e->allSnapStates();
        };
        resolver.forgetWindow = [e = m_engine](const QString& id) {
            e->forgetWindow(id);
        };
        m_service->setSnapStateResolver(resolver);
    }

    /// Restore the fixture's single-store wiring so the shared teardown runs
    /// against the same holder init() set up.
    void restoreFixtureWiring()
    {
        m_service->setSnapState(m_engine->snapState());
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestSnapPerMonitor)
#include "test_snap_per_monitor.moc"
