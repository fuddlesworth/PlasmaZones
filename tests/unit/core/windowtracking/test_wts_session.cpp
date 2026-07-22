// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_session.cpp
 * @brief Unit tests for WindowTrackingService session restore, clear-stale, and resnap
 *
 * Tests cover:
 * 1. Clear stale pending assignments
 * 2. Resnap buffer population (desktop filter, per-screen VDM desktops,
 *    durable-record fallback) and resnap calculations from the previous layout
 * 3. Rotation calculations
 * 4. Pending-restore queue persistence round-trips
 * 5. Auto-snap marking
 * 6. Consume pending assignment
 *
 * Session zone-restore-from-session (the old calculateRestoreFromSession /
 * PendingRestoreQueues path) is now covered by the unified WindowPlacementStore
 * tests (test_window_placement_store, test_wta_convenience).
 *
 * WIRE FORMAT NOTE: fixtures use legacy "appId|uuid" composites because the
 * WTS is constructed without a WindowRegistry. See header in
 * test_wts_lifecycle.cpp for the reasoning.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QRectF>
#include <memory>

#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
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

using namespace PlasmaZones;
using PhosphorEngine::SnapResult;
using PhosphorEngine::ZoneAssignmentEntry;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using StubSettingsSession = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector + createTestLayout come from the shared
// helper (StubZoneDetector.h). No local Q_OBJECT subclass is needed — see the
// rationale in that header.
// =========================================================================

using StubZoneDetectorSession = StubZoneDetector;

namespace {

/// VDM double whose per-screen answer is scripted. The real
/// PhosphorWorkspaces::VirtualDesktopManager can never report an UNKNOWN screen
/// desktop through its public API (updateScreenDesktop rejects values below 1,
/// and a screen with no entry falls back to the global desktop, which is always
/// at least 1), so the vdmDesktop <= 0 fallback in
/// populateResnapBufferForAllScreens is only reachable with an override.
class ScriptedVdm : public PhosphorWorkspaces::VirtualDesktopManager
{
public:
    using PhosphorWorkspaces::VirtualDesktopManager::VirtualDesktopManager;
    int scriptedDesktop = 0; ///< returned for every screen
    int currentDesktopForScreen(const QString& /*screenId*/) const override
    {
        return scriptedDesktop;
    }
};

} // namespace

// =========================================================================
// Test Class
// =========================================================================

class TestWtsSession : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsSession(nullptr);
        m_zoneDetector = new StubZoneDetectorSession(nullptr);
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
    // P1: Session Restore
    // =====================================================================

    // Session zone-restore is now served by the unified WindowPlacementStore
    // (see test_window_placement_store + test_wta_convenience). The legacy
    // calculateRestoreFromSession / PendingRestoreQueues tests were removed with
    // that mechanism.

    // =====================================================================
    // P1: Clear Stale Pending
    // =====================================================================

    void testClearStalePendingAssignment()
    {
        QString windowId = QStringLiteral("app|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.screenId = QStringLiteral("DP-1");
        entry.virtualDesktop = 1;
        entry.layoutId = QUuid::createUuid().toString();
        entry.zoneNumbers = {1};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        bool popped = m_service->consumePendingAssignment(windowId);
        QVERIFY(popped);
        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    // =====================================================================
    // P1: Resnap
    // =====================================================================

    void testResnapFromPreviousLayout_zonePositionMapping()
    {
        QString window1 = QStringLiteral("app1|111");
        QString window2 = QStringLiteral("app2|222");
        QString window3 = QStringLiteral("app3|333");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);
        m_service->assignWindowToZone(window3, m_zoneIds[2], QString(), 0);

        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        // resolveLayoutForScreen falls back to defaultLayout() when the screen
        // has no explicit assignment — point the default at the NEW layout so
        // the resnap maps positions into it (same pattern as the cross-desktop
        // tests in test_wts_queries.cpp).
        const QString newLayoutId = newLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([newLayoutId]() {
            return newLayoutId;
        });
        m_service->onLayoutChanged();

        // Old zone positions map 1:1 into the new layout by zone number
        // (1 -> 1, 2 -> 2). window3's old position 3 exceeds the new 2-zone
        // layout and there is no captured pre-tile geometry to restore, so it
        // yields no entry at all.
        QHash<int, QString> newZoneByNumber;
        for (PhosphorZones::Zone* z : newLayout->zones()) {
            newZoneByNumber.insert(z->zoneNumber(), z->id().toString());
        }

        QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        QCOMPARE(resnap.size(), 2);
        QHash<QString, QString> targetByWindow;
        for (const ZoneAssignmentEntry& e : resnap) {
            targetByWindow.insert(e.windowId, e.targetZoneId);
        }
        QCOMPARE(targetByWindow.value(window1), newZoneByNumber.value(1));
        QCOMPARE(targetByWindow.value(window2), newZoneByNumber.value(2));
        QVERIFY(!targetByWindow.contains(window3));

        QVector<ZoneAssignmentEntry> secondCall = m_engine->calculateResnapFromPreviousLayout();
        QVERIFY(secondCall.isEmpty()); // Buffer consumed on first call
    }

    // Regression: daemon off->on while in autotile, then swap to snapping. The
    // window's snap zones live ONLY in the durable WindowPlacement record after a
    // restart — the live m_snapState map is cold. populateResnapBufferForAllScreens
    // must still resnap it from the durable record; otherwise it emits no geometry,
    // the effect never marks the window snapped, and the per-mode snapping border /
    // title-bar appearance is never applied.
    void testResnapBuffer_durableRecordWhenLiveSnapMapCold()
    {
        const QString windowId = QStringLiteral("app1|111");

        // Durable snapped record, but NO live assignWindowToZone (cold live map).
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = windowId;
        rec.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        rec.screenId = QStringLiteral("DP-1");
        rec.virtualDesktop = 0;
        PhosphorEngine::EngineSlot snapSlot;
        snapSlot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        snapSlot.zoneIds = QStringList{m_zoneIds[0]};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snapSlot);
        m_service->placementStore().record(rec);

        QVERIFY(m_service->zonesForWindow(windowId).isEmpty()); // live map is cold

        // autotile->snapping swap on DP-1 (no autotile screens excluded).
        m_service->populateResnapBufferForAllScreens({}, {QStringLiteral("DP-1")});

        const QVector<PhosphorEngine::ResnapEntry> buf = m_service->takeResnapBuffer();
        bool found = false;
        for (const PhosphorEngine::ResnapEntry& e : buf) {
            if (e.windowId == windowId) {
                found = true;
                QVERIFY(e.zonePosition > 0);
            }
        }
        QVERIFY2(found, "durable-recorded snap window must enter the resnap buffer when the live map is cold");
    }

    // Regression (#layout-leak): a per-desktop layout change must resnap only
    // the windows on that desktop. Without the desktop filter, assigning a
    // layout to desktop 2 pulled desktop 1's windows into desktop 2's zones —
    // the user saw one desktop's layout leak onto every desktop. With no VDM
    // wired, each window compares against the passed filter value directly.
    void testResnapBuffer_desktopFilterExcludesOtherDesktops()
    {
        const QString screen = QStringLiteral("DP-1");
        const QString winDesk1 = QStringLiteral("app1|111");
        const QString winDesk2 = QStringLiteral("app2|222");
        const QString winSticky = QStringLiteral("app3|333");

        m_service->assignWindowToZone(winDesk1, m_zoneIds[0], screen, 1);
        m_service->assignWindowToZone(winDesk2, m_zoneIds[1], screen, 2);
        m_service->assignWindowToZone(winSticky, m_zoneIds[2], screen, 0);

        m_service->populateResnapBufferForAllScreens({}, {}, 2);

        const QVector<PhosphorEngine::ResnapEntry> buf = m_service->takeResnapBuffer();
        QSet<QString> ids;
        for (const PhosphorEngine::ResnapEntry& e : buf) {
            ids.insert(e.windowId);
            if (e.windowId == winDesk2) {
                QCOMPARE(e.virtualDesktop, 2); // buffer preserves the recorded desktop
            }
        }
        QVERIFY2(!ids.contains(winDesk1), "a window recorded on desktop 1 must not enter a desktop-2 resnap");
        QVERIFY(ids.contains(winDesk2));
        QVERIFY2(ids.contains(winSticky), "sticky/unknown (desktop 0) windows resnap regardless of the filter");
    }

    // Per-output virtual desktops (#648): with a VDM wired, the desktop filter
    // compares each window against ITS screen's current desktop, not against
    // the single global filter value the caller passed.
    void testResnapBuffer_desktopFilterUsesPerScreenDesktop()
    {
        const QString screen = QStringLiteral("DP-1");

        PhosphorWorkspaces::VirtualDesktopManager vdm;
        vdm.updateScreenDesktop(screen, 3); // DP-1 is viewing desktop 3

        SnapState state(QString(), nullptr);
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, m_zoneDetector, nullptr, &vdm);
        service.setSnapState(&state);

        const QString winDesk3 = QStringLiteral("app1|111");
        const QString winDesk2 = QStringLiteral("app2|222");
        service.assignWindowToZone(winDesk3, m_zoneIds[0], screen, 3);
        service.assignWindowToZone(winDesk2, m_zoneIds[1], screen, 2);

        // The caller's global filter says desktop 2, but DP-1's own current
        // desktop is 3: the per-screen comparison must win.
        service.populateResnapBufferForAllScreens({}, {}, 2);

        const QVector<PhosphorEngine::ResnapEntry> buf = service.takeResnapBuffer();
        QSet<QString> ids;
        for (const PhosphorEngine::ResnapEntry& e : buf) {
            ids.insert(e.windowId);
        }
        QVERIFY2(ids.contains(winDesk3), "the window on DP-1's actual desktop (3) must resnap");
        QVERIFY2(!ids.contains(winDesk2),
                 "a window recorded on desktop 2 must not resnap while its screen shows desktop 3");

        service.setSnapState(nullptr);
    }

    // The second fallback in the resnap desktop filter: a wired VDM that does
    // NOT know a screen's desktop (returns <= 0) must fall back to the caller's
    // filter value — without it, an unknown screen desktop would exclude every
    // non-sticky window on that screen from the resnap.
    void testResnapBuffer_desktopFilterFallsBackWhenVdmUnknown()
    {
        const QString screen = QStringLiteral("DP-1");

        ScriptedVdm vdm; // scriptedDesktop stays 0: no screen desktop known

        SnapState state(QString(), nullptr);
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, m_zoneDetector, nullptr, &vdm);
        service.setSnapState(&state);

        const QString winDesk2 = QStringLiteral("app1|111");
        const QString winDesk1 = QStringLiteral("app2|222");
        service.assignWindowToZone(winDesk2, m_zoneIds[0], screen, 2);
        service.assignWindowToZone(winDesk1, m_zoneIds[1], screen, 1);

        service.populateResnapBufferForAllScreens({}, {}, 2);

        const QVector<PhosphorEngine::ResnapEntry> buf = service.takeResnapBuffer();
        QSet<QString> ids;
        for (const PhosphorEngine::ResnapEntry& e : buf) {
            ids.insert(e.windowId);
        }
        QVERIFY2(ids.contains(winDesk2), "with the screen desktop unknown, the filter value itself must apply");
        QVERIFY2(!ids.contains(winDesk1), "a window recorded on desktop 1 must not enter a desktop-2 resnap");

        service.setSnapState(nullptr);
    }

    // Regression (#layout-leak): the resnap calculation must carry each
    // window's recorded desktop into the ZoneAssignmentEntry so the batch
    // commit re-records it on ITS desktop, not the currently-viewed one.
    void testCalculateResnap_preservesRecordedDesktop()
    {
        const QString win = QStringLiteral("app1|111");
        m_service->assignWindowToZone(win, m_zoneIds[0], QString(), 2);

        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        const QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        QVERIFY(!resnap.isEmpty());
        for (const ZoneAssignmentEntry& e : resnap) {
            if (e.windowId == win) {
                QCOMPARE(e.virtualDesktop, 2);
                return;
            }
        }
        QFAIL("window missing from resnap entries");
    }

    // Producer-level twin of the rotation targetScreenId assertion: the
    // previous-layout resnap producer stamps each entry's authoritative
    // target screen so the commit side never re-derives it from geometry.
    void testResnapFromPreviousLayout_stampsTargetScreen()
    {
        const QString screen = QStringLiteral("DP-1");
        const QString win = QStringLiteral("app1|111");
        m_service->assignWindowToZone(win, m_zoneIds[0], screen, 1);

        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        const QString newLayoutId = newLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([newLayoutId]() {
            return newLayoutId;
        });
        m_service->onLayoutChanged();

        const QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        QVERIFY(!resnap.isEmpty());
        for (const ZoneAssignmentEntry& e : resnap) {
            QCOMPARE(e.targetScreenId, screen);
        }
    }

    // Same stamp assertion for the current-assignments producer (gap reflow,
    // VS reconfigure).
    void testCalculateResnapFromCurrentAssignments_stampsTargetScreen()
    {
        const QString screen = QStringLiteral("DP-1");
        const QString win = QStringLiteral("app1|111");
        m_service->assignWindowToZone(win, m_zoneIds[0], screen, 1);

        const QVector<ZoneAssignmentEntry> entries = m_engine->calculateResnapFromCurrentAssignments(QString());
        if (entries.isEmpty()) {
            QSKIP("resnap produced no geometry in headless harness — screen unavailable");
        }
        for (const ZoneAssignmentEntry& e : entries) {
            QCOMPARE(e.targetScreenId, screen);
        }
    }

    // Regression (#layout-leak): applyBatchAssignments must commit on the
    // entry's desktop. Dropping it re-stamped off-desktop windows onto the
    // current desktop, making the cross-desktop resnap corruption durable.
    void testApplyBatchAssignments_commitsOnEntryDesktop()
    {
        const QString win = QStringLiteral("app1|111");

        ZoneAssignmentEntry entry;
        entry.windowId = win;
        entry.targetZoneId = m_zoneIds[0];
        entry.targetGeometry = QRect(0, 0, 800, 600);
        entry.targetScreenId = QStringLiteral("DP-1");
        entry.virtualDesktop = 2;

        const auto geometries =
            m_engine->applyBatchAssignments({entry}, PhosphorEngine::SnapIntent::UserInitiated, nullptr);
        QCOMPARE(geometries.size(), 1);
        QCOMPARE(m_engine->snapState()->desktopForWindow(win), 2);
    }

    // Regression (#layout-leak): the pinned desktop must survive the FULL wire —
    // serialize (emitBatchedResnap's format), deserialize (the parse
    // handleBatchedResnap runs), then batch-commit. The through-the-wire twin of
    // testApplyBatchAssignments_commitsOnEntryDesktop.
    void testBatchedResnapWire_commitsOnEntryDesktop()
    {
        const QString win = QStringLiteral("app1|111");

        ZoneAssignmentEntry entry;
        entry.windowId = win;
        entry.targetZoneId = m_zoneIds[0];
        entry.targetGeometry = QRect(0, 0, 800, 600);
        entry.targetScreenId = QStringLiteral("DP-1");
        entry.virtualDesktop = 2;

        const QString wire = PhosphorEngine::GeometryUtils::serializeZoneAssignments({entry});
        QString parseError;
        const QVector<ZoneAssignmentEntry> parsed =
            PhosphorEngine::GeometryUtils::deserializeZoneAssignments(wire, &parseError);
        QVERIFY(parseError.isEmpty());
        QCOMPARE(parsed.size(), 1);
        // The producer's authoritative screen stamp must survive the wire —
        // without it the receive side re-derives the screen from the geometry
        // center, which races with virtual-screen swap/rotate.
        QCOMPARE(parsed[0].targetScreenId, QStringLiteral("DP-1"));

        const auto geometries =
            m_engine->applyBatchAssignments(parsed, PhosphorEngine::SnapIntent::UserInitiated, nullptr);
        QCOMPARE(geometries.size(), 1);
        QCOMPARE(m_engine->snapState()->desktopForWindow(win), 2);
    }

    // =====================================================================
    // P1: Rotation
    // =====================================================================

    // Regression (#layout-leak): resnap-from-current-assignments (gap reflow,
    // VS reconfigure) iterates the aggregated all-desktop assignment map, so
    // its entries must carry each window's recorded desktop or the batch
    // commit re-stamps off-desktop windows onto the current desktop.
    void testCalculateResnapFromCurrentAssignments_preservesRecordedDesktop()
    {
        const QString win = QStringLiteral("app1|111");
        m_service->assignWindowToZone(win, m_zoneIds[0], QString(), 2);

        const QVector<ZoneAssignmentEntry> entries = m_engine->calculateResnapFromCurrentAssignments(QString());
        if (entries.isEmpty()) {
            QSKIP("resnap produced no geometry in headless harness — screen unavailable");
        }
        for (const ZoneAssignmentEntry& e : entries) {
            if (e.windowId == win) {
                QCOMPARE(e.virtualDesktop, 2);
                return;
            }
        }
        QFAIL("window missing from resnap entries");
    }

    // Rotation sibling of the resnap desktop filter: rotating must act on the
    // screen's CURRENT desktop only — windows snapped on another desktop must
    // not be pulled through the viewing desktop's layout. Sticky desktop-0
    // windows still rotate.
    void testCalculateRotation_scopedToScreenCurrentDesktop()
    {
        const QString screen = QStringLiteral("DP-1");

        PhosphorWorkspaces::VirtualDesktopManager vdm;
        vdm.updateScreenDesktop(screen, 2); // DP-1 is viewing desktop 2

        SnapEngine engine(m_layoutManager, m_service, m_zoneDetector, &vdm);
        engine.setEngineSettings(m_settings);

        const QString winDesk2 = QStringLiteral("app1|111");
        const QString winDesk1 = QStringLiteral("app2|222");
        const QString winSticky = QStringLiteral("app3|333");
        engine.snapState()->assignWindowToZone(winDesk2, m_zoneIds[0], screen, 2);
        engine.snapState()->assignWindowToZone(winDesk1, m_zoneIds[1], screen, 1);
        engine.snapState()->assignWindowToZone(winSticky, m_zoneIds[2], screen, 0);

        const QVector<ZoneAssignmentEntry> cw = engine.calculateRotation(true);
        if (cw.isEmpty()) {
            QSKIP("rotation produced no geometry in headless harness — screen unavailable");
        }
        QSet<QString> ids;
        for (const ZoneAssignmentEntry& e : cw) {
            ids.insert(e.windowId);
            // Rotation stamps the authoritative target screen, like the two
            // resnap producers, so the commit side never re-derives it from
            // the geometry center.
            QCOMPARE(e.targetScreenId, screen);
        }
        QVERIFY2(ids.contains(winDesk2), "the window on DP-1's current desktop must rotate");
        QVERIFY2(ids.contains(winSticky), "sticky (desktop 0) windows rotate regardless of the filter");
        QVERIFY2(!ids.contains(winDesk1), "a window snapped on another desktop must not rotate");
    }

    void testCalculateRotation_clockwiseAndCounterClockwise()
    {
        QString window1 = QStringLiteral("app1|111");
        QString window2 = QStringLiteral("app2|222");

        // Desktop 2 so the rotation entries can be checked for desktop
        // preservation below (regression #layout-leak).
        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 2);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 2);

        QVector<ZoneAssignmentEntry> cw = m_engine->calculateRotation(true);
        QVector<ZoneAssignmentEntry> ccw = m_engine->calculateRotation(false);

        // Rotation target geometry needs a resolvable screen; in the headless
        // harness the zone-geometry lookup may yield no entries. Only assert the
        // rotation *semantics* when entries were produced — otherwise we'd be
        // pinning harness screen availability rather than the rotation logic.
        if (cw.isEmpty() && ccw.isEmpty()) {
            QSKIP("rotation produced no geometry in headless harness — screen unavailable");
        }

        QCOMPARE(cw.size(), ccw.size());

        // Clockwise (+1) and counter-clockwise (-1) must send the same window to
        // DIFFERENT target zones in a 3-zone layout, and the source must be the
        // window's current assignment.
        const auto targetFor = [](const QVector<ZoneAssignmentEntry>& v, const QString& w) -> QString {
            for (const auto& e : v) {
                if (e.windowId == w) {
                    return e.targetZoneId;
                }
            }
            return QString();
        };
        const QString cwTarget = targetFor(cw, window1);
        const QString ccwTarget = targetFor(ccw, window1);
        QVERIFY(!cwTarget.isEmpty());
        QVERIFY(!ccwTarget.isEmpty());
        QVERIFY(cwTarget != ccwTarget);

        // Rotation entries preserve each window's recorded desktop
        // (regression #layout-leak — see the resnap desktop tests above).
        for (const ZoneAssignmentEntry& e : cw) {
            QCOMPARE(e.virtualDesktop, 2);
        }
    }

    // =====================================================================
    // P0: Daemon Restart / Pending Restore
    // =====================================================================

    // Pins the pending-restore queue setter/readback round-trip — pure state
    // persistence, no restart simulation and no signal emission involved.
    void testPendingRestoreQueue_roundTrip()
    {
        QString appId = QStringLiteral("firefox");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.layoutId = m_testLayout->id().toString();

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneIds.first(), m_zoneIds[0]);
    }

    // =====================================================================
    // P0: PendingRestore round-trip preserves screenId
    // (wrong-display restore *resolution* is covered by test_window_placement_store
    //  / test_wta_convenience; this only pins the persistence round-trip)
    // =====================================================================

    void testPendingRestore_preservesScreenId()
    {
        QString appId = QStringLiteral("app");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.screenId = QStringLiteral("HDMI-2");

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().screenId, QStringLiteral("HDMI-2"));
    }

    // =====================================================================
    // P1: Auto-snap / Mark as auto-snapped
    // =====================================================================

    void testMarkAsAutoSnapped()
    {
        QString windowId = QStringLiteral("app|12345");

        QVERIFY(!m_service->isAutoSnapped(windowId));
        m_service->markAsAutoSnapped(windowId);
        QVERIFY(m_service->isAutoSnapped(windowId));
        QVERIFY(m_service->clearAutoSnapped(windowId));
        QVERIFY(!m_service->isAutoSnapped(windowId));
    }

    // =====================================================================
    // P1: Consume pending assignment
    // =====================================================================

    void testConsumePendingAssignment()
    {
        QString windowId = QStringLiteral("app|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.zoneNumbers = {1};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        m_service->consumePendingAssignment(windowId);

        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    // =====================================================================
    // P0: PendingRestore round-trip preserves zoneNumbers
    // (UUID-collision regeneration on import is covered elsewhere; this only
    //  pins that zoneNumbers survive the persistence round-trip)
    // =====================================================================

    void testPendingRestore_preservesZoneNumbers()
    {
        QString appId = QStringLiteral("app");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.zoneNumbers = {1};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneNumbers.first(), 1);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsSession* m_settings = nullptr;
    StubZoneDetectorSession* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsSession)
#include "test_wts_session.moc"
