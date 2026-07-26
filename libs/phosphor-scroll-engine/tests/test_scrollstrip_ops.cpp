// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

using ScrollTestUtils::defaultParams;
using ScrollTestUtils::isHidden;
using ScrollTestUtils::kHalf;
using ScrollTestUtils::rectOf;
using ScrollTestUtils::resolveContains;

/// a | b | c as three single-tile columns, focus on c. Insert results are
/// checked here (a QVERIFY in a non-test helper cannot abort the caller,
/// so a broken fixture surfaces as this explicit column-count mismatch
/// rather than confusing downstream assertions).
ScrollStrip threeColumns(const ScrollLayoutParams& params)
{
    ScrollStrip strip;
    const bool ok = strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params)
        && strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params)
        && strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params);
    if (!ok || strip.columnCount() != 3) {
        qFatal("threeColumns fixture failed to build");
    }
    return strip;
}

} // namespace

class TestScrollStripOps : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void consumePullsNextColumnsWindow();
    void expelPushesOutToOwnColumn();
    void consumeOrExpelLeftRight();
    void moveColumnAndTiles();
    void moveColumnToFirstLast();
    void widthPresetCycling();
    void widthAdjustByPercent();
    void maximizeColumnToggle();
    void expandToAvailableWidth();
    void windowHeights();
    void heightAdjustAndReset();
    void tabbedColumnLayout();
    void tabbedColumnBehavesLikeNormalStructurally();
    void reconcileAppResize();
    void reconcileGuardsAndEmptyAck();
    void minimizePicksNearestVisibleSibling();
    void renormalizationRespectsAutoFloors();
    void minHeightClampStaysInBudget();
    void centeringPolicySurvivesCollapseAndConsume();
    void takeWindowLeavesFocusPolicyAlone();
    void onOverflowIgnoresShiftedPrevIdxOnRemoval();
    void reconcileLoneTileNeverRecordsHeightIntent();
    void degenerateWorkAreaNeverAsserts();
    void monsterFixedSiblingLeavesAutoTilesVisible();
    void moveActiveColumnToTracksPreMaximizeSlot();
    void centerActiveColumnCentersAndReports();
    void minWidthClampsResolvedColumn();
    void focusAdjacentSkipsFullyMinimizedColumn();
    void updateViewForFocusKeepsRightEdgeDeadSpace();
    void consumeOpenDisplayOverrideSemantics();
};

void TestScrollStripOps::consumePullsNextColumnsWindow()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(1, params)); // focus b

    QVERIFY(strip.consumeWindowIntoColumn(params));
    QCOMPARE(strip.columnCount(), 2);
    QCOMPARE(strip.columns().at(1).tiles.size(), 2);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    // b and c now stack vertically in one column.
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("b")).x(), rectOf(r, QStringLiteral("c")).x());
    QVERIFY(rectOf(r, QStringLiteral("b")).y() < rectOf(r, QStringLiteral("c")).y());
    // No neighbour on the right: nothing to consume.
    QVERIFY(!strip.consumeWindowIntoColumn(params));
}

void TestScrollStripOps::expelPushesOutToOwnColumn()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.consumeWindowIntoColumn(params)); // b+c stacked, active c

    QVERIFY(strip.expelWindowFromColumn(params));
    QCOMPARE(strip.columnCount(), 3);
    QCOMPARE(strip.activeColumnIndex(), 2);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    // A single-tile column cannot expel.
    QVERIFY(!strip.expelWindowFromColumn(params));
}

void TestScrollStripOps::consumeOrExpelLeftRight()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);

    // c is alone: consume-or-expel-left consumes it into b's column.
    QVERIFY(strip.consumeOrExpel(-1, params));
    QCOMPARE(strip.columnCount(), 2);
    QCOMPARE(strip.activeColumnIndex(), 1);
    QCOMPARE(strip.columns().at(1).tiles.size(), 2);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    // c now shares a column: consume-or-expel-left expels it into its own
    // column on the left.
    QVERIFY(strip.consumeOrExpel(-1, params));
    QCOMPARE(strip.columnCount(), 3);
    QCOMPARE(strip.activeColumnIndex(), 1);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));

    // Rightward: alone again, consume into b's column on the right.
    QVERIFY(strip.consumeOrExpel(+1, params));
    QCOMPARE(strip.columnCount(), 2);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    // Leftmost lone column with no left neighbour: no-op.
    QVERIFY(strip.focusFirstColumn(params));
    QVERIFY(!strip.consumeOrExpel(-1, params));
}

void TestScrollStripOps::moveColumnAndTiles()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);

    // Move c left twice: order becomes c a b, focus stays on c.
    QVERIFY(strip.moveActiveColumn(-1, params));
    QVERIFY(strip.moveActiveColumn(-1, params));
    QVERIFY(!strip.moveActiveColumn(-1, params)); // already first
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    // Tile reorder within a column.
    QVERIFY(strip.consumeOrExpel(+1, params)); // c joins a's column below it
    QCOMPARE(strip.columns().at(0).tiles.size(), 2);
    QVERIFY(strip.moveActiveTile(-1));
    QCOMPARE(strip.columns().at(0).tiles.at(0).windowId, QStringLiteral("c"));
    QVERIFY(!strip.moveActiveTile(-1)); // already top
}

void TestScrollStripOps::moveColumnToFirstLast()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(1, params)); // b

    QVERIFY(strip.moveActiveColumnToFirst(params));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));
    QVERIFY(!strip.moveActiveColumnToFirst(params));

    QVERIFY(strip.moveActiveColumnToLast(params));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
    QVERIFY(!strip.moveActiveColumnToLast(params));
}

void TestScrollStripOps::widthPresetCycling()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makePreset(1), ColumnDisplay::Normal, params));

    // Preset 1 (1/2) → cycle forward → preset 2 (2/3) → wraps to 0 (1/3).
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QCOMPARE(strip.activeColumn()->width.presetIdx, 2);
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QCOMPARE(strip.activeColumn()->width.presetIdx, 0);
    QVERIFY(strip.cycleActiveColumnPresetWidth(-1, params));
    QCOMPARE(strip.activeColumn()->width.presetIdx, 2);

    // From a non-preset width the cycle enters at the nearest preset.
    QVERIFY(strip.setActiveColumnWidth(
        ColumnWidth::makeFixed(ScrollStrip::resolveColumnWidthPx(ColumnWidth::makeProportion(0.34), params))));
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QCOMPARE(strip.activeColumn()->width.kind, ColumnWidth::Preset);
    QCOMPARE(strip.activeColumn()->width.presetIdx, 0);
}

void TestScrollStripOps::widthAdjustByPercent()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));

    QVERIFY(strip.adjustActiveColumnWidth(10.0, params)); // +10% of 1200 = +120
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")).width(), 520);
    QVERIFY(strip.adjustActiveColumnWidth(-10.0, params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")).width(), 400);
    // Clamped at the work-area width.
    QVERIFY(strip.adjustActiveColumnWidth(500.0, params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")).width(), params.workArea.width());
}

void TestScrollStripOps::maximizeColumnToggle()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));

    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("b")).width(), params.workArea.width());
    // Still tiled: a keeps its size and its slot in the strip.
    QCOMPARE(rectOf(r, QStringLiteral("a")).width(), 400);

    // Toggle back restores the pre-maximize intent.
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("b")).width(), 400);
}

void TestScrollStripOps::expandToAvailableWidth()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));

    // Viewport 1200, covered 300 + 10 + 300 = 610 → leftover 590 grows b.
    QVERIFY(strip.expandActiveColumnToAvailableWidth(params));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("b")).width(), 890);
    QCOMPARE(rectOf(r, QStringLiteral("a")).width(), 300);
    // Nothing left over now.
    QVERIFY(!strip.expandActiveColumnToAvailableWidth(params));
}

void TestScrollStripOps::windowHeights()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    // A lone tile fills the column height regardless of its intent.
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")).height(), 800);

    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // Even auto split: 800 - 10 gap = 790 → 395 each.
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("a")).height(), 395);
    QCOMPARE(rectOf(r, QStringLiteral("b")).height(), 395);

    // Preset height on the active tile (b): enters the preset list at
    // index 0 (1/3 of the column height, gap-aware).
    QVERIFY(strip.cycleActiveWindowPresetHeight(+1, params));
    r = strip.relayout(params);
    const int presetPx = rectOf(r, QStringLiteral("b")).height();
    // Literal, not the implementation's own formula: 1/3 of the gap-aware
    // 810 span is 270, minus the 10px gap = 260.
    QCOMPARE(presetPx, 260);
    // a absorbs the remainder.
    QCOMPARE(rectOf(r, QStringLiteral("a")).height(), 790 - presetPx);
}

void TestScrollStripOps::heightAdjustAndReset()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    QVERIFY(strip.adjustActiveWindowHeight(10.0, params)); // b: 395 + 80
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("b")).height(), 475);
    QCOMPARE(rectOf(r, QStringLiteral("a")).height(), 790 - 475);

    QVERIFY(strip.resetActiveColumnHeights());
    r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("a")).height(), 395);
    QCOMPARE(rectOf(r, QStringLiteral("b")).height(), 395);
    QVERIFY(!strip.resetActiveColumnHeights()); // already even
}

void TestScrollStripOps::tabbedColumnLayout()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    QVERIFY(strip.toggleActiveColumnTabbed());
    ResolvedStrip r = strip.relayout(params);
    QVERIFY(r.columns.first().tabbed);
    // Only the active tile (c) shows, at full column height. Presence
    // first: a dropped tile would make the !isHidden below pass vacuously.
    QVERIFY(resolveContains(r, QStringLiteral("c")));
    QVERIFY(!isHidden(r, QStringLiteral("c")));
    QVERIFY(isHidden(r, QStringLiteral("a")));
    QVERIFY(isHidden(r, QStringLiteral("b")));
    QCOMPARE(rectOf(r, QStringLiteral("c")).height(), 800);

    // Tab cycling: focus-window-up moves the active tab.
    QVERIFY(strip.focusAdjacentTile(-1));
    r = strip.relayout(params);
    QVERIFY(resolveContains(r, QStringLiteral("b")));
    QVERIFY(!isHidden(r, QStringLiteral("b")));
    QVERIFY(isHidden(r, QStringLiteral("c")));
    QCOMPARE(rectOf(r, QStringLiteral("b")).height(), 800);

    // Tab reorder via move-window-up.
    QVERIFY(strip.moveActiveTile(-1));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));

    // Toggle back to normal restores the vertical stack.
    QVERIFY(strip.toggleActiveColumnTabbed());
    r = strip.relayout(params);
    QVERIFY(!r.columns.first().tabbed);
    QVERIFY(resolveContains(r, QStringLiteral("a")));
    QVERIFY(!isHidden(r, QStringLiteral("a")));
}

void TestScrollStripOps::tabbedColumnBehavesLikeNormalStructurally()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // A tabbed column still consumes...
    QVERIFY(strip.consumeWindowIntoColumn(params));
    QCOMPARE(strip.columns().at(1).tiles.size(), 2);
    QCOMPARE(strip.columns().at(1).display, ColumnDisplay::Tabbed);
    // ...moves...
    QVERIFY(strip.moveActiveColumn(-1, params));
    QCOMPARE(strip.activeColumnIndex(), 0);
    QCOMPARE(strip.columns().at(0).display, ColumnDisplay::Tabbed);
    // ...resizes...
    QVERIFY(strip.adjustActiveColumnWidth(10.0, params));
    // ...and expels, with the expelled window's new column back to normal.
    QVERIFY(strip.expelWindowFromColumn(params));
    QCOMPARE(strip.columns().at(1).tiles.size(), 1);
    QCOMPARE(strip.columns().at(1).display, ColumnDisplay::Normal);
}

void TestScrollStripOps::reconcileAppResize()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    // The client acked 640 wide: the column's intent becomes Fixed(640);
    // the other column is untouched.
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("b"), QSize(640, 780)));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("b")).width(), 640);
    // Lone tile: height intent is NOT recorded (fills the column anyway).
    QCOMPARE(rectOf(r, QStringLiteral("b")).height(), 800);
    QCOMPARE(rectOf(r, QStringLiteral("a")).width(), ScrollStrip::resolveColumnWidthPx(kHalf, params));
    // Same size again: no change reported.
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("b"), QSize(640, 780)));
}

void TestScrollStripOps::reconcileGuardsAndEmptyAck()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    // widthChanged=false: a vertical-only resize must not convert the
    // column's Proportion intent into Fixed pixels.
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("b"), QSize(999, 300), /*widthChanged=*/false));
    QCOMPARE(strip.columns().at(0).width.kind, ColumnWidth::Proportion);

    // Empty and half-empty acks are refused outright: QSize(0,0) is
    // "valid" to QSize but would reconcile into a 1px column, and a
    // zero-height ack would pin a bogus Fixed height. Neither may touch
    // the recorded intents.
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("b"), QSize(0, 0)));
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("b"), QSize(640, 0)));
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("b"), QSize(0, 640)));
    QCOMPARE(strip.columns().at(0).width.kind, ColumnWidth::Proportion);

    // heightChanged=false: a horizontal-only resize must not pin the tile's
    // height intent either (multi-tile column, so height IS recordable).
    // Value-level: the first reconcile above pinned Fixed(300); a failure
    // here names what the intent became instead of a bare "not equal".
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("b"), QSize(999, 555), /*widthChanged=*/false,
                                       /*heightChanged=*/false));
    const int bIdx = strip.columns().at(0).indexOfWindow(QStringLiteral("b"));
    QVERIFY(bIdx >= 0);
    const WindowHeight after = strip.columns().at(0).tiles.at(bIdx).height;
    QCOMPARE(static_cast<int>(after.kind), static_cast<int>(WindowHeight::Fixed));
    QCOMPARE(after.fixedPx, 300);

    // Unknown window: plain no-op.
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("nope"), QSize(100, 100)));
}

void TestScrollStripOps::reconcileLoneTileNeverRecordsHeightIntent()
{
    // The lone-tile guard is invisible in resolved rects (a lone tile
    // fills its column regardless), so pin the recorded INTENT: a
    // height-changing ack on a single-tile column must leave the tile
    // Auto — recording Fixed would fight the work area on the next
    // resolution change and survive a later stack join.
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("solo"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(!strip.reconcileWindowSize(QStringLiteral("solo"), QSize(600, 300), /*widthChanged=*/false,
                                       /*heightChanged=*/true));
    const Column& col = strip.columns().at(0);
    QCOMPARE(col.tiles.at(0).height.kind, WindowHeight::Auto);
    // The same ack on a two-tile column DOES record (control case, so a
    // dropped `tiles.size() > 1` conjunct fails one of the two arms).
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("mate"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusWindow(QStringLiteral("solo"), params));
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("solo"), QSize(600, 300), /*widthChanged=*/false,
                                      /*heightChanged=*/true));
    QCOMPARE(strip.columns().at(0).tiles.at(0).height.kind, WindowHeight::Fixed);
}

void TestScrollStripOps::degenerateWorkAreaNeverAsserts()
{
    // CLAUDE.md edge case: invalid coordinates at the boundary. A null
    // work area flows through relayout and both size adjusters during
    // screen teardown. The guard that matters is resolveColumnWidthPx's
    // workW<=0 early return, whose FIXED branch would otherwise feed
    // qBound(1, px, 0) — an inverted range that aborts a debug daemon —
    // so the fixture MUST hold a Fixed column (a proportion-only strip
    // never reaches the qBound and passes even with the guard deleted).
    ScrollLayoutParams dead;
    dead.workArea = QRect();
    dead.gap = 10;
    ScrollStrip strip;
    QVERIFY(
        strip.insertWindow(QStringLiteral("fx"), ColumnWidth::makeFixed(600), ColumnDisplay::Normal, defaultParams()));
    QVERIFY(strip.insertWindow(QStringLiteral("pr"), kHalf, ColumnDisplay::Normal, defaultParams()));
    const ResolvedStrip resolved = strip.relayout(dead);
    // A zero-width work area drops every column from the resolve (colW
    // resolves to 1px, then the empty-viewport cull removes it) — pin the
    // ACTUAL contract rather than a rect loop that never runs.
    QVERIFY(resolved.columns.isEmpty());
    // Width adjuster refuses on the degenerate area. The HEIGHT adjuster
    // needs a STACKED column or its lone-tile guard returns first and
    // shadows the workH<=0 guard entirely.
    QVERIFY(strip.focusWindow(QStringLiteral("fx"), defaultParams()));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("fx2"), kHalf, ColumnDisplay::Normal, defaultParams()));
    QVERIFY(!strip.adjustActiveColumnWidth(10.0, dead));
    QVERIFY(!strip.adjustActiveWindowHeight(10.0, dead));
}

void TestScrollStripOps::monsterFixedSiblingLeavesAutoTilesVisible()
{
    // OBSERVABLE budget invariant: however outsized a Fixed sibling's
    // intent, every Auto tile still resolves to at least its 1px floor and
    // the column total stays within the exact height budget. Honest scope
    // note: the fixedBudget reservation and the min-height rebalance
    // enforce this JOINTLY — at this fixture they are observationally
    // interchangeable (mutating either alone can be absorbed by the
    // other), so this pins the invariant, not either mechanism in
    // isolation.
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("fixed"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("auto1"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("auto2"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusWindow(QStringLiteral("fixed"), params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(100000)));
    const ResolvedStrip resolved = strip.relayout(params);
    int total = 0;
    for (const QString& id : {QStringLiteral("fixed"), QStringLiteral("auto1"), QStringLiteral("auto2")}) {
        QVERIFY(resolveContains(resolved, id));
        QVERIFY(rectOf(resolved, id).height() >= 1);
        total += rectOf(resolved, id).height();
    }
    // Exact budget: tile heights plus the two inter-tile gaps fit the
    // work area with no slack allowance.
    QVERIFY(total + 2 * params.gap <= params.workArea.height());
}

void TestScrollStripOps::moveActiveColumnToTracksPreMaximizeSlot()
{
    // moveActiveColumnTo is a distinct path from ToFirst/ToLast with its
    // own pre-maximize index fixup in both directions.
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.moveActiveColumnTo(2, params));
    QCOMPARE(strip.columns().at(2).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(strip.activeColumnIndex(), 2);
    QVERIFY(!strip.moveActiveColumnTo(2, params)); // no-op target refused
    QVERIFY(!strip.moveActiveColumnTo(5, params)); // out of range refused

    // Maximize b (index 1), then move the ACTIVE (maximized) column left:
    // toggling back must restore the pre-maximize width, so the restore
    // slot has to follow the move. The pre-maximize width is a DISTINCTIVE
    // Fixed 377 — kHalf equals ScrollLayoutParams::defaultColumnWidth, so
    // with the default the "no stored intent" fallback restores an
    // identical value and a deleted index fixup would pass undetected.
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.setActiveColumnWidth(ColumnWidth::makeFixed(377)));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QVERIFY(strip.moveActiveColumnTo(0, params));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeFixed(377));
}

void TestScrollStripOps::centerActiveColumnCentersAndReports()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.centerActiveColumn(params));
    const ResolvedStrip resolved = strip.relayout(params);
    const QRect b = rectOf(resolved, QStringLiteral("b"));
    QCOMPARE(b.x(), (params.workArea.width() - b.width()) / 2);
}

void TestScrollStripOps::minWidthClampsResolvedColumn()
{
    // The per-tile min-WIDTH clamp in columnWidthPx (min-height has its
    // own budget test; width had nothing).
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("narrow"), ColumnWidth::makeProportion(0.1), ColumnDisplay::Normal,
                               params, /*minWidth=*/400, /*minHeight=*/0));
    QVERIFY(strip.insertWindow(QStringLiteral("other"), kHalf, ColumnDisplay::Normal, params));
    const ResolvedStrip resolved = strip.relayout(params);
    QVERIFY(resolveContains(resolved, QStringLiteral("narrow")));
    QCOMPARE(rectOf(resolved, QStringLiteral("narrow")).width(), 400);
    // setWindowMinimumSize raises it after the fact too.
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("narrow"), 500, 0));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("narrow")).width(), 500);
}

void TestScrollStripOps::focusAdjacentSkipsFullyMinimizedColumn()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    // b's column is fully minimized: focusing right lands on c.
    QVERIFY(strip.focusAdjacentColumn(1, params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
}

void TestScrollStripOps::consumeOpenDisplayOverrideSemantics()
{
    // The optional displayOverride contract: DISENGAGED (plain
    // consume-open) keeps the host column's user-toggled display, an
    // ENGAGED override flips it, and neither touches the host's width
    // intent (an open-rule width would resize every sibling).
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("host"), ColumnWidth::makeFixed(420), ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());
    QCOMPARE(strip.columns().at(0).display, ColumnDisplay::Tabbed);

    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("plain"), kHalf, std::nullopt, params));
    QCOMPARE(strip.columns().at(0).display, ColumnDisplay::Tabbed); // kept
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeFixed(420)); // width intent untouched

    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("untab"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.columns().at(0).display, ColumnDisplay::Normal); // engaged override applies
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeFixed(420));
}

void TestScrollStripOps::updateViewForFocusKeepsRightEdgeDeadSpace()
{
    // The updateViewForFocus early-return's second documented claim: it
    // must not reclaim removeWindowInternal's deliberate right-edge dead
    // space while the active column is fully visible. Deleting the
    // early-return makes the re-clamp scroll the strip right and moves c.
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("d"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusColumn(2, params));
    // Closing d leaves dead space right of c (survivors stay stationary).
    QVERIFY(strip.removeWindow(QStringLiteral("d"), params));
    const int cBefore = rectOf(strip.relayout(params), QStringLiteral("c")).x();
    strip.updateViewForFocus(params);
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("c")).x(), cBefore);
}

void TestScrollStripOps::minimizePicksNearestVisibleSibling()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusWindow(QStringLiteral("b"), params));

    // Minimizing the active MIDDLE tile hands the column's active slot to
    // the NEAREST visible sibling (ties break downward: "c"), not the first
    // tile in the stack.
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    {
        const Column& col = strip.columns().at(0);
        QCOMPARE(col.tiles.at(col.activeTileIdx).windowId, QStringLiteral("c"));
    }

    // Bottom tile minimized: no downward sibling exists, so the search
    // falls back UPWARD (a naive "next index" pick would leave the active
    // slot pointing at the minimized tile itself).
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), false, params));
    QVERIFY(strip.focusWindow(QStringLiteral("c"), params));
    QVERIFY(strip.setWindowMinimized(QStringLiteral("c"), true, params));
    {
        const Column& col = strip.columns().at(0);
        QCOMPARE(col.tiles.at(col.activeTileIdx).windowId, QStringLiteral("b"));
    }

    // Upward fallback again: with "c" still minimized, minimizing "b"
    // resolves at dist=1 via the ABOVE branch (the below neighbour is
    // already hidden) and lands on "a".
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    {
        const Column& col = strip.columns().at(0);
        QCOMPARE(col.tiles.at(col.activeTileIdx).windowId, QStringLiteral("a"));
    }

    // GENUINE dist=2 widening needs both immediate neighbours hidden: five
    // tiles a..e, minimize b and d, focus c, minimize c — the dist-1 probes
    // (d below, b above) both miss and dist=2 lands downward on "e".
    ScrollStrip five;
    for (const QString& id :
         {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")}) {
        if (five.isEmpty()) {
            QVERIFY(five.insertWindow(id, kHalf, ColumnDisplay::Normal, params));
        } else {
            QVERIFY(five.insertWindowIntoActiveColumn(id, kHalf, ColumnDisplay::Normal, params));
        }
    }
    QVERIFY(five.setWindowMinimized(QStringLiteral("b"), true, params));
    QVERIFY(five.setWindowMinimized(QStringLiteral("d"), true, params));
    QVERIFY(five.focusWindow(QStringLiteral("c"), params));
    QVERIFY(five.setWindowMinimized(QStringLiteral("c"), true, params));
    {
        const Column& col = five.columns().at(0);
        QCOMPARE(col.tiles.at(col.activeTileIdx).windowId, QStringLiteral("e"));
    }
}

void TestScrollStripOps::renormalizationRespectsAutoFloors()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    // Pin two tiles to Fixed heights that alone overflow the column, with a
    // third Auto tile keeping its 1px floor: the renormalization must scale
    // the Fixed pair into (availH - autoCount) so the stack still fits.
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("a"), QSize(600, 700), /*widthChanged=*/false));
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("b"), QSize(600, 700), /*widthChanged=*/false));

    const ResolvedStrip r = strip.relayout(params);
    // Column-count independent invariants in one walk: every tile keeps its
    // 1px floor and none lays out past the bottom of the work area.
    for (const ResolvedColumn& rc : r.columns) {
        for (const ResolvedTile& rt : rc.tiles) {
            QVERIFY(rt.rect.height() >= 1);
            QVERIFY2(rt.rect.bottom() <= params.workArea.bottom(),
                     qPrintable(QStringLiteral("%1 bottom %2 > %3")
                                    .arg(rt.windowId)
                                    .arg(rt.rect.bottom())
                                    .arg(params.workArea.bottom())));
        }
    }
}

void TestScrollStripOps::minHeightClampStaysInBudget()
{
    // The post-distribution min-height clamp rebalances by shrinking tiles
    // with slack above their own floor. When the floors ALONE overflow the
    // column (as here: 400+400+200 = 1000 > ~780 avail), the overflow
    // STANDS and trailing tiles lay out below the work area — that is the
    // documented accepted outcome, not a bug. What the budget must
    // guarantee: every tile keeps at least its floor (min(minHeight,
    // availH)), no tile collapses below 1px, and the total never exceeds
    // the sum of the floors (the clamp adds no overflow beyond what the
    // floors demand).
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params, 0, 400));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params, 0, 400));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params, 0, 200));

    const ResolvedStrip r = strip.relayout(params);
    const int availH = params.workArea.height();
    int total = 0;
    for (const ResolvedColumn& rc : r.columns) {
        for (const ResolvedTile& rt : rc.tiles) {
            const int floor_ = rt.windowId == QStringLiteral("c") ? 200 : 400;
            QVERIFY2(
                rt.rect.height() >= qMin(floor_, availH),
                qPrintable(QStringLiteral("%1 shrank below its floor: %2").arg(rt.windowId).arg(rt.rect.height())));
            total += rt.rect.height();
        }
    }
    QVERIFY(total <= 400 + 400 + 200);
}

void TestScrollStripOps::centeringPolicySurvivesCollapseAndConsume()
{
    // keepOrRecenterAnchor regression (pass-2 fix): under Always centering
    // the focused column must re-center after a structural narrowing —
    // deleting the centering early-return leaves it wherever the
    // keep-in-place math put it.
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusColumn(1, params));

    // Consume pulls c into b's column: the strip narrows by one column and
    // the focused column must be re-centered, not merely kept in place.
    QVERIFY(strip.consumeWindowIntoColumn(params));
    const ResolvedStrip afterConsume = strip.relayout(params);
    const QRect bRect = rectOf(afterConsume, QStringLiteral("b"));
    const int expectedX = (params.workArea.width() - bRect.width()) / 2;
    QCOMPARE(bRect.x(), expectedX);

    // Under Always, the focused column must stay pinned to the center
    // through a sibling-tile minimize too (the collapse here changes no
    // column widths — the assertion re-confirms the pin holds across the
    // reanchor that setWindowMinimized triggers).
    QVERIFY(strip.setWindowMinimized(QStringLiteral("c"), true, params));
    const ResolvedStrip afterCollapse = strip.relayout(params);
    const QRect bAfter = rectOf(afterCollapse, QStringLiteral("b"));
    QCOMPARE(bAfter.x(), (params.workArea.width() - bAfter.width()) / 2);
}

void TestScrollStripOps::takeWindowLeavesFocusPolicyAlone()
{
    const auto params = defaultParams();
    {
        ScrollStrip strip = threeColumns(params);
        QVERIFY(strip.focusColumn(0, params));
        // Taking an unfocused window (transfer path) leaves focus on a.
        QVERIFY(strip.takeWindow(QStringLiteral("b"), params));
        QCOMPARE(strip.activeWindowId(), QStringLiteral("a"));
        QCOMPARE(strip.columnCount(), 2);
        QVERIFY(!strip.containsWindow(QStringLiteral("b")));
        QVERIFY(!strip.takeWindow(QStringLiteral("b"), params));
    }
    // Taking the FOCUSED window is the discriminating case: removeWindow's
    // refocus policy would CENTER the successor under Always, takeWindow
    // must not (the caller is mid-transfer and wants zero churn). Under a
    // takeWindow → removeWindowInternal(refocus=true) mutation the
    // successor lands centered and this fails.
    auto centerParams = params;
    centerParams.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, centerParams));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, centerParams));
    // c deliberately NARROWER than b: with equal widths the successor's
    // keep-stationary x accidentally equals its centered x and the two
    // outcomes are indistinguishable.
    QVERIFY(strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeProportion(0.25), ColumnDisplay::Normal,
                               centerParams));
    QVERIFY(strip.focusColumn(1, centerParams));
    const ResolvedStrip before = strip.relayout(centerParams);
    const int aBefore = rectOf(before, QStringLiteral("a")).x();
    QVERIFY(strip.takeWindow(QStringLiteral("b"), centerParams));
    const ResolvedStrip after = strip.relayout(centerParams);
    QVERIFY(resolveContains(after, QStringLiteral("a")));
    // Survivors stay pixel-stationary (a's rect unchanged); the refocus
    // policy would have re-centered the new active column instead.
    QCOMPARE(rectOf(after, QStringLiteral("a")).x(), aBefore);
    const QRect activeRect = rectOf(after, strip.activeWindowId());
    QVERIFY(activeRect.x() != (centerParams.workArea.width() - activeRect.width()) / 2);
}

void TestScrollStripOps::onOverflowIgnoresShiftedPrevIdxOnRemoval()
{
    // removeWindowInternal's prevIdx fixup pin, on a path where the
    // refocus reanchor genuinely RUNS: closing a wide column left of the
    // active one shifts the strip so far that the keep-stationary anchor
    // goes negative and reanchorAfterFocusChange is consulted. The fixup
    // makes prevIdx track the active column itself (so the OnOverflow
    // entering-edge test is skipped and Never-policy pins the column to
    // the left edge); an UNADJUSTED prevIdx names the RIGHT neighbour d,
    // and with c+gap+d overflowing the work area the stale index would
    // wrongly center c instead.
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::OnOverflow;
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.9), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("d"), ColumnWidth::makeProportion(0.8), ColumnDisplay::Normal, params));
    // Arrive at c from b (b+gap+c fits, so the focus itself cannot center
    // — a centered starting anchor would blur the two outcomes).
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.focusColumn(2, params));

    QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
    const ResolvedStrip after = strip.relayout(params);
    const QRect cAfter = rectOf(after, QStringLiteral("c"));
    QVERIFY(resolveContains(after, QStringLiteral("c")));
    // Never-policy left-edge pin; the stale-prevIdx failure mode is the
    // OnOverflow centering at (workW - cWidth) / 2.
    // (x == 0 already excludes the OnOverflow centering, which would land
    // at (workW - width) / 2.)
    QCOMPARE(cAfter.x(), 0);
}

QTEST_APPLESS_MAIN(TestScrollStripOps)
#include "test_scrollstrip_ops.moc"
