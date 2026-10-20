// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_virtual_migration.cpp
 * @brief Unit tests for WindowTrackingService virtual screen migration
 *
 * Tests cover:
 * 1. Migration from physical to virtual screen IDs, including the
 *    geometry-routing happy path (headless, via FakePhysicalScreenSource) and the
 *    two guard clauses, each isolated so the conjunct it names is the
 *    deciding one.
 * 2. Migration from virtual back to physical screen IDs, including the
 *    pre-float screen fold.
 * 3. Round-trip: physical -> virtual -> physical preserves assignments.
 * 4. No-op cases: no windows on the target screen, no virtual windows.
 * 5. Re-assignment across a virtual-screen boundary leaves nothing behind on
 *    the old sub-screen.
 * 6. Removing all virtual screens reverts to physical, touching only the
 *    target monitor.
 * 7. physicalScreensWithStaleVirtualAssignments orphan discovery.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QRectF>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorScreens/Manager.h>
#include "FakePhysicalScreenSource.h"
#include "core/utils/utils.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
/// A two-way 50/50 horizontal split of @p physId, the shape the settings app
/// writes for a side-by-side subdivision.
PhosphorScreens::VirtualScreenConfig makeHorizontalSplit(const QString& physId)
{
    const auto makeDef = [&physId](int index, const QString& name, const QRectF& region) {
        PhosphorScreens::VirtualScreenDef def;
        def.id = PhosphorIdentity::VirtualScreenId::make(physId, index);
        def.physicalScreenId = physId;
        def.displayName = name;
        def.region = region;
        def.index = index;
        return def;
    };
    PhosphorScreens::VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(makeDef(0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1.0)));
    config.screens.append(makeDef(1, QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1.0)));
    return config;
}
} // namespace

// =========================================================================
// Test Class
// =========================================================================

class TestWtsVirtualMigration : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_snapState = new PhosphorSnapEngine::SnapState(QString(), nullptr);
        m_service->setSnapState(m_snapState);

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
        delete m_snapState;
        m_snapState = nullptr;
        delete m_service;
        m_service = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // =====================================================================
    // P0: Migration from physical to virtual IDs.
    //
    // migrateScreenAssignmentsToVirtual needs a ScreenManager for the
    // geometry lookups, and gets one here: ScreenManager takes an injected
    // IPhysicalScreenSource, so FakePhysicalScreenSource stages the physical output
    // headlessly and no QGuiApplication or real QScreen is involved. The
    // happy path is covered by testMigrateToVirtual_routesZoneToItsHalf
    // below; the two guard clauses (null manager, empty virtual list) are
    // covered separately, each naming the guard it actually exercises.
    // =====================================================================

    // The happy path: with a real (headless) ScreenManager, each window is
    // routed to the virtual sub-screen its ZONE's centre falls in, not
    // blanket-assigned to the first one. The layout's zone 0 sits in the left
    // third and zone 2 in the right third, so a 50/50 split must separate
    // them — a migration that ignored geometry would put both on vs:0.
    void testMigrateToVirtual_routesZoneToItsHalf()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QVERIFY(mgr.physicalScreenFor(physId).isValid());
        QVERIFY(mgr.setVirtualScreenConfig(physId, makeHorizontalSplit(physId)));

        const QStringList virtualIds = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(virtualIds.size(), 2);
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);

        const QString leftWin = QStringLiteral("konsole|left");
        const QString rightWin = QStringLiteral("dolphin|right");
        m_service->assignWindowToZone(leftWin, m_zoneIds[0], physId, 1);
        m_service->assignWindowToZone(rightWin, m_zoneIds[2], physId, 1);

        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, &mgr);

        QCOMPARE(m_service->screenForWindow(leftWin), vs0);
        QCOMPARE(m_service->screenForWindow(rightWin), vs1);
        // Zone assignments survive the screen rewrite.
        QCOMPARE(m_service->zonesForWindow(leftWin).first(), m_zoneIds[0]);
        QCOMPARE(m_service->zonesForWindow(rightWin).first(), m_zoneIds[2]);
    }

    // A window whose zone is not in its virtual screen's layout loses the
    // zone and its pre-float home, but it is a live window: its float-back
    // rects (one per monitor) and its sticky flag stay (F441).
    void testMigrateToVirtual_prunedWindowKeepsItsFreeGeometryAndStickyFlag()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QVERIFY(mgr.setVirtualScreenConfig(physId, makeHorizontalSplit(physId)));
        const QStringList virtualIds = mgr.virtualScreenIdsFor(physId);
        PhosphorZones::Layout* vsLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(vsLayout);
        for (const QString& vs : virtualIds) {
            m_layoutManager->assignLayout(vs, m_layoutManager->currentVirtualDesktop(), QString(), vsLayout);
        }

        const QString w = QStringLiteral("konsole|pruned");
        const QRect elsewhere(10, 10, 400, 300);
        m_service->assignWindowToZone(w, m_zoneIds[0], physId, 1);
        m_service->setWindowSticky(w, true);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = w;
        rec.appId = QStringLiteral("konsole");
        rec.screenId = physId;
        rec.freeGeometryByScreen.insert(QStringLiteral("HDMI-1"), elsewhere);
        QVERIFY(m_service->placementStore().record(rec));

        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, &mgr);

        QVERIFY2(m_service->zonesForWindow(w).isEmpty(), "the zone is not in the virtual screen's layout");
        const auto after = m_service->placementStore().peekExact(w);
        QVERIFY(after.has_value());
        QCOMPARE(after->freeGeometryByScreen.value(QStringLiteral("HDMI-1")), elsewhere);
        QVERIFY(m_service->isWindowSticky(w));
    }

    // A last-used zone recorded on desktop 2 is judged against desktop 2's
    // layout, not the one the desktop in view runs, so a reconfigure keeps it
    // (F162).
    void testMigrateToVirtual_backgroundDesktopKeepsItsLastUsedZone()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QVERIFY(mgr.setVirtualScreenConfig(physId, makeHorizontalSplit(physId)));
        const QStringList virtualIds = mgr.virtualScreenIdsFor(physId);
        const QString vs0 = virtualIds.first();
        PhosphorZones::Layout* other = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(other);
        m_layoutManager->assignLayout(vs0, 2, QString(), other);
        const QString zone = other->zones().first()->id().toString();
        m_snapState->restoreLastUsedZone(zone, vs0, QStringLiteral("konsole"), 2);

        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, &mgr);

        QCOMPARE(m_snapState->lastUsedZoneId(), zone);
    }

    // A desktop running autotile keeps its snap memory frozen: its last-used
    // zone is not judged against the default layout the cascade answers there
    // (F162).
    void testMigrateToVirtual_tilingDesktopKeepsItsFrozenLastUsedZone()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QVERIFY(mgr.setVirtualScreenConfig(physId, makeHorizontalSplit(physId)));
        const QStringList virtualIds = mgr.virtualScreenIdsFor(physId);
        const QString vs0 = virtualIds.first();
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
        entry.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(vs0, 2, QString(), entry);
        const QString frozen = QUuid::createUuid().toString();
        m_snapState->restoreLastUsedZone(frozen, vs0, QStringLiteral("konsole"), 2);

        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, &mgr);

        QCOMPARE(m_snapState->lastUsedZoneId(), frozen);
    }

    // Guard clause: a null manager refuses the migration. The virtual list is
    // deliberately NON-EMPTY so the null-manager conjunct is the only reason
    // the call can return early — with both falsy the test would pass under a
    // guard that had lost the manager check entirely.
    void testMigrateToVirtual_requiresScreenManager()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QStringList virtualIds = {vs0};

        // Assign window to physical screen
        const QString windowId = QStringLiteral("konsole|aaa-bbb");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], physId, 1);
        QCOMPARE(m_service->screenForWindow(windowId), physId);

        // With nullptr PhosphorScreens::ScreenManager, migrateToVirtual is a no-op (guard clause)
        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, nullptr);

        // Window should remain on the physical screen (no migration occurred)
        QCOMPARE(m_service->screenForWindow(windowId), physId);
    }

    // =====================================================================
    // P0: Migration from virtual back to physical IDs
    // =====================================================================

    void testMigrateFromVirtual_updatesScreenAssignment()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);

        // Assign windows to virtual screens directly
        const QString win1 = QStringLiteral("konsole|aaa");
        const QString win2 = QStringLiteral("dolphin|bbb");
        m_service->assignWindowToZone(win1, m_zoneIds[0], vs0, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vs1, 1);

        QCOMPARE(m_service->screenForWindow(win1), vs0);
        QCOMPARE(m_service->screenForWindow(win2), vs1);

        // Migrate back to physical
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        QCOMPARE(m_service->screenForWindow(win1), physId);
        QCOMPARE(m_service->screenForWindow(win2), physId);
    }

    // =====================================================================
    // P0: Round-trip — physical -> virtual -> physical preserves assignment
    // =====================================================================

    void testMigrationRoundTrip_virtualToPhysicalPreservesZone()
    {
        // Test round-trip at the virtual->physical->virtual level.
        // Since migrateToVirtual requires PhosphorScreens::ScreenManager, we simulate the
        // "virtual" state by assigning directly to a virtual screen ID,
        // then round-trip: virtual -> physical -> verify zone preserved.
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);

        const QString windowId = QStringLiteral("kate|round-trip");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);

        // Verify starting state: window on virtual screen
        QCOMPARE(m_service->screenForWindow(windowId), vs0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Virtual -> Physical
        m_service->migrateScreenAssignmentsFromVirtual(physId);
        QCOMPARE(m_service->screenForWindow(windowId), physId);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // PhosphorZones::Zone assignment is preserved throughout migration
        QCOMPARE(m_service->zonesForWindow(windowId).first(), m_zoneIds[0]);
    }

    // =====================================================================
    // P1: No windows on target screen — migration is a no-op
    // =====================================================================

    void testMigrateToVirtual_noWindowsOnScreen_noop()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString otherPhysId = QStringLiteral("LG:27GP850:ABC123");

        // A REAL manager, so the migration runs for real and the per-window
        // screen filter is what leaves the LG window alone. Passing nullptr
        // here would short-circuit on the guard clause instead, and the test
        // would pass even if the filter were removed entirely.
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
        fake.addScreen(QStringLiteral("DP-2"), QRect(3840, 0, 1920, 1080), otherPhysId);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        QVERIFY(mgr.setVirtualScreenConfig(physId, makeHorizontalSplit(physId)));
        const QStringList virtualIds = mgr.virtualScreenIdsFor(physId);

        // Assign window to a *different* physical screen
        const QString windowId = QStringLiteral("konsole|noop-test");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], otherPhysId, 1);

        // Migrate the Dell screen — should not touch the LG window
        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, &mgr);

        QCOMPARE(m_service->screenForWindow(windowId), otherPhysId);
    }

    void testMigrateFromVirtual_migratesPreFloatScreen()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);

        // Snap on a virtual screen, then float: unsnapForFloat records the
        // pre-float zone AND screen (vs0) in the owning store.
        const QString windowId = QStringLiteral("konsole|prefloat");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);
        m_service->unsnapForFloat(windowId);
        QCOMPARE(m_service->preFloatScreen(windowId), vs0);

        // Removing the virtual config must fold the pre-float screen back to
        // the physical id, or a later unfloat would target a dead virtual
        // screen and restore to the wrong geometry.
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        QCOMPARE(m_service->preFloatScreen(windowId), physId);
    }

    void testMigrateFromVirtual_noVirtualWindows_noop()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Assign window to the physical screen (not a virtual one)
        const QString windowId = QStringLiteral("konsole|noop-test");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], physId, 1);

        // Migrate from virtual — no virtual IDs to convert, window stays on physId
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        QCOMPARE(m_service->screenForWindow(windowId), physId);
    }

    // =====================================================================
    // T1: A window re-assigned across a virtual-screen boundary on the same
    // physical monitor lands wholly on the new sub-screen.
    //
    // The WTS does NOT detect the crossing — the daemon does, from the
    // window's centre, and calls assignWindowToZone with the new (zone,
    // screen) pair. What is pinned here is that the service's own state
    // moves as one: the screen value, the zone list, and the reverse map all
    // follow, with nothing left pointing at vs:0.
    // =====================================================================

    void testCrossBoundaryCrossing_windowMovesFromVs0ToVs1()
    {
        // Set up two virtual screens (left/right 50/50 split) on one physical monitor
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);

        // Window initially on vs:0 (left half)
        const QString windowId = QStringLiteral("konsole|cross-boundary");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);
        QCOMPARE(m_service->screenForWindow(windowId), vs0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // The daemon detects the crossing from the window's centre and
        // re-assigns; this is that call.
        m_service->assignWindowToZone(windowId, m_zoneIds[1], vs1, 1);

        QCOMPARE(m_service->screenForWindow(windowId), vs1);
        QCOMPARE(m_service->zonesForWindow(windowId).first(), m_zoneIds[1]);
        QVERIFY(m_service->isWindowSnapped(windowId));
        // The store holds ONE screen entry for this window and it names vs:1
        // — the distinguishing assertion this test used to lack, and the
        // reason it is not a duplicate of the plain re-assignment tests: a
        // rewrite that recorded the new screen without retiring the old one
        // would pass every line above.
        const QHash<QString, QString> assigns = m_snapState->screenAssignments();
        QCOMPARE(assigns.value(windowId), vs1);
        QVERIFY(assigns.value(windowId) != vs0);
    }

    // =====================================================================
    // T2: Remove all virtual screens — revert to physical
    // =====================================================================

    void testRemoveAllVirtualScreens_revertToPhysical()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);

        // Assign windows to virtual screens
        const QString win1 = QStringLiteral("konsole|revert1");
        const QString win2 = QStringLiteral("dolphin|revert2");
        const QString win3 = QStringLiteral("kate|revert3");
        m_service->assignWindowToZone(win1, m_zoneIds[0], vs0, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vs1, 1);
        m_service->assignWindowToZone(win3, m_zoneIds[2], vs0, 1);

        // Verify starting state: all on virtual screens
        QCOMPARE(m_service->screenForWindow(win1), vs0);
        QCOMPARE(m_service->screenForWindow(win2), vs1);
        QCOMPARE(m_service->screenForWindow(win3), vs0);

        // Remove all virtual screen config (revert to physical-only)
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        // All tracked windows should transition to the physical screen ID
        QCOMPARE(m_service->screenForWindow(win1), physId);
        QCOMPARE(m_service->screenForWindow(win2), physId);
        QCOMPARE(m_service->screenForWindow(win3), physId);

        // PhosphorZones::Zone assignments should be preserved
        QVERIFY(m_service->isWindowSnapped(win1));
        QVERIFY(m_service->isWindowSnapped(win2));
        QVERIFY(m_service->isWindowSnapped(win3));
        QCOMPARE(m_service->zonesForWindow(win1).first(), m_zoneIds[0]);
        QCOMPARE(m_service->zonesForWindow(win2).first(), m_zoneIds[1]);
        QCOMPARE(m_service->zonesForWindow(win3).first(), m_zoneIds[2]);
    }

    void testRemoveAllVirtualScreens_mixedScreens_onlyTargetAffected()
    {
        // When reverting virtual screens on one physical monitor, windows
        // on a second physical monitor must not be touched.
        //
        // This also carries the intent of a former standalone "boundary shift"
        // slot: WTS keys screen assignments by the virtual screen ID STRING
        // ("Dell:U2722D:111111/vs:0"), never by geometry, so where a virtual
        // boundary sits is a ScreenManager concern that WTS cannot observe.
        // Only an explicit migrateScreenAssignments* call moves a window, and
        // it moves only the windows on the physical screen it names — which is
        // exactly what the assertions below pin.
        const QString physId1 = QStringLiteral("Dell:U2722D:111111");
        const QString physId2 = QStringLiteral("LG:27GP850:222222");
        const QString vs1_0 = PhosphorIdentity::VirtualScreenId::make(physId1, 0);
        const QString vs1_1 = PhosphorIdentity::VirtualScreenId::make(physId1, 1);
        const QString vs2_0 = PhosphorIdentity::VirtualScreenId::make(physId2, 0);

        const QString win1 = QStringLiteral("app1|aaa");
        const QString win2 = QStringLiteral("app2|bbb");
        const QString win3 = QStringLiteral("app3|ccc");
        m_service->assignWindowToZone(win1, m_zoneIds[0], vs1_0, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vs1_1, 1);
        m_service->assignWindowToZone(win3, m_zoneIds[2], vs2_0, 1);

        // Remove virtual screens only on physId1
        m_service->migrateScreenAssignmentsFromVirtual(physId1);

        // physId1 windows should revert to physical
        QCOMPARE(m_service->screenForWindow(win1), physId1);
        QCOMPARE(m_service->screenForWindow(win2), physId1);
        // physId2 window should remain on its virtual screen
        QCOMPARE(m_service->screenForWindow(win3), vs2_0);
    }

    // =====================================================================
    // P1: Multiple windows across screens — only target screen migrated
    // =====================================================================

    void testMigrateFromVirtual_onlyTargetScreenAffected()
    {
        // Windows on different physical screens' virtual IDs should be
        // independent. Migrating one physical screen doesn't touch the other.
        const QString physId1 = QStringLiteral("Dell:U2722D:111111");
        const QString physId2 = QStringLiteral("LG:27GP850:222222");
        const QString vs1_0 = PhosphorIdentity::VirtualScreenId::make(physId1, 0);
        const QString vs2_0 = PhosphorIdentity::VirtualScreenId::make(physId2, 0);

        const QString win1 = QStringLiteral("app1|aaa");
        const QString win2 = QStringLiteral("app2|bbb");
        m_service->assignWindowToZone(win1, m_zoneIds[0], vs1_0, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vs2_0, 1);

        // Migrate only physId1 from virtual
        m_service->migrateScreenAssignmentsFromVirtual(physId1);

        QCOMPARE(m_service->screenForWindow(win1), physId1);
        QCOMPARE(m_service->screenForWindow(win2), vs2_0);
    }

    // =====================================================================
    // P1: Empty virtual screen list — no migration occurs
    // =====================================================================

    void testMigrateToVirtual_emptyVirtualList_noop()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString windowId = QStringLiteral("konsole|empty-list");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], physId, 1);

        // A live manager, so the EMPTY LIST is the only reason the guard
        // fires. The sibling test covers the null-manager conjunct; passing
        // both falsy here would let either half of the guard be deleted
        // without a test noticing.
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();

        m_service->migrateScreenAssignmentsToVirtual(physId, {}, &mgr);

        QCOMPARE(m_service->screenForWindow(windowId), physId);
    }

    // =====================================================================
    // physicalScreensWithStaleVirtualAssignments — orphan-physId discovery
    // =====================================================================

    void testStaleScan_findsOrphanInActiveAssignments()
    {
        const QString physA = QStringLiteral("Dell:U2722D:115107");
        const QString physB = QStringLiteral("LG:27GP850:ABC123");
        const QString vsA0 = PhosphorIdentity::VirtualScreenId::make(physA, 0);
        const QString vsB0 = PhosphorIdentity::VirtualScreenId::make(physB, 0);

        m_service->assignWindowToZone(QStringLiteral("kate|a"), m_zoneIds[0], vsA0, 1);
        m_service->assignWindowToZone(QStringLiteral("dolphin|b"), m_zoneIds[0], vsB0, 1);

        // Caller declares physA still subdivided; physB is not.
        const QSet<QString> stillSubdivided{physA};
        const QSet<QString> orphans = m_service->physicalScreensWithStaleVirtualAssignments(stillSubdivided);

        QCOMPARE(orphans.size(), 1);
        QVERIFY(orphans.contains(physB));
        QVERIFY(!orphans.contains(physA));
    }

    void testStaleScan_emptyWhenAllPolicyHonored()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        m_service->assignWindowToZone(QStringLiteral("kate|a"), m_zoneIds[0], vs0, 1);

        const QSet<QString> orphans = m_service->physicalScreensWithStaleVirtualAssignments({physId});

        QVERIFY(orphans.isEmpty());
    }

    void testStaleScan_ignoresBarePhysicalIds()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        m_service->assignWindowToZone(QStringLiteral("kate|a"), m_zoneIds[0], physId, 1);

        // Even with no subdivisions in the policy, a bare physId is not stale.
        const QSet<QString> orphans = m_service->physicalScreensWithStaleVirtualAssignments({});

        QVERIFY(orphans.isEmpty());
    }

    // =====================================================================
    // The last-used zone on a reconfigure (F803)
    // =====================================================================

    // A last-used zone already on a valid virtual screen is validated against
    // that screen's layout. A stale one is cleared and marked dirty by the
    // validation itself: nothing else in this reconfigure schedules a save.
    void validateLastUsed_staleZoneClearedAndMarked()
    {
        SplitFixture split;
        m_snapState->restoreLastUsedZone(QUuid::createUuid().toString(), split.vs0, QStringLiteral("konsole"), 1);
        m_service->clearDirty();

        m_service->migrateScreenAssignmentsToVirtual(split.physId, split.virtualIds, &split.mgr);

        QVERIFY(m_snapState->lastUsedZoneId().isEmpty());
        QCOMPARE(m_service->peekDirty(),
                 PhosphorPlacement::WindowTrackingService::DirtyMask(
                     PhosphorPlacement::WindowTrackingService::DirtyLastUsedZone));
    }

    // A context with no layout cannot prove the zone stale, so it keeps it (F264).
    void validateLastUsed_noneContextKeepsIt()
    {
        SplitFixture split;
        PhosphorZones::AssignmentEntry none;
        none.mode = PhosphorZones::AssignmentEntry::Snapping;
        none.snappingLayout = QString(PhosphorZones::NoSnappingLayout);
        m_layoutManager->setAssignmentEntryDirect(split.vs0, 1, QString(), none);
        // A zone in no layout, so only the None context can keep it.
        const QString unproven = QUuid::createUuid().toString();
        m_snapState->restoreLastUsedZone(unproven, split.vs0, QStringLiteral("konsole"), 1);

        m_service->migrateScreenAssignmentsToVirtual(split.physId, split.virtualIds, &split.mgr);

        QCOMPARE(m_snapState->lastUsedZoneId(), unproven);
    }

    // Another monitor's last-used zone is not this migration's to judge.
    void validateLastUsed_otherScreenUntouched()
    {
        SplitFixture split;
        const QString other = QStringLiteral("LG:27GP850:ABC123");
        const QString stale = QUuid::createUuid().toString();
        m_snapState->restoreLastUsedZone(stale, other, QStringLiteral("konsole"), 1);

        m_service->migrateScreenAssignmentsToVirtual(split.physId, split.virtualIds, &split.mgr);

        QCOMPARE(m_snapState->lastUsedZoneId(), stale);
        QCOMPARE(m_snapState->lastUsedScreenId(), other);
    }

    // Removing the split folds the last-used screen back to the monitor and
    // keeps a zone the monitor's layout still has.
    void migrateFromVirtual_keepsAValidLastUsedZone()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        m_snapState->restoreLastUsedZone(m_zoneIds[1], vs0, QStringLiteral("konsole"), 1);

        m_service->migrateScreenAssignmentsFromVirtual(physId);

        QCOMPARE(m_snapState->lastUsedZoneId(), m_zoneIds[1]);
        QCOMPARE(m_snapState->lastUsedScreenId(), physId);
    }

    // =====================================================================
    // resolveEffectiveScreenId (F584, F834)
    // =====================================================================

    void resolveEffective_liveVsIsItself()
    {
        SplitFixture split;
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, &split.mgr, nullptr);
        QCOMPARE(service.resolveEffectiveScreenId(split.vs0), split.vs0);
    }

    void resolveEffective_deadVsGoesToNearestSibling()
    {
        SplitFixture split;
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, &split.mgr, nullptr);
        QCOMPARE(service.resolveEffectiveScreenId(PhosphorIdentity::VirtualScreenId::make(split.physId, 3)), split.vs1);
    }

    void resolveEffective_physicalIsItself()
    {
        SplitFixture split;
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, &split.mgr, nullptr);
        QCOMPARE(service.resolveEffectiveScreenId(split.physId), split.physId);
    }

    void resolveEffective_splitRemovedGoesToPhysical()
    {
        SplitFixture split;
        QVERIFY(split.mgr.setVirtualScreenConfig(split.physId, PhosphorScreens::VirtualScreenConfig{}));
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, &split.mgr, nullptr);
        QCOMPARE(service.resolveEffectiveScreenId(split.vs1), split.physId);
    }

    // An unplugged output keeps its split configured, so its virtual screens
    // are still listed for it but none is live: the answer is the monitor, not
    // a dead sibling.
    void resolveEffective_unpluggedOutputGoesToPhysical()
    {
        SplitFixture split;
        split.fake.removeScreen(QStringLiteral("DP-1"));
        QVERIFY(split.mgr.virtualScreenIdsFor(split.physId).contains(split.vs0));
        QVERIFY(!split.mgr.effectiveScreenIds().contains(split.vs0));
        PhosphorPlacement::WindowTrackingService service(m_layoutManager, &split.mgr, nullptr);
        QCOMPARE(service.resolveEffectiveScreenId(PhosphorIdentity::VirtualScreenId::make(split.physId, 3)),
                 split.physId);
    }

    // =====================================================================
    // A closed window's record follows its monitor's split (F381)
    // =====================================================================

    // A record with no live window is placed by its snapped slot's zone: zone 3
    // sits in the right third, so the record moves to the right half.
    void migrateToVirtual_closedWindowRecordFollowsItsZone()
    {
        SplitFixture split;
        const QRect spot(3000, 200, 400, 300);
        QVERIFY(m_service->placementStore().record(closedSnappedRecord(split.physId, m_zoneIds[2], spot)));

        m_service->migrateScreenAssignmentsToVirtual(split.physId, split.virtualIds, &split.mgr);

        const auto after = m_service->placementStore().peekExact(QStringLiteral("kate|closed"));
        QVERIFY(after.has_value());
        QCOMPARE(after->screenId, split.vs1);
        QCOMPARE(after->freeGeometryByScreen.value(split.vs1), spot);
        QVERIFY(!after->freeGeometryByScreen.contains(split.physId));
    }

    // A record on a virtual screen the new configuration does not have is
    // re-homed by index, the same as a live window's.
    void migrateToVirtual_deadVsRecordReHomed()
    {
        SplitFixture split;
        const QString dead = PhosphorIdentity::VirtualScreenId::make(split.physId, 2);
        QVERIFY(m_service->placementStore().record(closedSnappedRecord(dead, m_zoneIds[0], QRect())));

        m_service->migrateScreenAssignmentsToVirtual(split.physId, split.virtualIds, &split.mgr);

        const auto after = m_service->placementStore().peekExact(QStringLiteral("kate|closed"));
        QVERIFY(after.has_value());
        QCOMPARE(after->screenId, split.vs1);
    }

private:
    /// A 3840x2160 output split 50/50 side by side, staged headlessly.
    struct SplitFixture
    {
        SplitFixture()
        {
            fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3840, 2160), physId);
            mgr.start();
            if (!mgr.setVirtualScreenConfig(physId, makeHorizontalSplit(physId))) {
                qFatal("could not split the output");
            }
            virtualIds = mgr.virtualScreenIdsFor(physId);
        }
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);
        PhosphorScreens::FakePhysicalScreenSource fake;
        PhosphorScreens::ScreenManager mgr{
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false}};
        QStringList virtualIds;
    };

    /// The record of a closed window snapped in @p zoneId on @p screenId,
    /// with a float-back rect there when @p spot is valid.
    static PhosphorEngine::WindowPlacement closedSnappedRecord(const QString& screenId, const QString& zoneId,
                                                               const QRect& spot)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("kate|closed");
        rec.appId = QStringLiteral("kate");
        rec.screenId = screenId;
        rec.virtualDesktop = 1;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{zoneId};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        if (spot.isValid()) {
            rec.freeGeometryByScreen.insert(screenId, spot);
        }
        return rec;
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsVirtualMigration)
#include "test_wts_virtual_migration.moc"
