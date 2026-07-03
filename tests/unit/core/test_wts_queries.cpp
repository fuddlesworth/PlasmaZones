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
#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using PhosphorEngine::ZoneAssignmentEntry;
using namespace PhosphorSnapEngine;

namespace {

/// Minimal cross-surface resolver: maps a direction to a neighbour desktop,
/// reports no neighbour OUTPUT (so cross-output swap fails and the engine falls
/// through to the cross-desktop path).
class FakeCrossSurfaceQueries : public PhosphorEngine::ICrossSurfaceResolver
{
public:
    int desktopRight = 0;
    QString outputRight; // neighbour OUTPUT to the right (empty = none)
    QString neighborOutputInDirection(const QString&, const QString& dir) const override
    {
        return dir == QLatin1String("right") ? outputRight : QString();
    }
    int neighborDesktopInDirection(int, const QString& dir) const override
    {
        return dir == QLatin1String("right") ? desktopRight : 0;
    }
};

/// Minimal zone-adjacency resolver: no adjacent zone on any output (forces the
/// no_adjacent_zone boundary), no first-in-direction zone.
class FakeAdjacencyQueries : public PhosphorSnapEngine::IZoneAdjacencyResolver
{
public:
    QString getAdjacentZone(const QString&, const QString&, const QString&) const override
    {
        return QString();
    }
    QString getFirstZoneInDirection(const QString&, const QString&) const override
    {
        return QString();
    }
};

} // namespace
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

    int resolveInnerGap(PhosphorZones::Layout*, const QString&) const override
    {
        return PhosphorEngine::GeometryDefaults::InnerGap;
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
        // The multi-zone geometry must be the TIGHT bounding union of the two
        // zones — not merely a rect that happens to contain both (which the whole
        // screen would also satisfy). multiZoneGeometry computes the bounds with
        // QRectF-style exclusive extents, so each edge may sit up to 1px beyond
        // QRect::united's inclusive convention; assert tightness within that 1px.
        const QRect expectedUnion = zone0.united(zone1);
        QVERIFY(geo.isValid());
        QVERIFY(geo.contains(zone0));
        QVERIFY(geo.contains(zone1));
        QVERIFY(qAbs(geo.left() - expectedUnion.left()) <= 1);
        QVERIFY(qAbs(geo.top() - expectedUnion.top()) <= 1);
        QVERIFY(qAbs(geo.right() - expectedUnion.right()) <= 1);
        QVERIFY(qAbs(geo.bottom() - expectedUnion.bottom()) <= 1);
    }

    // =====================================================================
    // P1: Snap-border frame inset (reserved seam)
    //
    // The snap-border inset seam shrinks a snapped window's frame by the inset
    // on every side so a border drawn on the window edge would sit INSIDE the
    // zone, separating adjacent tiles. Production snapBorderInset() is pinned to
    // 0 (no inset); this test exercises the insetRect seam itself with a
    // SYNTHETIC non-zero inset supplied by a stub resolver, NOT a show-border
    // setting. The fixture m_service has a null resolver (snapBorderInset() == 0),
    // so it is the un-inset baseline; a fresh service with a stub resolver
    // supplies the inset. Same layout + null screen manager → the only delta is the
    // inset.
    // =====================================================================

    void testZoneGeometry_insetBySnapBorder()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        // unique_ptr so a failed assertion's early return can't leak the local.
        const auto insetService = std::make_unique<PhosphorPlacement::WindowTrackingService>(
            m_layoutManager, m_zoneDetector, nullptr, nullptr, &resolver);

        const QRect baseline = m_service->zoneGeometry(m_zoneIds[0], QString());
        const QRect inset = insetService->zoneGeometry(m_zoneIds[0], QString());
        QVERIFY(baseline.isValid());
        QVERIFY(inset.isValid());
        QCOMPARE(inset, baseline.adjusted(kInset, kInset, -kInset, -kInset));
    }

    void testZoneGeometry_noInsetWhenBorderOff()
    {
        // Inset 0 mirrors snappingShowBorder == false: geometry is unchanged.
        StubBorderInsetResolver resolver(0);
        const auto service = std::make_unique<PhosphorPlacement::WindowTrackingService>(m_layoutManager, m_zoneDetector,
                                                                                        nullptr, nullptr, &resolver);

        QCOMPARE(service->zoneGeometry(m_zoneIds[0], QString()), m_service->zoneGeometry(m_zoneIds[0], QString()));
    }

    void testMultiZoneGeometry_insetSpanOnce()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        const auto insetService = std::make_unique<PhosphorPlacement::WindowTrackingService>(
            m_layoutManager, m_zoneDetector, nullptr, nullptr, &resolver);

        const QStringList multiZones = {m_zoneIds[0], m_zoneIds[1]};
        const QRect baseline = m_service->multiZoneGeometry(multiZones, QString());
        const QRect inset = insetService->multiZoneGeometry(multiZones, QString());
        QVERIFY(baseline.isValid());
        QVERIFY(inset.isValid());
        // The COMBINED span is inset once (not per sub-zone): exactly kInset per
        // side off the union rect.
        QCOMPARE(inset, baseline.adjusted(kInset, kInset, -kInset, -kInset));
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

    // Helpers live in a plain private section — moc rejects non-slot
    // declarations inside Q_SLOTS.
private:
    // A SnapEngine wired to a WTS carrying a border-inset resolver, sharing the
    // fixture layout. Members declared service-first so reverse-order
    // destruction tears the engine down before the service it references —
    // and unique_ptr ownership means a failed assertion's early return in a
    // test slot cannot leak either object.
    struct EngineWithService
    {
        std::unique_ptr<PhosphorPlacement::WindowTrackingService> service;
        std::unique_ptr<SnapEngine> engine;
    };

    EngineWithService makeEngineWithResolver(PhosphorPlacement::IGeometryResolver* resolver)
    {
        EngineWithService out;
        out.service = std::make_unique<PhosphorPlacement::WindowTrackingService>(m_layoutManager, m_zoneDetector,
                                                                                 nullptr, nullptr, resolver);
        out.engine = std::make_unique<SnapEngine>(m_layoutManager, out.service.get(), m_zoneDetector, nullptr, nullptr);
        out.engine->setEngineSettings(m_settings);
        out.service->setSnapState(out.engine->snapState());
        out.service->setSnapEngine(out.engine.get());
        return out;
    }

private Q_SLOTS:
    void testCalculateRotation_committedFrameInsetWhenBorderOn()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        const EngineWithService f = makeEngineWithResolver(&resolver);

        // Snap one window to zone 0, then rotate clockwise → it targets zone 1.
        const QString windowId = QStringLiteral("app:win:rotate-inset");
        f.service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);

        const QVector<ZoneAssignmentEntry> entries = f.engine->calculateRotation(/*clockwise=*/true, QString());
        QCOMPARE(entries.size(), 1);

        const QString targetZoneId = entries[0].targetZoneId;
        // The committed frame must equal the INSET zone geometry, not the raw
        // zone rect — proving the rotate path routes through the wrapper.
        QCOMPARE(entries[0].targetGeometry, f.service->zoneGeometry(targetZoneId, QString()));
        QCOMPARE(entries[0].targetGeometry,
                 m_service->zoneGeometry(targetZoneId, QString()).adjusted(kInset, kInset, -kInset, -kInset));
    }

    void testCalculateRotation_committedFrameNotInsetWhenBorderOff()
    {
        StubBorderInsetResolver resolver(0);
        const EngineWithService f = makeEngineWithResolver(&resolver);

        const QString windowId = QStringLiteral("app:win:rotate-noinset");
        f.service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);

        const QVector<ZoneAssignmentEntry> entries = f.engine->calculateRotation(/*clockwise=*/true, QString());
        QCOMPARE(entries.size(), 1);

        const QString targetZoneId = entries[0].targetZoneId;
        // Inset 0 (border off) → committed frame equals the full zone rect.
        QCOMPARE(entries[0].targetGeometry, m_service->zoneGeometry(targetZoneId, QString()));
    }

    void testCalculateSnapAllWindows_committedFrameInsetWhenBorderOn()
    {
        constexpr int kInset = 4;
        StubBorderInsetResolver resolver(kInset);
        const EngineWithService f = makeEngineWithResolver(&resolver);

        const QStringList windows = {QStringLiteral("new1:win:111")};
        const QVector<ZoneAssignmentEntry> entries = f.engine->calculateSnapAllWindowEntries(windows, QString());
        QCOMPARE(entries.size(), 1);

        // The fill-empty-zone commit must inset the frame exactly like the
        // wrapper — same proof as the rotate path.
        QCOMPARE(entries[0].targetGeometry, f.service->zoneGeometry(entries[0].targetZoneId, QString()));
        QCOMPARE(
            entries[0].targetGeometry,
            m_service->zoneGeometry(entries[0].targetZoneId, QString()).adjusted(kInset, kInset, -kInset, -kInset));
    }

    void testCalculateSnapAllWindows_committedFrameNotInsetWhenBorderOff()
    {
        StubBorderInsetResolver resolver(0);
        const EngineWithService f = makeEngineWithResolver(&resolver);

        const QStringList windows = {QStringLiteral("new1:win:222")};
        const QVector<ZoneAssignmentEntry> entries = f.engine->calculateSnapAllWindowEntries(windows, QString());
        QCOMPARE(entries.size(), 1);

        QCOMPARE(entries[0].targetGeometry, m_service->zoneGeometry(entries[0].targetZoneId, QString()));
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

    // ── Cross-desktop re-snap (Phase 1): SnapEngine::resolveCrossDesktopZone maps
    //    a window's zone onto the target desktop's layout so a snap→snap
    //    cross-desktop move lands snapped in the equivalent zone, not floating. ──
    void testCrossDesktopZone_sharedLayout_resolvesEquivalentZone()
    {
        // layoutForScreen(screen, desktop, activity) falls back to defaultLayout()
        // when the target (screen,desktop) has no explicit assignment — point the
        // default at the test layout so desktop 2 resolves to the same layout.
        const QString layoutId = m_testLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });

        // Shared layout → the middle zone maps back to itself (1-based position
        // round-trips), and the pixel geometry resolves (QTEST_MAIN primary screen).
        const auto [zoneId, geo] = m_engine->resolveCrossDesktopZone(m_zoneIds.at(1), QStringLiteral("DP-1"), 2);
        QCOMPARE(zoneId, m_zoneIds.at(1));
        QVERIFY(geo.isValid());

        // An unknown source zone has no position → no equivalent zone resolvable.
        const auto [missZone, missGeo] =
            m_engine->resolveCrossDesktopZone(QStringLiteral("not-a-zone"), QStringLiteral("DP-1"), 2);
        QVERIFY(missZone.isEmpty());
    }

    // ── Swap does NOT cross virtual desktops: exchanging with a window on a
    //    desktop you can't see is meaningless (move owns cross-desktop
    //    relocation). A swap at a desktop boundary is a clean no-op. ──
    void testCrossDesktopSwap_isNoOp_doesNotRelocate()
    {
        const QString layoutId = m_testLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });

        // No neighbour output; desktop 2 lies to the right; no in-surface adjacent
        // zone — so the only crossing available is to the adjacent desktop.
        FakeCrossSurfaceQueries cross;
        cross.desktopRight = 2;
        FakeAdjacencyQueries adj;
        m_engine->setZoneAdjacencyResolver(&adj);
        m_engine->setCrossSurfaceResolver(&cross);

        const QString f = QStringLiteral("appF:win:1");
        SnapState* snap = m_engine->snapState();
        snap->assignWindowToZone(f, m_zoneIds.at(2), QStringLiteral("DP-1"), 0);

        QSignalSpy desktopSpy(m_engine, &SnapEngine::windowDesktopMoveRequested);
        QSignalSpy geoSpy(m_engine, &SnapEngine::applyGeometryRequested);

        PhosphorEngine::NavigationContext ctx;
        ctx.windowId = f;
        ctx.screenId = QStringLiteral("DP-1");
        m_engine->swapFocusedInDirection(QStringLiteral("right"), ctx);

        // The window does not relocate and is not re-geometried — swap stops at
        // the desktop boundary.
        QCOMPARE(desktopSpy.count(), 0);
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(snap->windowsOnScreenAndDesktop(QStringLiteral("DP-1"), 0), QStringList{f});

        m_engine->setCrossSurfaceResolver(nullptr);
        m_engine->setZoneAdjacencyResolver(nullptr);
    }

    // ── Cross-mode swap (Phase 4c): swapping a snapped window toward an AUTOTILE
    //    neighbour output defers to the daemon via crossModeSwapRequested (the
    //    two-way exchange), rather than snapping onto the tiled screen. ──
    void testCrossModeSwap_autotileNeighbourOutput_emitsCrossModeSwapRequested()
    {
        const QString layoutId = m_testLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // DP-2 is an AUTOTILE neighbour to the right (current desktop 0).
        PhosphorZones::AssignmentEntry autotileEntry;
        autotileEntry.mode = PhosphorZones::AssignmentEntry::Autotile;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotileEntry);

        FakeCrossSurfaceQueries cross;
        cross.outputRight = QStringLiteral("DP-2");
        FakeAdjacencyQueries adj; // no in-surface adjacent zone → boundary
        m_engine->setZoneAdjacencyResolver(&adj);
        m_engine->setCrossSurfaceResolver(&cross);

        // Focused window snapped in the last zone on DP-1 (current desktop 0).
        const QString f = QStringLiteral("appF:win:1");
        m_engine->snapState()->assignWindowToZone(f, m_zoneIds.at(2), QStringLiteral("DP-1"), 0);

        QSignalSpy swapSpy(m_engine, &SnapEngine::crossModeSwapRequested);

        PhosphorEngine::NavigationContext ctx;
        ctx.windowId = f;
        ctx.screenId = QStringLiteral("DP-1");
        m_engine->swapFocusedInDirection(QStringLiteral("right"), ctx);

        QCOMPARE(swapSpy.count(), 1);
        const QList<QVariant> args = swapSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), f);
        QCOMPARE(args.at(1).toString(), QStringLiteral("DP-2"));
        QCOMPARE(args.at(2).toInt(), 0); // monitor crossing, current desktop
        QCOMPARE(args.at(3).toString(), QStringLiteral("right"));

        m_engine->setCrossSurfaceResolver(nullptr);
        m_engine->setZoneAdjacencyResolver(nullptr);
    }

    // ── Cross-mode MOVE (Phase 3): moving a snapped window toward an AUTOTILE
    //    neighbour output defers to the daemon via crossModeMoveRequested (the
    //    one-way insert), rather than snapping onto the tiled screen. ──
    void testCrossModeMove_autotileNeighbourOutput_emitsCrossModeMoveRequested()
    {
        const QString layoutId = m_testLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        PhosphorZones::AssignmentEntry autotileEntry;
        autotileEntry.mode = PhosphorZones::AssignmentEntry::Autotile;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotileEntry);

        FakeCrossSurfaceQueries cross;
        cross.outputRight = QStringLiteral("DP-2");
        FakeAdjacencyQueries adj;
        m_engine->setZoneAdjacencyResolver(&adj);
        m_engine->setCrossSurfaceResolver(&cross);

        const QString f = QStringLiteral("appF:win:1");
        m_engine->snapState()->assignWindowToZone(f, m_zoneIds.at(2), QStringLiteral("DP-1"), 0);

        QSignalSpy moveSpy(m_engine, &SnapEngine::crossModeMoveRequested);

        PhosphorEngine::NavigationContext ctx;
        ctx.windowId = f;
        ctx.screenId = QStringLiteral("DP-1");
        m_engine->moveFocusedInDirection(QStringLiteral("right"), ctx);

        QCOMPARE(moveSpy.count(), 1);
        const QList<QVariant> args = moveSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), f);
        QCOMPARE(args.at(1).toString(), QStringLiteral("DP-2"));
        QCOMPARE(args.at(2).toInt(), 0); // monitor crossing, current desktop
        QCOMPARE(args.at(3).toString(), QStringLiteral("right"));

        m_engine->setCrossSurfaceResolver(nullptr);
        m_engine->setZoneAdjacencyResolver(nullptr);
    }

    // ── A SNAP (non-autotile) neighbour output with no entry zone is a genuine
    //    boundary: the engine must NOT emit a cross-mode handoff (no
    //    crossModeSwapRequested) — it reports the boundary instead. ──
    void testCrossModeSwap_snapNeighbourOutput_doesNotEmit()
    {
        const QString layoutId = m_testLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // DP-2 has NO assignment → defaults to Snapping mode (not autotile).
        FakeCrossSurfaceQueries cross;
        cross.outputRight = QStringLiteral("DP-2");
        FakeAdjacencyQueries adj; // getFirstZoneInDirection empty → no entry zone
        m_engine->setZoneAdjacencyResolver(&adj);
        m_engine->setCrossSurfaceResolver(&cross);

        const QString f = QStringLiteral("appF:win:1");
        m_engine->snapState()->assignWindowToZone(f, m_zoneIds.at(2), QStringLiteral("DP-1"), 0);

        QSignalSpy swapSpy(m_engine, &SnapEngine::crossModeSwapRequested);
        QSignalSpy feedbackSpy(m_engine, &SnapEngine::navigationFeedback);

        PhosphorEngine::NavigationContext ctx;
        ctx.windowId = f;
        ctx.screenId = QStringLiteral("DP-1");
        m_engine->swapFocusedInDirection(QStringLiteral("right"), ctx);

        QCOMPARE(swapSpy.count(), 0); // snap neighbour → no cross-mode handoff
        // The boundary is reported (a failure feedback for the swap action).
        QVERIFY(feedbackSpy.count() >= 1);
        const QList<QVariant> fb = feedbackSpy.takeLast();
        QCOMPARE(fb.at(0).toBool(), false);
        QCOMPARE(fb.at(2).toString(), QStringLiteral("no_adjacent_zone"));

        m_engine->setCrossSurfaceResolver(nullptr);
        m_engine->setZoneAdjacencyResolver(nullptr);
    }

    // ── Cross-mode cross-DESKTOP MOVE: moving a snapped window to an adjacent
    //    desktop whose layout is AUTOTILE defers to the daemon via
    //    crossModeMoveRequested with the target desktop (not 0). ──
    void testCrossModeMove_autotileTargetDesktop_emitsCrossModeMoveRequested()
    {
        const QString layoutId = m_testLayout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // DP-1's desktop 2 is autotile mode (the current desktop, 0, stays snap).
        PhosphorZones::AssignmentEntry autotileEntry;
        autotileEntry.mode = PhosphorZones::AssignmentEntry::Autotile;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-1"), 2, QString(), autotileEntry);

        // No neighbour output; desktop 2 lies to the right; no in-surface adjacent.
        FakeCrossSurfaceQueries cross;
        cross.desktopRight = 2;
        FakeAdjacencyQueries adj;
        m_engine->setZoneAdjacencyResolver(&adj);
        m_engine->setCrossSurfaceResolver(&cross);

        const QString f = QStringLiteral("appF:win:1");
        m_engine->snapState()->assignWindowToZone(f, m_zoneIds.at(2), QStringLiteral("DP-1"), 0);

        QSignalSpy moveSpy(m_engine, &SnapEngine::crossModeMoveRequested);

        PhosphorEngine::NavigationContext ctx;
        ctx.windowId = f;
        ctx.screenId = QStringLiteral("DP-1");
        m_engine->moveFocusedInDirection(QStringLiteral("right"), ctx);

        QCOMPARE(moveSpy.count(), 1);
        const QList<QVariant> args = moveSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), f);
        QCOMPARE(args.at(1).toString(), QStringLiteral("DP-1")); // same screen, target desktop
        QCOMPARE(args.at(2).toInt(), 2); // the autotile target desktop
        QCOMPARE(args.at(3).toString(), QStringLiteral("right"));

        m_engine->setCrossSurfaceResolver(nullptr);
        m_engine->setZoneAdjacencyResolver(nullptr);
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
