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
#include <PhosphorPlacement/IGeometryResolver.h>
#include <PhosphorEngine/IGeometrySettings.h>

using StubSettingsQueries = StubSettings;

// =========================================================================
// Stub geometry resolver: returns a fixed snap-border inset so the snapped
// frame-geometry inset can be exercised independently of ISettings wiring.
// All gap/padding accessors fall through to defaults (matching the daemon
// resolver's null-settings behaviour) so geometry maths stay deterministic.
// =========================================================================
class StubBorderInsetResolver : public PhosphorPlacement::IGeometryResolver
{
public:
    explicit StubBorderInsetResolver(int inset)
        : m_inset(inset)
    {
    }

    int resolveZonePadding(PhosphorZones::Layout*, const QString&) const override
    {
        return PhosphorEngine::GeometryDefaults::ZonePadding;
    }
    PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout*, const QString&) const override
    {
        return PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    }
    int defaultBorderWidth() const override
    {
        return 2;
    }
    int defaultBorderRadius() const override
    {
        return 0;
    }
    int snapBorderInset() const override
    {
        return m_inset;
    }

private:
    int m_inset;
};

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
    // P1: Snap-border frame inset
    //
    // When the snap show-border setting is on, a snapped window's frame is
    // shrunk by the border width on every side so the border the KWin effect
    // draws on the window edge sits INSIDE the zone, separating adjacent tiles.
    // The fixture m_service has a null resolver (snapBorderInset() == 0), so it
    // is the un-inset baseline; a fresh service with a stub resolver supplies
    // the inset. Same layout + null screen manager → the only delta is the
    // inset.
    // =====================================================================

    void testZoneGeometry_insetBySnapBorder()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        auto* insetService =
            new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr, &resolver);

        const QRect baseline = m_service->zoneGeometry(m_zoneIds[0], QString());
        const QRect inset = insetService->zoneGeometry(m_zoneIds[0], QString());
        QVERIFY(baseline.isValid());
        QVERIFY(inset.isValid());
        QCOMPARE(inset, baseline.adjusted(kInset, kInset, -kInset, -kInset));

        delete insetService;
    }

    void testZoneGeometry_noInsetWhenBorderOff()
    {
        // Inset 0 mirrors snappingShowBorder == false: geometry is unchanged.
        StubBorderInsetResolver resolver(0);
        auto* service =
            new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr, &resolver);

        QCOMPARE(service->zoneGeometry(m_zoneIds[0], QString()), m_service->zoneGeometry(m_zoneIds[0], QString()));

        delete service;
    }

    void testMultiZoneGeometry_insetSpanOnce()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        auto* insetService =
            new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr, &resolver);

        const QStringList multiZones = {m_zoneIds[0], m_zoneIds[1]};
        const QRect baseline = m_service->multiZoneGeometry(multiZones, QString());
        const QRect inset = insetService->multiZoneGeometry(multiZones, QString());
        QVERIFY(baseline.isValid());
        QVERIFY(inset.isValid());
        // The COMBINED span is inset once (not per sub-zone): exactly kInset per
        // side off the union rect.
        QCOMPARE(inset, baseline.adjusted(kInset, kInset, -kInset, -kInset));

        delete insetService;
    }

    // =====================================================================
    // P1: Commit-path inset — rotate + snap-all
    //
    // The snap border inset must be applied to the COMMITTED frame geometry of
    // every snap-engine path that produces a real window frame, not just the
    // WTS wrapper in isolation. SnapEngine::calculateRotation and
    // calculateSnapAllWindowEntries build entry.targetGeometry — the rect that
    // becomes the window's frame via applyGeometriesBatch — so both must route
    // through the inset-applying WTS wrapper. Earlier they called the raw
    // GeometryUtils::getZoneGeometryForScreen, which bypassed the inset and
    // shrank the inter-tile gap on every rotate (the login-then-rotate
    // whack-a-mole the user hit). These tests pin the contract by wiring a
    // SnapEngine to an inset WTS and an un-inset WTS and comparing the produced
    // targetGeometry against each service's own zoneGeometry().
    // =====================================================================

    // Build a SnapEngine whose WTS carries the given border-inset resolver,
    // sharing the fixture layout. Caller owns the returned engine + service
    // via the out-ptr.
    SnapEngine* makeEngineWithResolver(PhosphorPlacement::IGeometryResolver* resolver,
                                       PhosphorPlacement::WindowTrackingService** outService)
    {
        auto* service =
            new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr, resolver);
        auto* engine = new SnapEngine(m_layoutManager, service, m_zoneDetector, nullptr, nullptr);
        engine->setEngineSettings(m_settings);
        service->setSnapState(engine->snapState());
        service->setSnapEngine(engine);
        *outService = service;
        return engine;
    }

    void testCalculateRotation_committedFrameInsetWhenBorderOn()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        PhosphorPlacement::WindowTrackingService* service = nullptr;
        SnapEngine* engine = makeEngineWithResolver(&resolver, &service);

        // Snap one window to zone 0, then rotate clockwise → it targets zone 1.
        const QString windowId = QStringLiteral("app:win:rotate-inset");
        service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);

        const QVector<ZoneAssignmentEntry> entries = engine->calculateRotation(/*clockwise=*/true, QString());
        QCOMPARE(entries.size(), 1);

        const QString targetZoneId = entries[0].targetZoneId;
        // The committed frame must equal the INSET zone geometry, not the raw
        // zone rect — proving the rotate path routes through the wrapper.
        QCOMPARE(entries[0].targetGeometry, service->zoneGeometry(targetZoneId, QString()));
        QCOMPARE(entries[0].targetGeometry,
                 m_service->zoneGeometry(targetZoneId, QString()).adjusted(kInset, kInset, -kInset, -kInset));

        delete engine;
        delete service;
    }

    void testCalculateRotation_committedFrameNotInsetWhenBorderOff()
    {
        StubBorderInsetResolver resolver(0);
        PhosphorPlacement::WindowTrackingService* service = nullptr;
        SnapEngine* engine = makeEngineWithResolver(&resolver, &service);

        const QString windowId = QStringLiteral("app:win:rotate-noinset");
        service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);

        const QVector<ZoneAssignmentEntry> entries = engine->calculateRotation(/*clockwise=*/true, QString());
        QCOMPARE(entries.size(), 1);

        const QString targetZoneId = entries[0].targetZoneId;
        // Inset 0 (border off) → committed frame equals the full zone rect.
        QCOMPARE(entries[0].targetGeometry, m_service->zoneGeometry(targetZoneId, QString()));

        delete engine;
        delete service;
    }

    void testCalculateSnapAllWindows_committedFrameInsetWhenBorderOn()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        PhosphorPlacement::WindowTrackingService* service = nullptr;
        SnapEngine* engine = makeEngineWithResolver(&resolver, &service);

        const QStringList windows = {QStringLiteral("new1:win:111")};
        const QVector<ZoneAssignmentEntry> entries = engine->calculateSnapAllWindowEntries(windows, QString());
        QCOMPARE(entries.size(), 1);

        // The fill-empty-zone commit must inset the frame exactly like the
        // wrapper — same proof as the rotate path.
        QCOMPARE(entries[0].targetGeometry, service->zoneGeometry(entries[0].targetZoneId, QString()));
        QCOMPARE(
            entries[0].targetGeometry,
            m_service->zoneGeometry(entries[0].targetZoneId, QString()).adjusted(kInset, kInset, -kInset, -kInset));

        delete engine;
        delete service;
    }

    void testCalculateSnapAllWindows_committedFrameNotInsetWhenBorderOff()
    {
        StubBorderInsetResolver resolver(0);
        PhosphorPlacement::WindowTrackingService* service = nullptr;
        SnapEngine* engine = makeEngineWithResolver(&resolver, &service);

        const QStringList windows = {QStringLiteral("new1:win:222")};
        const QVector<ZoneAssignmentEntry> entries = engine->calculateSnapAllWindowEntries(windows, QString());
        QCOMPARE(entries.size(), 1);

        QCOMPARE(entries[0].targetGeometry, m_service->zoneGeometry(entries[0].targetZoneId, QString()));

        delete engine;
        delete service;
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
