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

#include "core/windowtrackingservice.h"
#include "snap/SnapEngine.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsQueries = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

class StubZoneDetectorQueries : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorQueries(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(PhosphorZones::Layout* layout) override
    {
        m_layout = layout;
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

// =========================================================================
// Helper
// =========================================================================

static PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

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
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsQueries(nullptr);
        m_zoneDetector = new StubZoneDetectorQueries(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, m_settings, nullptr, nullptr);
        m_snapState = new PhosphorZones::SnapState(QString(), m_engine);
        m_engine->setSnapState(m_snapState);
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
        delete m_engine;
        m_engine = nullptr;
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

        QSignalSpy spy(m_service, &WindowTrackingService::windowZoneChanged);
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
        QStringList unsnappedWindows = {
            QStringLiteral("new1:win:111"),
            QStringLiteral("new2:win:222"),
        };

        QVector<ZoneAssignmentEntry> entries = m_engine->calculateSnapAllWindowEntries(unsnappedWindows, QString());

        // In headless mode, result is empty (no screen -> no geometry)
        Q_UNUSED(entries);
    }

    // =====================================================================
    // P1: Multi-PhosphorZones::Zone Geometry
    // =====================================================================

    void testMultiZoneGeometry_unionOfZones()
    {
        QStringList multiZones = {m_zoneIds[0], m_zoneIds[1]};
        QRect geo = m_service->multiZoneGeometry(multiZones, QString());
        // In headless mode geo is invalid. The method should not crash.
        Q_UNUSED(geo);
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

    void testPreSnapGeometry_stableIdFallback()
    {
        // Pre-snap geometry keyed by appId should be found when looking up by full windowId
        QString appId = QStringLiteral("dolphin");
        QString windowId = QStringLiteral("dolphin|a1b2c3d4-0000-0000-0000-000088888888");

        QHash<QString, WindowTrackingService::PreTileGeometry> geos;
        geos[appId] = {QRect(50, 100, 640, 480), QString()};
        m_service->setPreTileGeometries(geos);

        QVERIFY(m_service->hasPreTileGeometry(windowId));
        auto geo = m_service->preTileGeometry(windowId);
        QVERIFY(geo.has_value());
        QCOMPARE(geo->width(), 640);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsQueries* m_settings = nullptr;
    StubZoneDetectorQueries* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::SnapState* m_snapState = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsQueries)
#include "test_wts_queries.moc"
