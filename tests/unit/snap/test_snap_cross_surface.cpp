// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for SnapNavigationTargetResolver's cross-output handoff: when a
// directional move/focus finds no adjacent zone on the current output, it must
// cross into the entry zone of the geometrically-adjacent output. The resolver
// is pure compute over injected interfaces, so it is driven here with light
// fakes (no daemon, no KWin, no real layouts).

#include <QHash>
#include <QRect>
#include <QTest>

#include <optional>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>

#include "../helpers/LayoutRegistryTestHelpers.h"

using PhosphorSnapEngine::SnapNavigationTargetResolver;
using PhosphorSnapEngine::SnapState;

namespace {

QString key(const QString& a, const QString& b)
{
    return a + QLatin1Char('|') + b;
}

/// Minimal IWindowTrackingService: only the handful of reads the navigation
/// path touches are meaningful; the rest return defaults.
class FakeWindowTracking : public PhosphorEngine::IWindowTrackingService
{
public:
    QHash<QString, QString> zoneOfWindow; // windowId  -> zoneId
    QHash<QString, QStringList> windowsByZone; // zoneId    -> windowIds
    QHash<QString, QRect> zoneGeo; // "zone|screen" -> rect
    QHash<QString, QString> screenOfWindow; // windowId  -> screenId

    QString zoneForWindow(const QString& w) const override
    {
        return zoneOfWindow.value(w);
    }
    QStringList windowsInZone(const QString& z) const override
    {
        return windowsByZone.value(z);
    }
    QRect zoneGeometry(const QString& z, const QString& s = QString()) const override
    {
        return zoneGeo.value(key(z, s));
    }
    PhosphorScreens::ScreenManager* screenManager() const override
    {
        return nullptr;
    }
    const QHash<QString, QString>& screenAssignments() const override
    {
        return screenOfWindow;
    }
    const QHash<QString, QStringList>& zoneAssignments() const override
    {
        return m_zoneAssign;
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
    QStringList zonesForWindow(const QString&) const override
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
    QHash<QString, QStringList> m_zoneAssign;
    QHash<QString, QList<PhosphorEngine::PendingRestore>> m_pending;
    PhosphorEngine::WindowPlacementStore m_store;
};

class FakeZoneAdjacency : public PhosphorSnapEngine::IZoneAdjacencyResolver
{
public:
    QString adjacent; // getAdjacentZone returns this (empty = boundary)
    QHash<QString, QString> firstInDir; // "dir|screen" -> zone
    QString getAdjacentZone(const QString&, const QString&, const QString&) const override
    {
        return adjacent;
    }
    QString getFirstZoneInDirection(const QString& dir, const QString& screen) const override
    {
        return firstInDir.value(key(dir, screen));
    }
};

class FakeCrossSurface : public PhosphorEngine::ICrossSurfaceResolver
{
public:
    QHash<QString, QString> neighborOut; // "screen|dir" -> neighbour screen
    QString neighborOutputInDirection(const QString& s, const QString& d) const override
    {
        return neighborOut.value(key(s, d));
    }
    int neighborDesktopInDirection(int, const QString&) const override
    {
        return 0;
    }
};

} // namespace

class TestSnapCrossSurface : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void move_noAdjacentZone_crossesToNeighbourOutputEntryZone();
    void focus_noAdjacentZone_focusesWindowInNeighbourOutputEntryZone();
    void focus_crossOutputZoneSharedWithSourceScreen_picksNeighbourOutputWindow();
    void focus_inSurfaceZoneSharedWithSiblingScreen_picksThisScreenWindow();
    void focus_inSurfaceScreenIdSkew_fallsBackToZoneOccupant();
    void swap_inSurfaceZoneSharedWithSiblingScreen_picksThisScreenPartner();
    void cycle_zoneSharedWithSiblingScreen_staysOnThisScreen();
    void move_noNeighbourOutput_reportsBoundary();

    void reassignDesktop_restampsAssignedWindowKeepingZone();
    void windowsOnScreenAndDesktop_filtersByScreenAndDesktopSorted();
};

void TestSnapCrossSurface::move_noAdjacentZone_crossesToNeighbourOutputEntryZone()
{
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    wts.zoneGeo[key(QStringLiteral("z-b"), QStringLiteral("DP-2"))] = QRect(1920, 0, 960, 1080);

    FakeZoneAdjacency adj;
    adj.adjacent = QString(); // no adjacent zone on DP-1 → boundary
    adj.firstInDir[key(QStringLiteral("left"), QStringLiteral("DP-2"))] = QStringLiteral("z-b"); // entry from the left

    FakeCrossSurface cross;
    cross.neighborOut[key(QStringLiteral("DP-1"), QStringLiteral("right"))] = QStringLiteral("DP-2");

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});
    resolver.setCrossSurfaceResolver(&cross);

    const auto result =
        resolver.getMoveTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.zoneId, QStringLiteral("z-b"));
    QCOMPARE(result.screenName, QStringLiteral("DP-2"));
}

void TestSnapCrossSurface::focus_noAdjacentZone_focusesWindowInNeighbourOutputEntryZone()
{
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    wts.zoneGeo[key(QStringLiteral("z-b"), QStringLiteral("DP-2"))] = QRect(1920, 0, 960, 1080);
    wts.windowsByZone[QStringLiteral("z-b")] = {QStringLiteral("w2")};
    wts.screenOfWindow[QStringLiteral("w2")] = QStringLiteral("DP-2"); // entry window lives on the neighbour output

    FakeZoneAdjacency adj;
    adj.adjacent = QString();
    adj.firstInDir[key(QStringLiteral("left"), QStringLiteral("DP-2"))] = QStringLiteral("z-b");

    FakeCrossSurface cross;
    cross.neighborOut[key(QStringLiteral("DP-1"), QStringLiteral("right"))] = QStringLiteral("DP-2");

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});
    resolver.setCrossSurfaceResolver(&cross);

    const auto result =
        resolver.getFocusTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.windowIdToActivate, QStringLiteral("w2"));
    QCOMPARE(result.screenName, QStringLiteral("DP-2"));
}

void TestSnapCrossSurface::focus_crossOutputZoneSharedWithSourceScreen_picksNeighbourOutputWindow()
{
    // The entry zone's UUID also exists on the SOURCE output (one layout drives
    // both monitors). windowsInZone(z-b) therefore returns a window on each
    // screen; the cross-output focus must pick the one on the neighbour output
    // (DP-2), never the same-zone window still on the source (DP-1).
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    wts.zoneGeo[key(QStringLiteral("z-b"), QStringLiteral("DP-2"))] = QRect(1920, 0, 960, 1080);
    // wSrc is in zone z-b but on DP-1; wDst is in zone z-b on DP-2.
    wts.windowsByZone[QStringLiteral("z-b")] = {QStringLiteral("wSrc"), QStringLiteral("wDst")};
    wts.screenOfWindow[QStringLiteral("wSrc")] = QStringLiteral("DP-1");
    wts.screenOfWindow[QStringLiteral("wDst")] = QStringLiteral("DP-2");

    FakeZoneAdjacency adj;
    adj.adjacent = QString();
    adj.firstInDir[key(QStringLiteral("left"), QStringLiteral("DP-2"))] = QStringLiteral("z-b");

    FakeCrossSurface cross;
    cross.neighborOut[key(QStringLiteral("DP-1"), QStringLiteral("right"))] = QStringLiteral("DP-2");

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});
    resolver.setCrossSurfaceResolver(&cross);

    const auto result =
        resolver.getFocusTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.windowIdToActivate, QStringLiteral("wDst")); // the DP-2 occupant, not wSrc
    QCOMPARE(result.screenName, QStringLiteral("DP-2"));
}

void TestSnapCrossSurface::focus_inSurfaceZoneSharedWithSiblingScreen_picksThisScreenWindow()
{
    // In-surface (same-output) directional focus: the adjacent zone's UUID also
    // exists on a sibling monitor (one layout drives both). windowsInZone returns
    // a window on each screen; same-surface focus must pick the one on THIS
    // output (DP-1), not the sibling's — even though the sibling's is list-first.
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    wts.windowsByZone[QStringLiteral("z-target")] = {QStringLiteral("wThere"), QStringLiteral("wHere")};
    wts.screenOfWindow[QStringLiteral("wHere")] = QStringLiteral("DP-1");
    wts.screenOfWindow[QStringLiteral("wThere")] = QStringLiteral("DP-2");

    FakeZoneAdjacency adj;
    adj.adjacent = QStringLiteral("z-target"); // a real in-surface neighbour zone

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});
    // No cross-surface resolver — this stays on the current output.

    const auto result =
        resolver.getFocusTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.windowIdToActivate, QStringLiteral("wHere")); // the DP-1 occupant, not wThere
    QCOMPARE(result.screenName, QStringLiteral("DP-1"));
}

void TestSnapCrossSurface::focus_inSurfaceScreenIdSkew_fallsBackToZoneOccupant()
{
    // Regression guard for the best-effort same-surface filter: if the only
    // occupant of the in-surface target zone is stored under a screen-id form
    // that doesn't string-match effectiveScreenId (virtual vs bare-physical
    // skew), focus must still land on it rather than spuriously reporting
    // no_window_in_zone. The strict screen filter is authoritative only across
    // the cross-OUTPUT boundary.
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    wts.windowsByZone[QStringLiteral("z-target")] = {QStringLiteral("wHere")};
    wts.screenOfWindow[QStringLiteral("wHere")] = QStringLiteral("virtual:DP-1#abc"); // skewed id form

    FakeZoneAdjacency adj;
    adj.adjacent = QStringLiteral("z-target");

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});

    const auto result =
        resolver.getFocusTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.windowIdToActivate, QStringLiteral("wHere")); // fallback occupant, not no_window_in_zone
}

void TestSnapCrossSurface::swap_inSurfaceZoneSharedWithSiblingScreen_picksThisScreenPartner()
{
    // In-surface swap counterpart selection: the adjacent zone's UUID also exists
    // on a sibling monitor. The swap partner must be the occupant on THIS output
    // (DP-1), not the sibling's list-first occupant — mirroring the focus path.
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    wts.zoneGeo[key(QStringLiteral("z-a"), QStringLiteral("DP-1"))] = QRect(0, 0, 960, 1080);
    wts.zoneGeo[key(QStringLiteral("z-target"), QStringLiteral("DP-1"))] = QRect(960, 0, 960, 1080);
    wts.windowsByZone[QStringLiteral("z-target")] = {QStringLiteral("wThere"), QStringLiteral("wHere")};
    wts.screenOfWindow[QStringLiteral("wHere")] = QStringLiteral("DP-1");
    wts.screenOfWindow[QStringLiteral("wThere")] = QStringLiteral("DP-2");

    FakeZoneAdjacency adj;
    adj.adjacent = QStringLiteral("z-target");

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});

    const auto result =
        resolver.getSwapTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.windowId2, QStringLiteral("wHere")); // the DP-1 partner, not list-first wThere
}

void TestSnapCrossSurface::cycle_zoneSharedWithSiblingScreen_staysOnThisScreen()
{
    // Cycling focus within a zone must stay on this output: a layout shared with
    // a sibling monitor shares the zone UUID, so the unfiltered ring would
    // include the sibling's window and cycle focus would jump monitors.
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");
    // Same zone UUID occupied on both outputs; the sibling (wOther/DP-2) sits
    // BETWEEN the two DP-1 windows in iteration order to prove it is skipped.
    wts.windowsByZone[QStringLiteral("z-a")] = {QStringLiteral("w1"), QStringLiteral("wOther"), QStringLiteral("w1b")};
    wts.screenOfWindow[QStringLiteral("w1")] = QStringLiteral("DP-1");
    wts.screenOfWindow[QStringLiteral("w1b")] = QStringLiteral("DP-1");
    wts.screenOfWindow[QStringLiteral("wOther")] = QStringLiteral("DP-2");

    FakeZoneAdjacency adj;
    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});

    const auto result =
        resolver.getCycleTargetForWindow(QStringLiteral("w1"), /*forward=*/true, QStringLiteral("DP-1"));
    QVERIFY(result.success);
    QCOMPARE(result.windowIdToActivate, QStringLiteral("w1b")); // the other DP-1 window, not wOther on DP-2
}

void TestSnapCrossSurface::move_noNeighbourOutput_reportsBoundary()
{
    FakeWindowTracking wts;
    wts.zoneOfWindow[QStringLiteral("w1")] = QStringLiteral("z-a");

    FakeZoneAdjacency adj;
    adj.adjacent = QString();

    FakeCrossSurface cross; // no neighbour configured → empty

    std::unique_ptr<PhosphorZones::LayoutRegistry> layoutManager(
        PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("test-snap-cross")));
    SnapNavigationTargetResolver resolver(&wts, layoutManager.get(), &adj, {});
    resolver.setCrossSurfaceResolver(&cross);

    const auto result =
        resolver.getMoveTargetForWindow(QStringLiteral("w1"), QStringLiteral("right"), QStringLiteral("DP-1"));
    QVERIFY(!result.success);
    // Verify it is specifically the boundary path (no adjacent zone, no neighbour
    // output), not some other failure (invalid window / no zone detection) that
    // would satisfy a bare !success — the test name promises the boundary reason.
    QCOMPARE(result.reason, QStringLiteral("no_adjacent_zone"));
    QVERIFY(result.zoneId.isEmpty());
    QCOMPARE(result.sourceZoneId, QStringLiteral("z-a"));
}

void TestSnapCrossSurface::reassignDesktop_restampsAssignedWindowKeepingZone()
{
    // The state primitive cross-desktop move relies on: re-stamp a snapped
    // window's desktop, keep its zone; no-op for unassigned / same-desktop.
    SnapState state(QStringLiteral("DP-1"));
    state.assignWindowToZone(QStringLiteral("w1"), QStringLiteral("z-a"), QStringLiteral("DP-1"), 1);
    QCOMPARE(state.desktopForWindow(QStringLiteral("w1")), 1);

    QVERIFY(state.reassignDesktop(QStringLiteral("w1"), 2));
    QCOMPARE(state.desktopForWindow(QStringLiteral("w1")), 2);
    QCOMPARE(state.zoneForWindow(QStringLiteral("w1")), QStringLiteral("z-a")); // zone preserved

    QVERIFY(!state.reassignDesktop(QStringLiteral("ghost"), 3)); // not assigned
    QVERIFY(!state.reassignDesktop(QStringLiteral("w1"), 2)); // already on desktop 2
}

void TestSnapCrossSurface::windowsOnScreenAndDesktop_filtersByScreenAndDesktopSorted()
{
    // The entry-window lookup behind cross-desktop focus: only windows on the
    // given screen AND desktop, sorted by id for a deterministic choice.
    SnapState state(QStringLiteral("DP-1"));
    state.assignWindowToZone(QStringLiteral("wb"), QStringLiteral("z1"), QStringLiteral("DP-1"), 2);
    state.assignWindowToZone(QStringLiteral("wa"), QStringLiteral("z2"), QStringLiteral("DP-1"), 2);
    state.assignWindowToZone(QStringLiteral("wc"), QStringLiteral("z3"), QStringLiteral("DP-1"), 1); // other desktop
    state.assignWindowToZone(QStringLiteral("wd"), QStringLiteral("z4"), QStringLiteral("DP-2"), 2); // other screen

    QCOMPARE(state.windowsOnScreenAndDesktop(QStringLiteral("DP-1"), 2),
             (QStringList{QStringLiteral("wa"), QStringLiteral("wb")}));
    QVERIFY(state.windowsOnScreenAndDesktop(QStringLiteral("DP-1"), 3).isEmpty());
}

QTEST_MAIN(TestSnapCrossSurface)
#include "test_snap_cross_surface.moc"
