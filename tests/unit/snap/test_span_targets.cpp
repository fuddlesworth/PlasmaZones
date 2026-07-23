// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for SnapNavigationTargetResolver::getSpanTargetForWindow: the
// keyboard grow/shrink of a multi-zone span. One directional quad drives both
// operations — grow into the adjacent zone(s) when some exist beyond the
// span's edge, otherwise retract the opposite edge. The resolver is pure
// compute over injected interfaces, so it is driven here with a light
// tracking fake plus a real LayoutRegistry holding a real zone layout.

#include <QHash>
#include <QRect>
#include <QTest>

#include <memory>
#include <optional>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using PhosphorSnapEngine::SnapNavigationTargetResolver;
using PhosphorSnapEngine::SpanTargetResult;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

QString key(const QString& a, const QString& b)
{
    return a + QLatin1Char('|') + b;
}

const QString kScreen = QStringLiteral("DP-1");
constexpr QRect kScreenRect(0, 0, 1920, 1080);

/// Minimal IWindowTrackingService: only the reads the span path touches are
/// meaningful; the rest return defaults.
class FakeWindowTracking : public PhosphorEngine::IWindowTrackingService
{
public:
    QHash<QString, QStringList> spanOfWindow; // windowId -> zoneIds
    QHash<QString, QRect> zoneGeo; // "zone|screen" -> rect

    QStringList zonesForWindow(const QString& w) const override
    {
        return spanOfWindow.value(w);
    }
    QString zoneForWindow(const QString& w) const override
    {
        const QStringList zones = spanOfWindow.value(w);
        return zones.isEmpty() ? QString() : zones.first();
    }
    QRect zoneGeometry(const QString& z, const QString& s = QString()) const override
    {
        return zoneGeo.value(key(z, s));
    }
    PhosphorScreens::ScreenManager* screenManager() const override
    {
        return nullptr;
    }
    const QHash<QString, QList<PhosphorEngine::PendingRestore>>& pendingRestoreQueues() const override
    {
        return m_pending;
    }
    PhosphorEngine::WindowPlacementStore& placementStore() override
    {
        return m_store;
    }

    // ── Unused by these tests — default stubs ──
    QObject* asQObject() override
    {
        return nullptr;
    }
    QString screenForWindow(const QString&) const override
    {
        return {};
    }
    QString screenForWindow(const QString&, const QString& def) const override
    {
        return def;
    }
    QStringList windowsInZone(const QString&) const override
    {
        return {};
    }
    void assignWindowToZone(const QString&, const QString&, const QString&, int) override
    {
    }
    void assignWindowToZones(const QString&, const QStringList&, const QString&, int) override
    {
    }
    void unassignWindow(const QString&) override
    {
    }
    QStringList recordedSnapZones(const QString&) const override
    {
        return {};
    }
    bool isWindowSnapped(const QString&) const override
    {
        return false;
    }
    QString findEmptyZone(const QString& = QString()) const override
    {
        return {};
    }
    void recordSnapIntent(const QString&, bool) override
    {
    }
    bool isWindowFloating(const QString&) const override
    {
        return false;
    }
    void setWindowFloating(const QString&, bool) override
    {
    }
    void unsnapForFloat(const QString&) override
    {
    }
    bool clearFloatingForSnap(const QString&) override
    {
        return false;
    }
    bool isWindowSticky(const QString&) const override
    {
        return false;
    }
    QStringList preFloatZones(const QString&) const override
    {
        return {};
    }
    QString preFloatScreen(const QString&) const override
    {
        return {};
    }
    void clearPreFloatZone(const QString&) override
    {
    }
    bool clearAutoSnapped(const QString&) override
    {
        return false;
    }
    bool consumePendingAssignment(const QString&) override
    {
        return false;
    }
    void updateLastUsedZone(const QString&, const QString&, const QString&, int) override
    {
    }
    QString currentAppIdFor(const QString&) const override
    {
        return {};
    }
    std::optional<QRect> validatedUnmanagedGeometry(const QString&, const QString&, bool = false) const override
    {
        return std::nullopt;
    }
    void recordFreeGeometry(const QString&, const QString&, const QRect&, bool) override
    {
    }
    void clearFreeGeometry(const QString&) override
    {
    }
    QRect resolveZoneGeometry(const QStringList&, const QString&) const override
    {
        return {};
    }
    QString resolveEffectiveScreenId(const QString& s) const override
    {
        return s;
    }
    QString findEmptyZoneInLayout(PhosphorZones::Layout*, const QString&, int = 0) const override
    {
        return {};
    }
    QSet<QUuid> buildOccupiedZoneSet(const QString& = QString(), int = 0) const override
    {
        return {};
    }
    QVector<PhosphorEngine::ResnapEntry> takeResnapBuffer() override
    {
        return {};
    }

private:
    QHash<QString, QList<PhosphorEngine::PendingRestore>> m_pending;
    PhosphorEngine::WindowPlacementStore m_store;
};

class FakeZoneAdjacency : public PhosphorSnapEngine::IZoneAdjacencyResolver
{
public:
    QHash<QString, QString> firstInDir; // "dir|screen" -> zone
    QString getAdjacentZone(const QString&, const QString&, const QString&) const override
    {
        return {};
    }
    QString getFirstZoneInDirection(const QString& dir, const QString& screen) const override
    {
        return firstInDir.value(key(dir, screen));
    }
};

/// Everything a span test needs: an isolated config dir, a registry whose
/// active layout holds zones with the given relative rects, and a tracking
/// fake pre-seeded with each zone's absolute geometry on kScreen. Zone ids
/// are exposed in insertion order for span/expectation wiring.
struct SpanFixture
{
    IsolatedConfigGuard guard;
    FakeWindowTracking wts;
    FakeZoneAdjacency adj;
    std::unique_ptr<PhosphorZones::LayoutRegistry> registry;
    QStringList zoneIds;

    explicit SpanFixture(const QVector<QRectF>& relativeRects)
        : registry(PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")))
    {
        auto* layout = new PhosphorZones::Layout(QStringLiteral("SpanTestLayout"), registry.get());
        int number = 1;
        for (const QRectF& rel : relativeRects) {
            auto* zone = new PhosphorZones::Zone(layout);
            zone->setRelativeGeometry(rel);
            zone->setZoneNumber(number++);
            layout->addZone(zone);
            const QString id = zone->id().toString();
            zoneIds.append(id);
            const QRect abs(qRound(rel.x() * kScreenRect.width()), qRound(rel.y() * kScreenRect.height()),
                            qRound(rel.width() * kScreenRect.width()), qRound(rel.height() * kScreenRect.height()));
            wts.zoneGeo[key(id, kScreen)] = abs;
        }
        registry->addLayout(layout);
        registry->setActiveLayout(layout);
    }

    SnapNavigationTargetResolver makeResolver()
    {
        return SnapNavigationTargetResolver(&wts, registry.get(), &adj, {});
    }
};

/// 2x2 grid: z0 top-left, z1 top-right, z2 bottom-left, z3 bottom-right.
QVector<QRectF> quadrants()
{
    return {QRectF(0.0, 0.0, 0.5, 0.5), QRectF(0.5, 0.0, 0.5, 0.5), QRectF(0.0, 0.5, 0.5, 0.5),
            QRectF(0.5, 0.5, 0.5, 0.5)};
}

} // namespace

class TestSpanTargets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void grow_right_addsAdjacentZoneOnly();
    void grow_right_sweepsStackedColumn();
    void grow_down_addsZoneBelow();
    void shrink_pressingBackTowardSpan_removesTrailingZone();
    void shrink_afterVerticalGrow_pressingUp_removesBottomZone();
    void singleZoneAtBoundary_reportsNoAdjacentZone();
    void horizontalBoundary_onVerticalStack_doesNotShrink();
    void unsnapped_snapsIntoEdgeZone();
    void staleMembers_fallBackToEdgeZone();
    void mixedStaleMembers_dropsDeadIdOnGrow();
    void unsnappedEmptyLayout_reportsNoZones();
    void shrinkTolerance_dropsJitteredTrailingBandTogether();
    void jitterSliverInBand_doesNotJoinSweep();
    void jitterEdgeCandidate_isNotGrowTarget();
    void growTie_prefersPerpendicularNearerCandidate();
    void gapBearingLayout_growsAcrossGap();
    void diagonalOnlyCandidate_doesNotGrow();
    void overlappingCandidate_beatsDistantZone();
    void invalidDirection_reportsInvalidDirection();
    void emptyWindowId_reportsInvalidWindow();
};

void TestSpanTargets::grow_right_addsAdjacentZoneOnly()
{
    // Top-left quadrant grows right: the extension band covers only the top
    // half, so the top-right zone joins and the bottom-right does not.
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0], f.zoneIds[1]}));
    QCOMPARE(result.geometry, QRect(0, 0, 1920, 540));
    QCOMPARE(result.screenName, kScreen);
}

void TestSpanTargets::grow_right_sweepsStackedColumn()
{
    // Left half is one full-height zone; right half is two stacked zones.
    // Growing right must take the WHOLE stacked column: adding only the
    // nearest zone would leave an L-shaped set whose bounding rect covers a
    // zone that was never added.
    SpanFixture f({QRectF(0.0, 0.0, 0.5, 1.0), QRectF(0.5, 0.0, 0.5, 0.5), QRectF(0.5, 0.5, 0.5, 0.5)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds.size(), 3);
    QVERIFY(result.zoneIds.contains(f.zoneIds[1]));
    QVERIFY(result.zoneIds.contains(f.zoneIds[2]));
    QCOMPARE(result.geometry, kScreenRect);
}

void TestSpanTargets::grow_down_addsZoneBelow()
{
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("down"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0], f.zoneIds[2]}));
    QCOMPARE(result.geometry, QRect(0, 0, 960, 1080));
}

void TestSpanTargets::shrink_pressingBackTowardSpan_removesTrailingZone()
{
    // Span covers the top row; the window sits at the left screen edge, so
    // pressing left cannot grow — it retracts the right edge instead,
    // undoing the earlier grow.
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0], f.zoneIds[1]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("left"), kScreen);

    QVERIFY(result.success);
    QVERIFY(!result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0]}));
    QCOMPARE(result.geometry, QRect(0, 0, 960, 540));
}

void TestSpanTargets::shrink_afterVerticalGrow_pressingUp_removesBottomZone()
{
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0], f.zoneIds[2]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("up"), kScreen);

    QVERIFY(result.success);
    QVERIFY(!result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0]}));
    QCOMPARE(result.geometry, QRect(0, 0, 960, 540));
}

void TestSpanTargets::singleZoneAtBoundary_reportsNoAdjacentZone()
{
    // A single-zone span at the left screen edge can neither grow left nor
    // shrink; the boundary is a hard stop (span never crosses outputs).
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("left"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("no_adjacent_zone"));
    QCOMPARE(result.sourceZoneId, f.zoneIds[0]);
}

void TestSpanTargets::horizontalBoundary_onVerticalStack_doesNotShrink()
{
    // Span covers the left column (both left quadrants). Pressing left: no
    // zone to grow into, and every member forms the trailing right-edge band,
    // so a shrink would empty the span. The correct outcome is a boundary
    // report, not a vertical span collapsing on a horizontal keypress.
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0], f.zoneIds[2]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("left"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("no_adjacent_zone"));
}

void TestSpanTargets::unsnapped_snapsIntoEdgeZone()
{
    // An unsnapped window mirrors move's entry behaviour: snap into the edge
    // zone in the pressed direction (via the adjacency resolver).
    SpanFixture f(quadrants());
    f.adj.firstInDir[key(QStringLiteral("right"), kScreen)] = f.zoneIds[1];

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[1]}));
    QCOMPARE(result.geometry, QRect(960, 0, 960, 540));
}

void TestSpanTargets::staleMembers_fallBackToEdgeZone()
{
    // A span whose stored zone ids no longer resolve to valid geometry (the
    // layout changed underneath it) must not carry the dead ids forward. A
    // fully stale span behaves like an unsnapped window and snaps into the
    // edge zone in the pressed direction.
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {QStringLiteral("{dead-zone-id}")};
    f.adj.firstInDir[key(QStringLiteral("right"), kScreen)] = f.zoneIds[1];

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[1]}));
    QVERIFY(!result.zoneIds.contains(QStringLiteral("{dead-zone-id}")));
}

void TestSpanTargets::mixedStaleMembers_dropsDeadIdOnGrow()
{
    // A partially stale span (one live member, one dead id) grows on the live
    // member alone. The dead id must not be re-included from the raw stored
    // zone list — it could even become the primary zone the commit keys off.
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0], QStringLiteral("{dead-zone-id}")};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0], f.zoneIds[1]}));
    QVERIFY(!result.zoneIds.contains(QStringLiteral("{dead-zone-id}")));
    QCOMPARE(result.geometry, QRect(0, 0, 1920, 540));
}

void TestSpanTargets::unsnappedEmptyLayout_reportsNoZones()
{
    // Unsnapped window and the adjacency resolver reports no entry zone in
    // the pressed direction: the failure leg must report no_zones, not crash
    // or claim a direction problem.
    SpanFixture f(quadrants());

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("no_zones"));
}

void TestSpanTargets::shrinkTolerance_dropsJitteredTrailingBandTogether()
{
    // The right column is two stacked zones whose right edges disagree by
    // 1px of rounding jitter (widths 0.4995 vs 0.5 → 1919 vs 1920). Pressing
    // left must treat both as the same trailing edge and drop them together;
    // an exact-edge comparison would keep the 1919 zone and leave an
    // L-shaped span.
    SpanFixture f({QRectF(0.0, 0.0, 0.5, 1.0), QRectF(0.5, 0.0, 0.4995, 0.5), QRectF(0.5, 0.5, 0.5, 0.5)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0], f.zoneIds[1], f.zoneIds[2]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("left"), kScreen);

    QVERIFY(result.success);
    QVERIFY(!result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0]}));
    QCOMPARE(result.geometry, QRect(0, 0, 960, 1080));
}

void TestSpanTargets::jitterSliverInBand_doesNotJoinSweep()
{
    // A perpendicular neighbour leaking 1px of rounding jitter into the
    // extension band (its top edge rounds to 539 instead of 540) must not be
    // swept into the span: only overlaps beyond kSpanEdgeTolerancePx count.
    SpanFixture f({QRectF(0.0, 0.0, 0.5, 0.5), QRectF(0.5, 0.0, 0.5, 0.5), QRectF(0.5, 0.4995, 0.5, 0.5)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0], f.zoneIds[1]}));
    QCOMPARE(result.geometry, QRect(0, 0, 1920, 540));
}

void TestSpanTargets::jitterEdgeCandidate_isNotGrowTarget()
{
    // A candidate whose far edge exceeds the span's by only 1px of rounding
    // jitter (1921 vs 1920) shares that edge rather than extending past it.
    // Treating it as a grow target would produce a visually-null "grow" that
    // blocks grow-else-retract; the press must report the boundary instead.
    SpanFixture f({QRectF(0.0, 0.0, 1.0, 0.5), QRectF(0.5, 0.25, 0.5005, 0.5)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("no_adjacent_zone"));
}

void TestSpanTargets::growTie_prefersPerpendicularNearerCandidate()
{
    // Two candidates tie on edge gap (both flush with the member's right
    // edge); the perpendicular-centre tie-break must pick the aligned one.
    // The off-centre candidate is inserted FIRST so insertion order alone
    // would pick it — only the perp comparison selects the aligned zone.
    // The winner is observable through the success feedback's target zone.
    SpanFixture f({QRectF(0.0, 0.25, 0.5, 0.5), QRectF(0.5, 0.25, 0.5, 0.75), QRectF(0.5, 0.25, 0.25, 0.5)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    QString feedbackReason;
    QString feedbackTarget;
    SnapNavigationTargetResolver resolver(
        &f.wts, f.registry.get(), &f.adj,
        [&](bool, const QString&, const QString& reason, const QString&, const QString& targetZoneId, const QString&) {
            feedbackReason = reason;
            feedbackTarget = targetZoneId;
        });
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(feedbackReason, QStringLiteral("grow:right"));
    QCOMPARE(feedbackTarget, f.zoneIds[2]);
}

void TestSpanTargets::gapBearingLayout_growsAcrossGap()
{
    // Two columns separated by a real 76px gap: grow must still find the
    // neighbour across it.
    SpanFixture f({QRectF(0.0, 0.0, 0.48, 1.0), QRectF(0.52, 0.0, 0.48, 1.0)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0], f.zoneIds[1]}));
    QCOMPARE(result.geometry, QRect(0, 0, 1920, 1080));
}

void TestSpanTargets::diagonalOnlyCandidate_doesNotGrow()
{
    // The only zone to the right is diagonal (no perpendicular overlap with
    // the span): it is not a grow target, so the press reports the boundary
    // instead of producing a diagonal span.
    SpanFixture f({QRectF(0.0, 0.0, 0.5, 0.5), QRectF(0.5, 0.5, 0.5, 0.5)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("no_adjacent_zone"));
}

void TestSpanTargets::overlappingCandidate_beatsDistantZone()
{
    // Overlapping-zone layout: candidate B overlaps the member and extends
    // past its right edge (gap clamps to 0); candidate C sits further right
    // with a real gap. B must win, and the extension band (member edge to
    // B's far edge) must not sweep up C, whose rect only touches the band.
    SpanFixture f({QRectF(0.0, 0.0, 0.5, 1.0), QRectF(0.4, 0.0, 0.3, 1.0), QRectF(0.7, 0.0, 0.3, 1.0)});
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), kScreen);

    QVERIFY(result.success);
    QVERIFY(result.grew);
    QCOMPARE(result.zoneIds, (QStringList{f.zoneIds[0], f.zoneIds[1]}));
    QCOMPARE(result.geometry, QRect(0, 0, 1344, 1080));
}

void TestSpanTargets::invalidDirection_reportsInvalidDirection()
{
    SpanFixture f(quadrants());
    f.wts.spanOfWindow[QStringLiteral("w1")] = {f.zoneIds[0]};

    auto resolver = f.makeResolver();
    const SpanTargetResult result =
        resolver.getSpanTargetForWindow(QStringLiteral("w1"), QStringLiteral("diagonal"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("invalid_direction"));
}

void TestSpanTargets::emptyWindowId_reportsInvalidWindow()
{
    SpanFixture f(quadrants());

    auto resolver = f.makeResolver();
    const SpanTargetResult result = resolver.getSpanTargetForWindow(QString(), QStringLiteral("right"), kScreen);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, QStringLiteral("invalid_window"));
}

QTEST_MAIN(TestSpanTargets)
#include "test_span_targets.moc"
