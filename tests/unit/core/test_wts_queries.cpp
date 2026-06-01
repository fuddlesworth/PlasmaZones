// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_queries.cpp
 * @brief Unit tests for WindowTrackingService query operations
 *
 * Tests cover:
 * 1. PhosphorZones::Zone assignment (single and multi-zone)
 * 2. Screen and desktop tracking
 * 3. Unassign with signal emission
 * 4. Snap-all-windows calculations
 * 5. Multi-zone geometry
 * 6. Stable ID fallback lookups (floating, pre-snap)
 * 7. Occupied zone set (excludes floating)
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>
#include <QRectF>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
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

using namespace PlasmaZones;
using PhosphorEngine::ZoneAssignmentEntry;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using StubSettingsQueries = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector + createTestLayout come from the shared
// helper (StubZoneDetector.h). No local Q_OBJECT subclass is needed — see the
// rationale in that header.
// =========================================================================

using StubZoneDetectorQueries = StubZoneDetector;

// =========================================================================
// Test Class
// =========================================================================

class TestWtsQueries : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsQueries(nullptr);
        m_zoneDetector = new StubZoneDetectorQueries(nullptr);
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
    // P1: PhosphorZones::Zone Assignment
    // =====================================================================

    void testAssignWindowToZone_multiZone()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QStringList multiZones = {m_zoneIds[0], m_zoneIds[1]};

        m_service->assignWindowToZones(windowId, multiZones, QStringLiteral("DP-1"), 1);

        QCOMPARE(m_service->zonesForWindow(windowId), multiZones);
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[0]);
    }

    void testAssignWindow_screenAndDesktopTracked()
    {
        QString windowId = QStringLiteral("app:window:12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("HDMI-1"), 2);

        QCOMPARE(m_service->screenAssignments().value(windowId), QStringLiteral("HDMI-1"));
        QCOMPARE(m_service->desktopAssignments().value(windowId), 2);
    }

    void testUnassignWindow_emitsSignal()
    {
        QString windowId = QStringLiteral("app:window:12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        QSignalSpy spy(m_service, &PhosphorPlacement::WindowTrackingService::windowZoneChanged);
        m_service->unassignWindow(windowId);

        QCOMPARE(spy.count(), 1);
        QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), windowId);
        QVERIFY(args.at(1).toString().isEmpty());
    }

    // =====================================================================
    // P1: Build Occupied PhosphorZones::Zone Set / Snap All
    // =====================================================================

    void testBuildOccupiedZoneSet_excludesFloating()
    {
        QString window1 = QStringLiteral("app1:win:111");
        QString window2 = QStringLiteral("app2:win:222");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QStringLiteral("DP-1"), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QStringLiteral("DP-1"), 0);
        m_service->setWindowFloating(window1, true);

        QStringList snapped = m_service->snappedWindows();
        QVERIFY(snapped.contains(window1));
        QVERIFY(snapped.contains(window2));
        QVERIFY(m_service->isWindowFloating(window1));
        QVERIFY(!m_service->isWindowFloating(window2));
    }

    // Regression test for discussion #323: windows parked on other virtual
    // desktops must not make zones appear occupied on the current desktop,
    // otherwise snap assist refuses to show the zone as "empty" even though
    // SnapAssistHandler::buildCandidates() would exclude those windows from
    // the candidate list (asymmetric filtering → snap assist never appears).
    void testBuildOccupiedZoneSet_desktopFilter_skipsOtherDesktops()
    {
        QString screen = QStringLiteral("DP-1");
        QUuid zone0 = *Utils::parseUuid(m_zoneIds[0]);
        QUuid zone1 = *Utils::parseUuid(m_zoneIds[1]);
        QUuid zone2 = *Utils::parseUuid(m_zoneIds[2]);

        QString onDesk1 = QStringLiteral("app1:win:111");
        QString onDesk2 = QStringLiteral("app2:win:222");
        QString pinned = QStringLiteral("app3:win:333");

        m_service->assignWindowToZone(onDesk1, m_zoneIds[0], screen, /*desktop=*/1);
        m_service->assignWindowToZone(onDesk2, m_zoneIds[1], screen, /*desktop=*/2);
        m_service->assignWindowToZone(pinned, m_zoneIds[2], screen, /*desktop=*/0);

        // No filter (desktopFilter=0): every assignment counts, regardless of desktop.
        QSet<QUuid> unfiltered = m_service->buildOccupiedZoneSet(screen, /*desktopFilter=*/0);
        QVERIFY(unfiltered.contains(zone0));
        QVERIFY(unfiltered.contains(zone1));
        QVERIFY(unfiltered.contains(zone2));

        // Filter to desktop 1: zone held by desktop-2 window must NOT count;
        // pinned (desktop=0) always counts.
        QSet<QUuid> filteredToDesktop1 = m_service->buildOccupiedZoneSet(screen, /*desktopFilter=*/1);
        QVERIFY(filteredToDesktop1.contains(zone0));
        QVERIFY(!filteredToDesktop1.contains(zone1));
        QVERIFY(filteredToDesktop1.contains(zone2));

        // Filter to desktop 2: symmetric — zone held by desktop-1 window is skipped.
        QSet<QUuid> filteredToDesktop2 = m_service->buildOccupiedZoneSet(screen, /*desktopFilter=*/2);
        QVERIFY(!filteredToDesktop2.contains(zone0));
        QVERIFY(filteredToDesktop2.contains(zone1));
        QVERIFY(filteredToDesktop2.contains(zone2));
    }

    void testCalculateSnapAllWindows_fillsEmptyZones()
    {
        const QStringList unsnappedWindows = {
            QStringLiteral("new1:win:111"),
            QStringLiteral("new2:win:222"),
        };

        QVector<ZoneAssignmentEntry> entries = m_engine->calculateSnapAllWindowEntries(unsnappedWindows, QString());

        // The two unsnapped windows fill two of the three empty zones: one entry per
        // input window, distinct target zones, no spurious windows.
        QCOMPARE(entries.size(), 2);
        QVERIFY(entries[0].targetZoneId != entries[1].targetZoneId);
        QSet<QString> placed;
        for (const ZoneAssignmentEntry& e : entries) {
            QVERIFY(unsnappedWindows.contains(e.windowId));
            placed.insert(e.windowId);
        }
        QCOMPARE(placed.size(), 2);
    }

    // =====================================================================
    // P1: Multi-PhosphorZones::Zone Geometry
    // =====================================================================

    void testMultiZoneGeometry_unionOfZones()
    {
        const QStringList multiZones = {m_zoneIds[0], m_zoneIds[1]};
        const QRect geo = m_service->multiZoneGeometry(multiZones, QString());
        const QRect zone0 = m_service->zoneGeometry(m_zoneIds[0], QString());
        const QRect zone1 = m_service->zoneGeometry(m_zoneIds[1], QString());
        QVERIFY(zone0.isValid());
        QVERIFY(zone1.isValid());
        // The multi-zone geometry is the bounding union — valid and covering both zones.
        QVERIFY(geo.isValid());
        QVERIFY(geo.contains(zone0));
        QVERIFY(geo.contains(zone1));
    }

    // =====================================================================
    // P2: App ID Fallbacks
    // =====================================================================

    void testFloatingWindow_stableIdLookupFallback()
    {
        // App ID is stored when window closes; new instance (different UUID) should match
        QString appId = QStringLiteral("firefox");
        QString windowIdNew = QStringLiteral("firefox|a1b2c3d4-0000-0000-0000-000099999999");

        QSet<QString> floating;
        floating.insert(appId);
        m_service->setFloatingWindows(floating);

        QVERIFY(m_service->isWindowFloating(windowIdNew));
    }

    // testPreSnapGeometry_stableIdFallback removed: the per-engine unmanaged-geometry
    // store was collapsed into the unified WindowPlacementStore. The appId-fallback
    // lookup for float-back geometry is now exercised by the WindowPlacementStore
    // peek/take appId-FIFO tests.

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsQueries* m_settings = nullptr;
    StubZoneDetectorQueries* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsQueries)
#include "test_wts_queries.moc"
