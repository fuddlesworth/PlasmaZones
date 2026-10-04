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
 *    effort. Snap on B, float, migrate to A → the window reports A and has forgotten
 *    its home on B, so no unfloat (on A or back on B) throws it back into B's zone.
 * 2. unfloatDrivenFromAnotherMonitorRefusesTheHome: SnapEngine::setWindowFloat(
 *    id, false, A) refuses a home on B and keeps the window floating.
 * 3. perMonitorSnapIndependence: two windows snapped on two monitors keep independent
 *    per-screen state; floating one does not disturb the other, and same-index zones on
 *    different monitors do not collide.
 * 4. migrationMovesEveryFieldButTheZone: a cross-monitor migrate carries the screen,
 *    floating bit and auto-snap flag to the destination store, re-stamps the desktop,
 *    drops the zone and the pre-float home, and leaves nothing behind in the source.
 * 5. pruneRemovedScreenDropsOnlyThatMonitor: a physically removed output's stores
 *    (including its virtual sub-screens) are reclaimed; the other monitor and the
 *    global holder survive.
 * 6. pruneRemovedDesktopDropsOnlyThatDesktop: a destroyed desktop's stores are
 *    reclaimed; other desktops and the empty-screenId global holder survive.
 * 7. pruneRemovedActivityDropsOnlyThatActivity: removed activities' stores are
 *    reclaimed; the empty-activity global holder is never a target.
 */

#include <QGuiApplication>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include "core/utils/utils.h"
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
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
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
    // B's zone when unfloated on A. It forgot that home when it moved, so moving
    // back to B does not bring it back either.
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

        // (b) The pre-float home on B is forgotten, and the window still floats.
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());
        QVERIFY(m_service->isWindowFloating(windowId));

        // (c) Unfloating while on A finds no home to throw it back to.
        QVERIFY(!m_engine->resolveUnfloatGeometry(windowId, monitorA).found);

        // (d) Nor does migrating back to B bring the forgotten home back.
        QVERIFY(m_engine->migrateWindowToScreen(windowId, monitorB));
        QCOMPARE(m_service->screenForWindow(windowId), monitorB);
        QVERIFY(!m_engine->resolveUnfloatGeometry(windowId, monitorB).found);
    }

    // =====================================================================
    // Test 2: an unfloat driven from another monitor refuses the home.
    //
    // The screen setWindowFloat is driven with is where the window is. A home
    // on another monitor is stale there, so driving setWindowFloat(id, false, A)
    // with the home on B keeps the window floating (the fallback setting is
    // off), and never throws it back to B.
    // =====================================================================
    void unfloatDrivenFromAnotherMonitorRefusesTheHome()
    {
        installFullResolver();

        const QString windowId = QStringLiteral("konsole|22222222-0000-0000-0000-000000000002");
        const QString monitorA = QStringLiteral("DP-1");
        const QString monitorB = QStringLiteral("HDMI-1");

        // Snap on B and float: pre-float (zone0, B).
        m_service->assignWindowToZone(windowId, m_zoneIds[0], monitorB, 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QCOMPARE(m_service->preFloatScreen(windowId), monitorB);

        // Drive the unfloat with a different screen (A): refused, still floating.
        QSignalSpy applySpy(m_engine, &SnapEngine::applyGeometryRequested);
        m_engine->setWindowFloat(windowId, false, monitorA);
        QCOMPARE(applySpy.count(), 0);
        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(m_service->isWindowFloating(windowId));

        // Driven from B, the monitor it floated on, the home restores. The snap
        // commit needs a real QScreen for the zone geometry, so gate on it.
        m_engine->setWindowFloat(windowId, false, monitorB);
        if (QGuiApplication::screens().size() > 0) {
            QCOMPARE(applySpy.count(), 1);
            QCOMPARE(applySpy.at(0).at(5).toString(), m_zoneIds[0]);
            QVERIFY(m_service->isWindowSnapped(windowId));
        }
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
    }

    // =====================================================================
    // Test 4 (Discussion #724): migration moves every per-window field but the zone.
    //
    // A cross-monitor migrate carries the live screen (rewritten to the
    // destination), the floating bit and auto-snap flag onto the destination
    // store, and re-stamps the desktop to the destination key's. The zone and
    // the pre-float home stay behind, dropped: both name the source screen's
    // layout. The source store holds none of them afterwards.
    // =====================================================================
    void migrationMovesEveryFieldButTheZone()
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

        // Every per-window field but the zone is now on B's store, on B's desktop.
        QVERIFY(stateB->zonesForWindow(windowId).isEmpty());
        QCOMPARE(stateB->screenForWindow(windowId), monitorB); // live screen rewritten to destination
        QCOMPARE(stateB->desktopForWindow(windowId), m_engine->heldKeyForWindow(windowId)->desktop);
        QVERIFY(stateB->desktopForWindow(windowId) != desktop);
        QVERIFY(stateB->isFloating(windowId));
        QVERIFY(stateB->isAutoSnapped(windowId));
        // The pre-float home is forgotten, not carried.
        QVERIFY(stateB->preFloatZones(windowId).isEmpty());
        QVERIFY(stateB->preFloatScreen(windowId).isEmpty());

        // The source store retains none of them.
        QVERIFY(stateA->zonesForWindow(windowId).isEmpty());
        QVERIFY(stateA->screenForWindow(windowId).isEmpty());
        QVERIFY(!stateA->isFloating(windowId));
        QVERIFY(!stateA->isAutoSnapped(windowId));
        QVERIFY(stateA->preFloatZones(windowId).isEmpty());
        QVERIFY(stateA->preFloatScreen(windowId).isEmpty());
    }

    // =====================================================================
    // Test 5 (#724 follow-up): a physically removed monitor's per-key stores are
    // reclaimed. pruneStatesForRemovedScreen drops every store on the removed physical
    // output — including its virtual sub-screens ("DP-1/vs:0") — while the other
    // monitor's store and the global holder survive. Without this the per-monitor
    // stores leak across monitor hot-unplug.
    // =====================================================================
    void pruneRemovedScreenDropsOnlyThatMonitor()
    {
        const QString winA = QStringLiteral("firefox|aaaa0000-0000-0000-0000-000000000010");
        const QString winAvs = QStringLiteral("kate|aaaa0000-0000-0000-0000-000000000011");
        const QString winB = QStringLiteral("dolphin|bbbb0000-0000-0000-0000-000000000012");
        const QString monitorAphys = QStringLiteral("DP-1");
        const QString monitorAvs = QStringLiteral("DP-1/vs:0");
        const QString monitorB = QStringLiteral("HDMI-1");

        m_engine->stateForWindowOnScreen(winA, monitorAphys)->assignWindowToZone(winA, m_zoneIds[0], monitorAphys, 1);
        m_engine->stateForWindowOnScreen(winAvs, monitorAvs)->assignWindowToZone(winAvs, m_zoneIds[1], monitorAvs, 1);
        m_engine->stateForWindowOnScreen(winB, monitorB)->assignWindowToZone(winB, m_zoneIds[0], monitorB, 1);

        SnapState* storeB = m_engine->stateForWindow(winB);
        SnapState* global = m_engine->globalState();
        QVERIFY(storeB != global);
        const int before = m_engine->allSnapStates().size();

        m_engine->pruneStatesForRemovedScreen(monitorAphys);

        // Both DP-1 stores (physical id + virtual sub-screen) are gone: their windows
        // fall back to the global holder now the reverse-map entry is dropped.
        QCOMPARE(m_engine->stateForWindow(winA), m_engine->globalState());
        QCOMPARE(m_engine->stateForWindow(winAvs), m_engine->globalState());
        // The other monitor's store and the global holder survive.
        QCOMPARE(m_engine->stateForWindow(winB), storeB);
        // The SAME holder survives IN THE STATES MAP, un-pruned. Comparing
        // globalState() to its earlier copy is vacuous (the member is never
        // reassigned, so it passes even if the prune removed the holder from the
        // map); reading the map genuinely fails in that case.
        QVERIFY(m_engine->allSnapStates().contains(global));
        QCOMPARE(m_engine->allSnapStates().size(), before - 2);
    }

    // =====================================================================
    // Test 6 (#724 follow-up): a destroyed virtual desktop's per-key stores are
    // reclaimed. pruneStatesForDesktop drops stores on that desktop only; other
    // desktops and the empty-screenId global holder survive (its key's desktop
    // defaults to 1, but the empty screenId is what exempts it).
    // =====================================================================
    void pruneRemovedDesktopDropsOnlyThatDesktop()
    {
        const QString win2 = QStringLiteral("firefox|cccc0000-0000-0000-0000-000000000020");
        const QString win3 = QStringLiteral("dolphin|dddd0000-0000-0000-0000-000000000021");
        const QString monitor = QStringLiteral("DP-1");

        m_engine->setCurrentDesktop(2);
        m_engine->stateForWindowOnScreen(win2, monitor)->assignWindowToZone(win2, m_zoneIds[0], monitor, 2);
        m_engine->setCurrentDesktop(3);
        m_engine->stateForWindowOnScreen(win3, monitor)->assignWindowToZone(win3, m_zoneIds[1], monitor, 3);

        QVERIFY(m_engine->desktopsWithActiveState().contains(2));
        QVERIFY(m_engine->desktopsWithActiveState().contains(3));
        SnapState* store2 = m_engine->stateForWindow(win2);
        SnapState* global = m_engine->globalState();
        const int before = m_engine->allSnapStates().size();

        m_engine->pruneStatesForDesktop(3);

        QCOMPARE(m_engine->stateForWindow(win3), m_engine->globalState()); // desktop-3 store gone
        QCOMPARE(m_engine->stateForWindow(win2), store2); // desktop-2 store survives
        QVERIFY(!m_engine->desktopsWithActiveState().contains(3));
        QVERIFY(m_engine->desktopsWithActiveState().contains(2));
        // The holder survives IN THE STATES MAP, as tests 5 and 7 assert for their
        // own prunes. Its key's desktop defaults to 1, so a prune of desktop 3 has
        // no reason to touch it — but the empty screenId is what actually exempts
        // it, and only reading the map proves the prune honoured that.
        QVERIFY(m_engine->allSnapStates().contains(global));
        QCOMPARE(m_engine->allSnapStates().size(), before - 1);
    }

    // =====================================================================
    // Test 7 (#724 follow-up): removed activities' per-key stores are reclaimed.
    // pruneStatesForActivities keeps only stores whose activity is still valid; the
    // empty-activity global holder is never a prune target.
    // =====================================================================
    void pruneRemovedActivityDropsOnlyThatActivity()
    {
        const QString winA = QStringLiteral("firefox|eeee0000-0000-0000-0000-000000000030");
        const QString winB = QStringLiteral("dolphin|ffff0000-0000-0000-0000-000000000031");
        const QString monitor = QStringLiteral("DP-1");
        const QString actKeep = QStringLiteral("activity-keep");
        const QString actGone = QStringLiteral("activity-gone");

        m_engine->setCurrentDesktop(1);
        m_engine->setCurrentActivity(actKeep);
        m_engine->stateForWindowOnScreen(winA, monitor)->assignWindowToZone(winA, m_zoneIds[0], monitor, 1);
        m_engine->setCurrentActivity(actGone);
        m_engine->stateForWindowOnScreen(winB, monitor)->assignWindowToZone(winB, m_zoneIds[1], monitor, 1);

        SnapState* storeKeep = m_engine->stateForWindow(winA);
        SnapState* global = m_engine->globalState();
        const int before = m_engine->allSnapStates().size();

        m_engine->pruneStatesForActivities(QStringList{actKeep});

        QCOMPARE(m_engine->stateForWindow(winB), m_engine->globalState()); // removed-activity store gone
        QCOMPARE(m_engine->stateForWindow(winA), storeKeep); // kept-activity store survives
        // The SAME empty-activity holder survives, un-pruned. Map-membership
        // check, not a pointer self-compare — see test 5.
        QVERIFY(m_engine->allSnapStates().contains(global));
        QCOMPARE(m_engine->allSnapStates().size(), before - 1);
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
        resolver.forWindowOnScreen = [e = m_engine](const QString& id, const QString& s, int desktop) {
            return e->stateForWindowOnScreen(id, s, desktop);
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
