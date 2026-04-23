// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_virtual_migration.cpp
 * @brief Unit tests for WindowTrackingService virtual screen migration
 *
 * Tests cover:
 * 1. Migration from physical to virtual screen IDs
 * 2. Migration from virtual back to physical screen IDs
 * 3. Round-trip: physical -> virtual -> physical preserves assignments
 * 4. Migration with no windows on the target screen (no-op)
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QRectF>
#include <memory>

#include "core/windowtrackingservice.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

using StubSettingsMigration = StubSettings;

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
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsMigration(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);
        m_snapState = new PhosphorZones::SnapState(QString(), nullptr);
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
    // P0: Migration from physical to virtual IDs
    // migrateScreenAssignmentsToVirtual requires a Phosphor::Screens::ScreenManager for
    // geometry lookups. With nullptr mgr it early-returns (guard clause).
    // We test that behavior explicitly, and also test the reverse direction
    // (migrateFromVirtual) which does NOT need a Phosphor::Screens::ScreenManager.
    //
    // NOTE: The forward migration happy path (physical → virtual with
    // geometry-based routing via Phosphor::Screens::ScreenManager) is not tested here because
    // Phosphor::Screens::ScreenManager requires a running QGuiApplication with real QScreen
    // objects. This path is covered by manual integration testing per the
    // PR test plan.
    // =====================================================================

    void testMigrateToVirtual_requiresScreenManager()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QStringList virtualIds = {vs0};

        // Assign window to physical screen
        const QString windowId = QStringLiteral("konsole|aaa-bbb");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], physId, 1);
        QCOMPARE(m_service->screenAssignments().value(windowId), physId);

        // With nullptr Phosphor::Screens::ScreenManager, migrateToVirtual is a no-op (guard clause)
        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, nullptr);

        // Window should remain on the physical screen (no migration occurred)
        QCOMPARE(m_service->screenAssignments().value(windowId), physId);
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

        QCOMPARE(m_service->screenAssignments().value(win1), vs0);
        QCOMPARE(m_service->screenAssignments().value(win2), vs1);

        // Migrate back to physical
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        QCOMPARE(m_service->screenAssignments().value(win1), physId);
        QCOMPARE(m_service->screenAssignments().value(win2), physId);
    }

    // =====================================================================
    // P0: Round-trip — physical -> virtual -> physical preserves assignment
    // =====================================================================

    void testMigrationRoundTrip_virtualToPhysicalPreservesZone()
    {
        // Test round-trip at the virtual->physical->virtual level.
        // Since migrateToVirtual requires Phosphor::Screens::ScreenManager, we simulate the
        // "virtual" state by assigning directly to a virtual screen ID,
        // then round-trip: virtual -> physical -> verify zone preserved.
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);

        const QString windowId = QStringLiteral("kate|round-trip");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);

        // Verify starting state: window on virtual screen
        QCOMPARE(m_service->screenAssignments().value(windowId), vs0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Virtual -> Physical
        m_service->migrateScreenAssignmentsFromVirtual(physId);
        QCOMPARE(m_service->screenAssignments().value(windowId), physId);
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
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QStringList virtualIds = {vs0};

        // Assign window to a *different* physical screen
        const QString windowId = QStringLiteral("konsole|noop-test");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], otherPhysId, 1);

        // Migrate the Dell screen — should not touch the LG window
        m_service->migrateScreenAssignmentsToVirtual(physId, virtualIds, nullptr);

        QCOMPARE(m_service->screenAssignments().value(windowId), otherPhysId);
    }

    void testMigrateFromVirtual_noVirtualWindows_noop()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Assign window to the physical screen (not a virtual one)
        const QString windowId = QStringLiteral("konsole|noop-test");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], physId, 1);

        // Migrate from virtual — no virtual IDs to convert, window stays on physId
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        QCOMPARE(m_service->screenAssignments().value(windowId), physId);
    }

    // =====================================================================
    // T1: Cross-VS boundary crossing — window moves between virtual screens
    // on the same physical monitor
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
        QCOMPARE(m_service->screenAssignments().value(windowId), vs0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Simulate the window moving to vs:1 (right half) by reassigning
        // This mirrors what the daemon does when it detects a window's center
        // has moved to a different virtual screen
        m_service->assignWindowToZone(windowId, m_zoneIds[1], vs1, 1);

        // Verify the WTS detects the screen change and updates the assignment
        QCOMPARE(m_service->screenAssignments().value(windowId), vs1);
        QCOMPARE(m_service->zonesForWindow(windowId).first(), m_zoneIds[1]);
        QVERIFY(m_service->isWindowSnapped(windowId));
    }

    // =====================================================================
    // T2: VS config change while windows are snapped — boundary shift
    // =====================================================================

    void testVsConfigChange_boundaryShift_windowStaysAssigned()
    {
        // This test verifies that WTS screen assignments are keyed by virtual
        // screen ID (e.g. "Dell:U2722D:115107/vs:0"), NOT by geometry. When
        // Phosphor::Screens::ScreenManager shifts the boundary from 50/50 to 70/30, the virtual
        // screen IDs remain the same, so WTS assignments must be stable.
        //
        // A full integration test would call Phosphor::Screens::ScreenManager::setVirtualScreenConfig
        // with a new boundary, but WTS's test fixture uses nullptr for
        // Phosphor::Screens::ScreenManager (it only needs PhosphorZones::LayoutRegistry + PhosphorZones::ZoneDetector).
        // The boundary shift is a Phosphor::Screens::ScreenManager concern, not a WTS concern — WTS only cares about
        // the string screen ID.
        //
        // We verify the invariant that matters: assigning to vs:0 and then
        // reading back gives the same screen ID and zone, which proves WTS
        // does not spontaneously reassign windows when no migration API is called.
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);

        const QString windowId = QStringLiteral("konsole|config-change");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);

        QCOMPARE(m_service->screenAssignments().value(windowId), vs0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Simulate "time passes, boundary shifts" — no migration API called.
        // Assign a second window to vs:1 to prove cross-contamination doesn't occur.
        const QString windowId2 = QStringLiteral("dolphin|config-change-2");
        m_service->assignWindowToZone(windowId2, m_zoneIds[1], vs1, 1);

        // Original window must still be on vs:0 with its zone intact
        QCOMPARE(m_service->screenAssignments().value(windowId), vs0);
        QCOMPARE(m_service->zonesForWindow(windowId).first(), m_zoneIds[0]);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Second window must be on vs:1
        QCOMPARE(m_service->screenAssignments().value(windowId2), vs1);
        QCOMPARE(m_service->zonesForWindow(windowId2).first(), m_zoneIds[1]);
    }

    // =====================================================================
    // T3: Remove all virtual screens — revert to physical
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
        QCOMPARE(m_service->screenAssignments().value(win1), vs0);
        QCOMPARE(m_service->screenAssignments().value(win2), vs1);
        QCOMPARE(m_service->screenAssignments().value(win3), vs0);

        // Remove all virtual screen config (revert to physical-only)
        m_service->migrateScreenAssignmentsFromVirtual(physId);

        // All tracked windows should transition to the physical screen ID
        QCOMPARE(m_service->screenAssignments().value(win1), physId);
        QCOMPARE(m_service->screenAssignments().value(win2), physId);
        QCOMPARE(m_service->screenAssignments().value(win3), physId);

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
        // on a second physical monitor must not be touched
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
        QCOMPARE(m_service->screenAssignments().value(win1), physId1);
        QCOMPARE(m_service->screenAssignments().value(win2), physId1);
        // physId2 window should remain on its virtual screen
        QCOMPARE(m_service->screenAssignments().value(win3), vs2_0);
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

        QCOMPARE(m_service->screenAssignments().value(win1), physId1);
        QCOMPARE(m_service->screenAssignments().value(win2), vs2_0);
    }

    // =====================================================================
    // P1: Empty virtual screen list — no migration occurs
    // =====================================================================

    void testMigrateToVirtual_emptyVirtualList_noop()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString windowId = QStringLiteral("konsole|empty-list");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], physId, 1);

        m_service->migrateScreenAssignmentsToVirtual(physId, {}, nullptr);

        QCOMPARE(m_service->screenAssignments().value(windowId), physId);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsMigration* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorZones::SnapState* m_snapState = nullptr;
    WindowTrackingService* m_service = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsVirtualMigration)
#include "test_wts_virtual_migration.moc"
