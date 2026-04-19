// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_assist_virtual.cpp
 * @brief Unit tests for snap assist behavior with virtual screen IDs
 *
 * Tests cover:
 * 1. screensMatch() with virtual screen IDs (same, different, physical vs virtual)
 * 2. buildOccupiedZoneSet() filtering with virtual screen IDs
 * 3. getEmptyZonesJson() with virtual screen IDs (returns valid JSON)
 *
 * These tests reproduce the snap assist bug where virtual screens cause
 * screensMatch() to return false even when screen IDs should match,
 * making all zones appear empty/occupied incorrectly.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

#include "core/windowtrackingservice.h"
#include <PhosphorZones/LayoutManager.h>
#include "core/pzlayoutmanagerfactory.h"
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

// Type alias — shared StubSettings with snap assist enabled for this test file
using StubSettingsSnapAssist = StubSettings;

// =========================================================================
// Test Class
// =========================================================================

class TestSnapAssistVirtual : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = makePzLayoutManager(nullptr).release();
        m_settings = new StubSettingsSnapAssist(nullptr);
        m_settings->setSnapAssistFeatureEnabled(true);
        m_settings->setSnapAssistEnabled(true);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);

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
    // screensMatch — Virtual Screen ID scenarios
    // =====================================================================

    void testScreensMatch_identicalVirtualIds_returnsTrue()
    {
        // Two identical virtual screen IDs must match (fast path: a == b)
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QVERIFY(Phosphor::Screens::ScreenIdentity::screensMatch(vsId, vsId));
    }

    void testScreensMatch_differentVirtualIndexes_returnsFalse()
    {
        // Two different virtual screens on the same physical monitor must NOT match
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(vs0, vs1));
    }

    void testScreensMatch_physicalVsVirtual_returnsFalse()
    {
        // A physical screen ID vs a virtual screen ID derived from it must NOT match
        // (once virtual screens are configured, the physical ID is no longer a valid screen)
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(physId, vsId));
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(vsId, physId));
    }

    void testScreensMatch_differentPhysicalVirtual_returnsFalse()
    {
        // Virtual screens from different physical monitors must NOT match
        QString vsA = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vsB = QStringLiteral("LG:27GL850:ABC123/vs:0");
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(vsA, vsB));
    }

    void testScreensMatch_emptyVsVirtual_returnsFalse()
    {
        // Empty string vs virtual screen ID must NOT match
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(QString(), vsId));
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(vsId, QString()));
    }

    void testScreensMatch_bothEmpty_returnsTrue()
    {
        // Two empty strings are identical -> true via fast path
        QVERIFY(Phosphor::Screens::ScreenIdentity::screensMatch(QString(), QString()));
    }

    void testScreensMatch_identicalPhysicalIds_returnsTrue()
    {
        // Sanity: two identical physical IDs still match
        // (In headless mode there are no QScreen objects, so findScreenByIdOrName
        // returns nullptr for both sides; screensMatch returns false when both
        // are non-virtual and neither resolves to a QScreen. The fast path a==b
        // handles the identical case.)
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QVERIFY(Phosphor::Screens::ScreenIdentity::screensMatch(physId, physId));
    }

    // =====================================================================
    // buildOccupiedZoneSet — Virtual Screen filtering
    // =====================================================================

    void testBuildOccupiedZoneSet_sameVirtualScreen_includesWindow()
    {
        // Window snapped on vs:0, query for vs:0 -> zone should appear occupied
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString windowId = QStringLiteral("konsole|aaa-bbb-ccc");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], vsId, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vsId);
        QUuid expectedZoneUuid = m_testLayout->zones().at(0)->id();
        QVERIFY2(occupied.contains(expectedZoneUuid), "Zone should be occupied when querying same virtual screen");
    }

    void testBuildOccupiedZoneSet_differentVirtualScreen_excludesWindow()
    {
        // Window snapped on vs:0, query for vs:1 -> zone should NOT appear occupied
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QString windowId = QStringLiteral("konsole|aaa-bbb-ccc");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vs1);
        QUuid zoneUuid = m_testLayout->zones().at(0)->id();
        QVERIFY2(!occupied.contains(zoneUuid), "Zone should NOT be occupied when querying different virtual screen");
    }

    void testBuildOccupiedZoneSet_physicalQueryVirtualWindow_excludesWindow()
    {
        // Window snapped on virtual screen, query with physical ID -> should NOT match
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QString windowId = QStringLiteral("dolphin|ddd-eee-fff");

        m_service->assignWindowToZone(windowId, m_zoneIds[1], vsId, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(physId);
        QUuid zoneUuid = m_testLayout->zones().at(1)->id();
        QVERIFY2(!occupied.contains(zoneUuid), "Physical ID query should not match windows on virtual screens");
    }

    void testBuildOccupiedZoneSet_virtualQueryPhysicalWindow_excludesWindow()
    {
        // Window snapped on physical screen, query with virtual ID -> should NOT match
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString windowId = QStringLiteral("kate|ggg-hhh-iii");

        m_service->assignWindowToZone(windowId, m_zoneIds[2], physId, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vsId);
        QUuid zoneUuid = m_testLayout->zones().at(2)->id();
        QVERIFY2(!occupied.contains(zoneUuid), "Virtual ID query should not match windows on physical screens");
    }

    void testBuildOccupiedZoneSet_emptyFilter_includesAllWindows()
    {
        // Empty screen filter -> all windows should appear occupied regardless of screen
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vs0, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vs1, 1);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(QString());
        QCOMPARE(occupied.size(), 2);
    }

    void testBuildOccupiedZoneSet_floatingWindowExcluded_virtualScreen()
    {
        // Floating window on virtual screen should be excluded from occupied set
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);
        m_service->setWindowFloating(win1, true);

        QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(vsId);

        QUuid zone0Uuid = m_testLayout->zones().at(0)->id();
        QUuid zone1Uuid = m_testLayout->zones().at(1)->id();
        QVERIFY2(!occupied.contains(zone0Uuid), "Floating window's zone should not appear occupied");
        QVERIFY2(occupied.contains(zone1Uuid), "Non-floating window's zone should appear occupied");
    }

    // =====================================================================
    // getEmptyZones — Virtual Screen scenarios
    // =====================================================================

    void testGetEmptyZones_virtualScreen_returnsValidList()
    {
        // getEmptyZones with virtual screen ID should return a list
        // (may be empty in headless mode since there's no QScreen, but must not crash)
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        EmptyZoneList result = m_service->getEmptyZones(vsId);
        // Just verify it doesn't crash and returns a valid (possibly empty) list
        Q_UNUSED(result);
    }

    void testGetEmptyZones_emptyScreenId_returnsValidList()
    {
        // Empty screen ID fallback should not crash
        EmptyZoneList result = m_service->getEmptyZones(QString());
        Q_UNUSED(result);
    }

    // =====================================================================
    // VirtualScreenId utilities (sanity checks for test infrastructure)
    // =====================================================================

    void testVirtualScreenId_isVirtual()
    {
        QVERIFY(PhosphorIdentity::VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107/vs:0")));
        QVERIFY(!PhosphorIdentity::VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107")));
    }

    void testVirtualScreenId_extractPhysicalId()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107/vs:0")),
                 QStringLiteral("Dell:U2722D:115107"));
        // Non-virtual ID returns itself
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107")),
                 QStringLiteral("Dell:U2722D:115107"));
    }

    void testVirtualScreenId_extractIndex()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:0")), 0);
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:3")), 3);
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107")), -1);
    }

    void testVirtualScreenId_make()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 0),
                 QStringLiteral("Dell:U2722D:115107/vs:0"));
        QCOMPARE(PhosphorIdentity::VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 2),
                 QStringLiteral("Dell:U2722D:115107/vs:2"));
    }

    // =====================================================================
    // Multi-zone assignment across virtual screens
    // =====================================================================

    void testMultiZoneAssignment_acrossVirtualScreens()
    {
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QString windowId = QStringLiteral("konsole|multi-vs");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);
        QCOMPARE(m_service->screenAssignments().value(windowId), vs0);

        // Reassign to a different zone on a different virtual screen
        m_service->assignWindowToZone(windowId, m_zoneIds[1], vs1, 1);
        QCOMPARE(m_service->screenAssignments().value(windowId), vs1);

        // buildOccupiedZoneSet for vs1 should show zone 1 occupied
        QSet<QUuid> occupiedVs1 = m_service->buildOccupiedZoneSet(vs1);
        QUuid zone1Uuid = m_testLayout->zones().at(1)->id();
        QVERIFY(occupiedVs1.contains(zone1Uuid));

        // buildOccupiedZoneSet for vs0 should NOT show zone 0 occupied
        // (window was reassigned away from vs0)
        QSet<QUuid> occupiedVs0 = m_service->buildOccupiedZoneSet(vs0);
        QUuid zone0Uuid = m_testLayout->zones().at(0)->id();
        QVERIFY(!occupiedVs0.contains(zone0Uuid));
    }

    // =====================================================================
    // pruneStaleAssignments
    // =====================================================================

    void testPruneStaleAssignments_removesDeadWindows()
    {
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");
        QString win3 = QStringLiteral("app3|ccc");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);
        m_service->assignWindowToZone(win3, m_zoneIds[2], vsId, 1);

        // Only win1 and win2 are alive
        QSet<QString> alive{win1, win2};
        int pruned = m_service->pruneStaleAssignments(alive);

        QCOMPARE(pruned, 1);
        QVERIFY(m_service->isWindowSnapped(win1));
        QVERIFY(m_service->isWindowSnapped(win2));
        QVERIFY(!m_service->isWindowSnapped(win3));
        // Screen and desktop assignments should also be gone
        QVERIFY(!m_service->screenAssignments().contains(win3));
        QVERIFY(!m_service->desktopAssignments().contains(win3));
    }

    void testPruneStaleAssignments_preservesAliveWindows()
    {
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);

        QSet<QString> alive{win1, win2};
        int pruned = m_service->pruneStaleAssignments(alive);

        QCOMPARE(pruned, 0);
        QVERIFY(m_service->isWindowSnapped(win1));
        QVERIFY(m_service->isWindowSnapped(win2));
    }

    void testPruneStaleAssignments_returnsCount()
    {
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");
        QString win3 = QStringLiteral("app3|ccc");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);
        m_service->assignWindowToZone(win3, m_zoneIds[2], vsId, 1);

        // Only win1 is alive — 2 should be pruned
        QSet<QString> alive{win1};
        int pruned = m_service->pruneStaleAssignments(alive);

        QCOMPARE(pruned, 2);
        QVERIFY(m_service->isWindowSnapped(win1));
        QVERIFY(!m_service->isWindowSnapped(win2));
        QVERIFY(!m_service->isWindowSnapped(win3));
    }

    void testPruneStaleAssignments_emptyAliveSet()
    {
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);

        // Empty alive set — all windows should be pruned
        QSet<QString> alive;
        int pruned = m_service->pruneStaleAssignments(alive);

        QCOMPARE(pruned, 2);
        QVERIFY(!m_service->isWindowSnapped(win1));
        QVERIFY(!m_service->isWindowSnapped(win2));
    }

    void testPruneStaleAssignments_cleansFloatingAndAutotileFloated()
    {
        QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString win1 = QStringLiteral("app1|aaa");
        QString win2 = QStringLiteral("app2|bbb");

        m_service->assignWindowToZone(win1, m_zoneIds[0], vsId, 1);
        m_service->assignWindowToZone(win2, m_zoneIds[1], vsId, 1);
        m_service->setWindowFloating(win1, true);
        m_service->markAutotileFloated(win1);

        // Only win2 is alive — win1 should be fully cleaned
        QSet<QString> alive{win2};
        int pruned = m_service->pruneStaleAssignments(alive);

        QCOMPARE(pruned, 1);
        QVERIFY(!m_service->isWindowFloating(win1));
        QVERIFY(!m_service->isAutotileFloated(win1));
    }

    void testScreensMatch_virtualScreenIdsAreDistinct()
    {
        // Verify that virtual screen IDs with different indices are never
        // considered matching, and that physical vs virtual IDs don't match.
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        // Both are virtual, different index -> should be false (correct behavior)
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(vs0, vs1));

        // Physical parent vs virtual child -> should be false (correct behavior for
        // the "virtual screens are separate screens" model)
        QString physId = QStringLiteral("Dell:U2722D:115107");
        QVERIFY(!Phosphor::Screens::ScreenIdentity::screensMatch(physId, vs0));
    }

    // =====================================================================
    // Cross-virtual-screen occupancy isolation
    // =====================================================================

    void testCrossVirtualScreenIsolation()
    {
        // A window snapped on vs:0 must NOT appear occupied when querying vs:1.
        // This verifies that buildOccupiedZoneSet isolates per virtual screen.
        QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        QString windowId = QStringLiteral("konsole|cross-iso-test");

        // Assign window to zone 0 on vs:0
        m_service->assignWindowToZone(windowId, m_zoneIds[0], vs0, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Query occupied zones for vs:1 — should NOT contain zone 0
        QSet<QUuid> occupiedOnVs1 = m_service->buildOccupiedZoneSet(vs1);
        QUuid zone0Uuid = m_testLayout->zones().at(0)->id();
        QVERIFY2(!occupiedOnVs1.contains(zone0Uuid),
                 "Zone snapped on vs:0 must NOT appear occupied when querying vs:1");

        // Sanity: querying vs:0 SHOULD contain zone 0
        QSet<QUuid> occupiedOnVs0 = m_service->buildOccupiedZoneSet(vs0);
        QVERIFY2(occupiedOnVs0.contains(zone0Uuid), "Zone snapped on vs:0 must appear occupied when querying vs:0");
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutManager* m_layoutManager = nullptr;
    StubSettingsSnapAssist* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestSnapAssistVirtual)
#include "test_snap_assist_virtual.moc"
#include <PhosphorScreens/ScreenIdentity.h>
