// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): this file exceeds the 1150-line ceiling
// because it owns the whole PURE STRIP-MODEL operation surface in one
// narrative — consume/expel, column and tile moves, the width and height
// intent families, tabbed columns, reconcile, viewport queries and the
// anchoring/centering policies. Each slot is a short self-contained scenario
// over the same threeColumns fixture; splitting by operation family would
// duplicate the fixture and the declaration-order table of contents below is
// what keeps the length navigable. The ENGINE-level behaviour is already
// split across seven sibling files.

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

/// a | b | c as three single-tile columns, focus on c.
///
/// A broken build returns an EMPTY strip instead of aborting: a QVERIFY in a
/// non-test helper cannot abort the caller, and the qFatal this used to call
/// took the whole binary down with it and discarded ~35 unrelated test
/// results. Every call site follows with QCOMPARE(columnCount(), 3), so a
/// broken fixture fails exactly one test.
ScrollStrip threeColumns(const ScrollLayoutParams& params)
{
    ScrollStrip strip;
    const bool ok = strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params)
        && strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params)
        && strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params);
    return (ok && strip.columnCount() == 3) ? strip : ScrollStrip{};
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
    // Declaration order tracks DEFINITION order from here down. The two had
    // drifted apart, which in a file this long makes the list useless as a
    // table of contents — and the list is also the run order, so a reader
    // chasing a failure could not find the body by scrolling.
    void visibleColumnIndicesTrackTheViewport();
    void rotateVisibleColumnsCyclesWindowsThroughSlots();
    void reconcileLoneTileRecordsHeightIntent();
    void degenerateWorkAreaNeverAsserts();
    void monsterFixedSiblingLeavesAutoTilesVisible();
    void moveActiveColumnToTracksPreMaximizeSlot();
    void moveActiveColumnToShiftsPreMaximizeSlotBothWays();
    void columnInsertShiftsPreMaximizeSlot();
    void reconcilePreMaximizeSlotKeyedOnResizedColumn();
    void centerActiveColumnCentersAndReports();
    void minWidthClampsResolvedColumn();
    void focusAdjacentSkipsFullyMinimizedColumn();
    void consumeOpenDisplayOverrideSemantics();
    void updateViewForFocusKeepsRightEdgeDeadSpace();
    void minimizePicksNearestVisibleSibling();
    void renormalizationRespectsAutoFloors();
    void minHeightClampStaysInBudget();
    void centeringPolicySurvivesCollapseAndConsume();
    void takeWindowLeavesFocusPolicyAlone();
    void leftOfActiveRemovalHoldsTheActiveColumnStill();
    void onOverflowAnchorsLeftOfActiveInsertAgainstTheShiftedPrevIdx();
    void firstInsertAnchorsAtTheStripHead();
    void centerVisibleColumnsCentersTheSpanAndFallsBack();
    void focusTileAtEndSeeksEndsAndSkipsMinimized();
    void scrollViewByClampsTheDeltaNotThePosition();
};

void TestScrollStripOps::consumePullsNextColumnsWindow()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QCOMPARE(strip.columnCount(), 3);
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
    QCOMPARE(strip.columnCount(), 3);
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
    QCOMPARE(strip.columnCount(), 3);

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

    // Rightward: alone again, consume into b's column on the right. The
    // ORDER inside the merged column is pinned like the leftward arm pins
    // its column order: a consumed window APPENDS below the destination's
    // tiles (consumeOrExpel's dest.tiles.append), so c sits under b and the
    // strip order reads a, b, c.
    QVERIFY(strip.consumeOrExpel(+1, params));
    QCOMPARE(strip.columnCount(), 2);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

    // Leftmost lone column with no left neighbour: no-op.
    QVERIFY(strip.focusFirstColumn(params));
    QVERIFY(!strip.consumeOrExpel(-1, params));
}

void TestScrollStripOps::moveColumnAndTiles()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QCOMPARE(strip.columnCount(), 3);

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
    QCOMPARE(strip.columnCount(), 3);
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
    // Value-anchored preset intent: the anchor is the picked FRACTION and
    // cycling steps between vocabulary entries by value (the default list is
    // 1/3, 1/2, 2/3).
    const QList<qreal>& presets = params.presetColumnWidths;
    QVERIFY(
        strip.insertWindow(QStringLiteral("a"), ColumnWidth::makePreset(presets.at(1)), ColumnDisplay::Normal, params));

    // activeColumn() is dereferenced after every mutation below, so it is
    // QVERIFY'd after every mutation: a regression that loses the active
    // column would otherwise segfault the binary and discard the rest of the
    // suite's results rather than failing this one test.
    QVERIFY(strip.activeColumn());

    // Anchor 1/2 → cycle forward → 2/3 → wraps to 1/3 → back to 2/3.
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QVERIFY(strip.activeColumn());
    QCOMPARE(strip.activeColumn()->width.presetFraction, presets.at(2));
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QVERIFY(strip.activeColumn());
    QCOMPARE(strip.activeColumn()->width.presetFraction, presets.at(0));
    QVERIFY(strip.cycleActiveColumnPresetWidth(-1, params));
    QVERIFY(strip.activeColumn());
    QCOMPARE(strip.activeColumn()->width.presetFraction, presets.at(2));

    // From a non-preset width the cycle enters at the nearest preset. The
    // fixture width is a LITERAL, not a resolveColumnWidthPx call — this
    // file's own rule (see the reconcile tests) forbids deriving fixture
    // values through the code under test. The exact pixel does not matter
    // for the verdict; 408 (≈ 0.34 of the viewport) sits comfortably nearer
    // presets[0] (one third, ≈ 400px) than presets[1] (one half, ≈ 600px)
    // under any of the resolver's gap conventions, so the entry preset is
    // unambiguous.
    QVERIFY(strip.setActiveColumnWidth(ColumnWidth::makeFixed(408)));
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QVERIFY(strip.activeColumn());
    QCOMPARE(strip.activeColumn()->width.kind, ColumnWidth::Preset);
    QCOMPARE(strip.activeColumn()->width.presetFraction, presets.at(0));
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
    // A lone tile with the default Auto intent fills the column height.
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
    QCOMPARE(strip.columnCount(), 3);
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
    // ...resizes (kHalf resolves to 595 on the 1200px area, +10% = +120)...
    QVERIFY(strip.adjustActiveColumnWidth(10.0, params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("c")).width(), 715);
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
    // Lone tile: the acked height is recorded and honored (niri parity).
    QCOMPARE(rectOf(r, QStringLiteral("b")).height(), 780);
    // Literal, not ScrollStrip::resolveColumnWidthPx(kHalf, params): computing
    // the expectation with the code under test made this arm agree with any
    // half-column resolver, including a broken one. 595 is what a gap-aware
    // 0.5 of 1200 with a 10px inner gap comes to.
    QCOMPARE(rectOf(r, QStringLiteral("a")).width(), 595);
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

void TestScrollStripOps::visibleColumnIndicesTrackTheViewport()
{
    // Viewport tracking, NOT the zone-number space (that is per-tile and
    // lives in ScrollEngine::visibleTiles): the helper reports which STRIP
    // indices the view currently covers, so scrolling changes the answer.
    // a | b | c focused on c shows strip indices 1 and 2; focusing a
    // scrolls to [a, b] and the same two viewport slots now name 0 and 1.
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QCOMPARE(strip.columnCount(), 3);
    QCOMPARE(strip.visibleColumnIndices(params), QVector<int>({1, 2}));
    QVERIFY(strip.focusFirstColumn(params));
    QCOMPARE(strip.visibleColumnIndices(params), QVector<int>({0, 1}));
}

void TestScrollStripOps::rotateVisibleColumnsCyclesWindowsThroughSlots()
{
    // a | b | c, focus on c: the view shows [b, c] and a sits off-left.
    // Rotate cycles WINDOWS through the visible slots only — a stays put,
    // the slot geometry (widths, positions) does not move, and the active
    // slot keeps its index so focus lands on the window rotated into it.
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QCOMPARE(strip.columnCount(), 3);
    const QRect bSlot = rectOf(strip.relayout(params), QStringLiteral("b"));
    const QRect cSlot = rectOf(strip.relayout(params), QStringLiteral("c"));

    QCOMPARE(strip.rotateVisibleColumns(true, params), 2);
    QCOMPARE(strip.windowsInOrder(), QStringList({QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
    // Slot geometry holds still; the windows traded places.
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("c")), bSlot);
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("b")), cSlot);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));

    // Counterclockwise undoes it.
    QCOMPARE(strip.rotateVisibleColumns(false, params), 2);
    QCOMPARE(strip.windowsInOrder(), QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    // A lone visible column has nothing to rotate with.
    ScrollStrip lone;
    QVERIFY(lone.insertWindow(QStringLiteral("solo"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(lone.rotateVisibleColumns(true, params), 0);
}

void TestScrollStripOps::reconcileLoneTileRecordsHeightIntent()
{
    // niri parity: a lone tile's height intent is honored by relayout, so
    // an interactive vertical resize of a solo window must RECORD Fixed
    // (previously refused, which snapped the window back) and the resolved
    // rect must show the shorter tile with empty column space below it.
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("solo"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("solo"), QSize(600, 300), /*widthChanged=*/false,
                                      /*heightChanged=*/true));
    const Column& col = strip.columns().at(0);
    QCOMPARE(col.tiles.at(0).height.kind, WindowHeight::Fixed);
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("solo")).height(), 300);
    // Reset returns the lone tile to Auto and the full column height.
    QVERIFY(strip.resetActiveColumnHeights());
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("solo")).height(), 800);
    // The height verbs work on a lone tile too (the old refusal spammed a
    // failure OSD per press): shrink by 10% of the 800px work height.
    QVERIFY(strip.adjustActiveWindowHeight(-10.0, params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("solo")).height(), 720);
    // Preset cycle enters from the preset nearest the current 720px:
    // 2/3 of the gap-aware 810 span is 540, minus the 10px gap = 530.
    QVERIFY(strip.cycleActiveWindowPresetHeight(+1, params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("solo")).height(), 530);
}

void TestScrollStripOps::degenerateWorkAreaNeverAsserts()
{
    // CLAUDE.md edge case: invalid coordinates at the boundary. A null
    // work area flows through relayout and both size adjusters during
    // screen teardown. The guard that matters is resolveColumnWidthPx's
    // workW<=0 early return, whose FIXED branch would otherwise feed
    // qBound(1, px, 0) — an inverted range — so the fixture MUST hold a
    // Fixed column (a proportion-only strip never reaches the qBound at
    // all).
    //
    // HONEST SCOPE, so this test is not read as more coverage than it is:
    // an inverted qBound only ABORTS through Q_ASSERT(!(max < min)), which
    // is compiled out of a release build — there qBound(1, px, 0) quietly
    // yields 1. So deleting resolveColumnWidthPx's guard (or
    // adjustActiveColumnWidth's, which then returns false anyway) is caught
    // in a DEBUG build only. The height arm is the one release-detectable
    // kill: without adjustActiveWindowHeight's workH<=0 guard the adjuster
    // resolves 0px, targets 1px, and reports a change instead of refusing.
    ScrollLayoutParams dead;
    dead.workArea = QRect();
    dead.gap = 10;
    ScrollStrip strip;
    QVERIFY(
        strip.insertWindow(QStringLiteral("fx"), ColumnWidth::makeFixed(600), ColumnDisplay::Normal, defaultParams()));
    QVERIFY(strip.insertWindow(QStringLiteral("pr"), kHalf, ColumnDisplay::Normal, defaultParams()));
    const ResolvedStrip resolved = strip.relayout(dead);
    // A zero-width work area drops every column from the resolve:
    // resolveColumnWidthPx answers 1px, columnWidthPx then clamps that to
    // qMin(1, workArea.width()) = 0, and relayout skips any column that
    // resolves to zero width. Pin the ACTUAL contract rather than a rect
    // loop that never runs.
    QVERIFY(resolved.columns.isEmpty());
    // Width adjuster refuses on the degenerate area. The stacked column is
    // historical (the height adjuster once refused lone tiles outright);
    // kept so the fixture keeps exercising the workH<=0 guard on a stack.
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
    QCOMPARE(strip.columnCount(), 3);
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.moveActiveColumnTo(2, params));
    QCOMPARE(strip.columns().at(2).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(strip.activeColumnIndex(), 2);
    QVERIFY(!strip.moveActiveColumnTo(2, params)); // no-op target refused
    QVERIFY(!strip.moveActiveColumnTo(5, params)); // out of range refused

    // Maximize c (the reorder above left the strip as b | c | a, so index 1
    // is c), then move the ACTIVE (maximized) column left:
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

void TestScrollStripOps::moveActiveColumnToShiftsPreMaximizeSlotBothWays()
{
    // The two INDEX-SHIFT arms of moveActiveColumnTo's pre-maximize fixup,
    // which the test above cannot reach: there the maximized column IS the
    // one being moved, here it is a BYSTANDER the move steps over. Each
    // stored width is distinctive (kHalf equals the default, so the "no
    // stored intent" fallback would restore an identical value and a
    // dropped arm would pass undetected).
    const auto params = defaultParams();
    // Returns an empty strip on a broken build rather than aborting the
    // process, exactly like threeColumns; the call sites check the count.
    const auto fourColumns = [&params]() {
        ScrollStrip s;
        for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}) {
            if (!s.insertWindow(id, kHalf, ColumnDisplay::Normal, params)) {
                return ScrollStrip{};
            }
        }
        return s;
    };

    // Rightward: a (index 0) moves to index 3, stepping over the maximized
    // c (index 2), which slides one slot LEFT to index 1.
    {
        ScrollStrip strip = fourColumns();
        QCOMPARE(strip.columnCount(), 4);
        QVERIFY(strip.focusColumn(2, params)); // c
        QVERIFY(strip.setActiveColumnWidth(ColumnWidth::makeFixed(377)));
        QVERIFY(strip.toggleMaximizeActiveColumn(params));
        QVERIFY(strip.focusColumn(0, params)); // a
        QVERIFY(strip.moveActiveColumnTo(3, params));
        QCOMPARE(strip.windowsInOrder(),
                 (QStringList{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("a")}));
        QCOMPARE(strip.activeColumnIndex(), 3);
        // c's restore slot followed it: toggling back returns ITS width.
        QVERIFY(strip.focusColumn(1, params));
        QVERIFY(strip.toggleMaximizeActiveColumn(params));
        QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeFixed(377));
    }

    // Leftward: d (index 3) moves to index 0, stepping over the maximized
    // b (index 1), which slides one slot RIGHT to index 2.
    {
        ScrollStrip strip = fourColumns();
        QCOMPARE(strip.columnCount(), 4);
        QVERIFY(strip.focusColumn(1, params)); // b
        QVERIFY(strip.setActiveColumnWidth(ColumnWidth::makeFixed(288)));
        QVERIFY(strip.toggleMaximizeActiveColumn(params));
        QVERIFY(strip.focusColumn(3, params)); // d
        QVERIFY(strip.moveActiveColumnTo(0, params));
        QCOMPARE(strip.windowsInOrder(),
                 (QStringList{QStringLiteral("d"), QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(strip.activeColumnIndex(), 0);
        QVERIFY(strip.focusColumn(2, params));
        QVERIFY(strip.toggleMaximizeActiveColumn(params));
        QCOMPARE(strip.columns().at(2).width, ColumnWidth::makeFixed(288));
    }
}

void TestScrollStripOps::columnInsertShiftsPreMaximizeSlot()
{
    // A new column opening to the LEFT of a maximized one shifts its index;
    // without insertWindow's pre-maximize bump the stored slot names the
    // wrong column and the un-maximize toggle dead-ends on the default-width
    // fallback (hence the distinctive 456).
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveColumnWidth(ColumnWidth::makeFixed(456))); // b is active
    QVERIFY(strip.toggleMaximizeActiveColumn(params));

    // Focus a, so the new column lands between a and the maximized b.
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.insertWindow(QStringLiteral("x"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("x"), QStringLiteral("b")}));

    QVERIFY(strip.focusColumn(2, params)); // b, now one slot right
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(2).width, ColumnWidth::makeFixed(456));
}

void TestScrollStripOps::reconcilePreMaximizeSlotKeyedOnResizedColumn()
{
    // reconcileWindowSize is the one width mutator keyed on the RESIZED
    // column rather than the active one, so it must leave a maximized
    // column's restore slot alone when some OTHER column is the one being
    // reconciled. Both arms below drive a client resize while column 0 sits
    // maximized and active; either mutation (keying the invalidation on the
    // active index, or hoisting it out of the width-change branch) wipes the
    // slot, and the un-maximize toggle then dead-ends on the default-width
    // fallback of 595px instead of restoring the distinctive 456.
    //
    // The invalidation's own POSITIVE arm — reconciling the maximized column
    // itself — has no observable tail: that reconcile overwrites the column
    // width, so the next toggle re-maximizes and re-stores the slot from
    // scratch whether or not the stale one was cleared.
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(456), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("a2"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));

    // Height-only ack on a tile of the maximized column: no width intent
    // moved, so nothing to invalidate.
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("a"), QSize(1200, 300), false, true));
    // Width ack on the OTHER column while the maximized one stays active.
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("b"), QSize(500, 800), true, false));

    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("a")).width(), 456);
    QCOMPARE(rectOf(r, QStringLiteral("b")).width(), 500);
}

void TestScrollStripOps::centerActiveColumnCentersAndReports()
{
    const auto params = defaultParams();
    ScrollStrip strip = threeColumns(params);
    QCOMPARE(strip.columnCount(), 3);
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.centerActiveColumn(params));
    const ResolvedStrip resolved = strip.relayout(params);
    QVERIFY(resolveContains(resolved, QStringLiteral("b")));
    const QRect b = rectOf(resolved, QStringLiteral("b"));
    QCOMPARE(b.x(), (params.workArea.width() - b.width()) / 2);

    // Already centred: the verb reports NO change, which is what makes its
    // return value worth anything — a centerActiveColumn that answered true
    // unconditionally would drive a relayout and a success OSD every press.
    QVERIFY(!strip.centerActiveColumn(params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("b")), b);
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
    QCOMPARE(strip.columnCount(), 3);
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    // No-change returns, unasserted anywhere else: minimizing an already
    // minimized tile reports false, and so does a min-size write that changes
    // nothing. Both feed emit-on-change gates one layer up, where an
    // unconditional true means a relayout and a placementChanged per event.
    QVERIFY(!strip.setWindowMinimized(QStringLiteral("b"), true, params));
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("b"), 300, 200));
    QVERIFY(!strip.setWindowMinimumSize(QStringLiteral("b"), 300, 200));
    // b's column is fully minimized: focusing right lands on c.
    QVERIFY(strip.focusAdjacentColumn(1, params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    // PARTIALLY minimized is the discriminating case the single-tile fixture
    // above cannot reach: a column with one minimized and one live tile is
    // NOT skipped, because it still occupies strip width and still has
    // something to show. A skip predicate that tested "has any minimized
    // tile" instead of "is fully minimized" lands the focus on a here.
    ScrollStrip partial;
    QVERIFY(partial.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(partial.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(partial.insertWindowIntoActiveColumn(QStringLiteral("b2"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(partial.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(partial.setWindowMinimized(QStringLiteral("b2"), true, params));
    QVERIFY(partial.focusColumn(0, params));
    QVERIFY(partial.focusAdjacentColumn(1, params));
    QCOMPARE(partial.activeWindowId(), QStringLiteral("b"));
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
    // The arrival JOINED the host column rather than opening its own — the
    // display/width assertions above would read the same on a strip that
    // silently grew a second column.
    QCOMPARE(strip.columnCount(), 1);
    QCOMPARE(strip.columns().at(0).tiles.size(), 2);

    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("untab"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.columns().at(0).display, ColumnDisplay::Normal); // engaged override applies
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeFixed(420));
    QCOMPARE(strip.columnCount(), 1);
    QCOMPARE(strip.columns().at(0).tiles.size(), 3);
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
    // c must be IN the resolve on both sides: rectOf answers a null rect for
    // a dropped tile, and null == null would report "it did not move" for a
    // strip that lost the column altogether.
    const ResolvedStrip beforeUpdate = strip.relayout(params);
    QVERIFY(resolveContains(beforeUpdate, QStringLiteral("c")));
    const int cBefore = rectOf(beforeUpdate, QStringLiteral("c")).x();
    strip.updateViewForFocus(params);
    const ResolvedStrip afterUpdate = strip.relayout(params);
    QVERIFY(resolveContains(afterUpdate, QStringLiteral("c")));
    QCOMPARE(rectOf(afterUpdate, QStringLiteral("c")).x(), cBefore);
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
    // third Auto tile keeping its 1px floor, and assert the stack still
    // fits. Honest scope note, same shape as monsterFixedSibling above: the
    // Fixed/Preset renormalization into (availH - autoCount) and the
    // post-clamp slack rebalance enforce this JOINTLY, and at this fixture
    // (no per-tile minHeights, so every tile has slack down to 1px) the
    // rebalance alone can absorb the overflow. This pins the fits-the-column
    // invariant, not the renormalization step in isolation.
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("a"), QSize(600, 700), /*widthChanged=*/false));
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("b"), QSize(600, 700), /*widthChanged=*/false));

    const ResolvedStrip r = strip.relayout(params);
    // Pinned before the walk: the invariants below are per-tile, so a
    // relayout that dropped the stack would satisfy all of them by having
    // nothing to check.
    QCOMPARE(r.columns.size(), 1);
    QCOMPARE(r.columns.at(0).tiles.size(), 3);
    // Every tile keeps its 1px floor and none lays out past the bottom of
    // the work area.
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
    // The walk below is the whole assertion, so its shape is pinned first: a
    // relayout that dropped the column (or its tiles) would run zero
    // iterations and pass every floor check vacuously.
    QCOMPARE(r.columns.size(), 1);
    QCOMPARE(r.columns.at(0).tiles.size(), 3);
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
        QCOMPARE(strip.columnCount(), 3);
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
    // The active window has to BE in the resolve for the centering check to
    // mean anything: rectOf answers a null rect for an absent id, and a null
    // rect's x is 0, which is not the centered x — so the assertion would
    // pass for a strip that lost its active column entirely.
    const QString activeAfter = strip.activeWindowId();
    QVERIFY(!activeAfter.isEmpty());
    QVERIFY(resolveContains(after, activeAfter));
    const QRect activeRect = rectOf(after, activeAfter);
    QVERIFY(activeRect.x() != (centerParams.workArea.width() - activeRect.width()) / 2);
}

void TestScrollStripOps::leftOfActiveRemovalHoldsTheActiveColumnStill()
{
    // niri parity ("A column to the left was removed; preserve the current
    // position"): closing a column LEFT of the active one holds the active
    // column pixel-stationary — the anchor is kept, so the left-side
    // survivors slide right to close the gap while the column the user is
    // looking at never moves. The pre-parity behaviour re-derived the
    // anchor from the old viewX, sliding the WHOLE visible strip left; in
    // this scenario that landed c at the left edge (x == 0), so the two
    // contracts are cleanly told apart.
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::OnOverflow;
    {
        // Wide surviving left neighbour: the kept anchor needs no clamp, so
        // the active column is EXACTLY stationary. The pre-parity slide
        // landed c at x == 0 here, so the contracts cannot be confused.
        ScrollStrip strip;
        QVERIFY(
            strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.9), ColumnDisplay::Normal, params));
        QVERIFY(
            strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeProportion(0.7), ColumnDisplay::Normal, params));
        QVERIFY(
            strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
        QVERIFY(
            strip.insertWindow(QStringLiteral("d"), ColumnWidth::makeProportion(0.8), ColumnDisplay::Normal, params));
        QVERIFY(strip.focusColumn(1, params));
        QVERIFY(strip.focusColumn(2, params));

        const ResolvedStrip before = strip.relayout(params);
        const QRect cBefore = rectOf(before, QStringLiteral("c"));
        const QRect bBefore = rectOf(before, QStringLiteral("b"));
        QVERIFY(cBefore.x() != 0); // the discriminator needs a non-edge start

        QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
        const ResolvedStrip after = strip.relayout(params);
        QVERIFY(resolveContains(after, QStringLiteral("c")));
        // The active column holds its exact screen position, and its left
        // neighbour keeps its relative offset.
        QCOMPARE(rectOf(after, QStringLiteral("c")), cBefore);
        QCOMPARE(rectOf(after, QStringLiteral("b")).x(), bBefore.x());
    }
    {
        // Narrow surviving left neighbour: the surviving strip left of c is
        // narrower than c's old screen offset, so the left-edge rule (never
        // expose space left of the first column) clamps the kept anchor —
        // the active column moves only as far left as that rule requires,
        // landing at the surviving left strip's width, never at the edge.
        ScrollStrip strip;
        QVERIFY(
            strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.9), ColumnDisplay::Normal, params));
        QVERIFY(
            strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
        QVERIFY(
            strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
        QVERIFY(strip.focusColumn(1, params));
        QVERIFY(strip.focusColumn(2, params));

        const ResolvedStrip before = strip.relayout(params);
        const QRect bBefore = rectOf(before, QStringLiteral("b"));
        QVERIFY(rectOf(before, QStringLiteral("c")).x() > bBefore.width() + params.gap);

        QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
        const ResolvedStrip after = strip.relayout(params);
        QVERIFY(resolveContains(after, QStringLiteral("c")));
        QCOMPARE(rectOf(after, QStringLiteral("b")).x(), 0);
        QCOMPARE(rectOf(after, QStringLiteral("c")).x(), bBefore.width() + params.gap);
    }
}

void TestScrollStripOps::onOverflowAnchorsLeftOfActiveInsertAgainstTheShiftedPrevIdx()
{
    // insertWindow's prevIdx fixup, on the position that makes it observable.
    // A LeftOfActive insert lands AT the old active index, so every column
    // from there on shifts right by one: the previously-focused column is now
    // at prevIdx + 1. Left unshifted, prevIdx names the column that just
    // arrived — which IS the active one, so the OnOverflow arm is skipped
    // entirely and the policy silently degrades to Never.
    //
    // Two wide neighbours put the new column far enough into the strip that
    // centering genuinely scrolls the view; at the strip head the centered
    // anchor and the left-edge pin coincide and the two outcomes cannot be
    // told apart (see firstInsertAnchorsAtTheStripHead).
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::OnOverflow;
    ScrollStrip strip;
    const ColumnWidth wide = ColumnWidth::makeProportion(0.9);
    QVERIFY(strip.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, params));
    QCOMPARE(strip.activeColumnIndex(), 1);

    QVERIFY(strip.insertWindow(QStringLiteral("n"), ColumnWidth::makeProportion(0.5), ColumnDisplay::Normal, params, 0,
                               0, ScrollInsertPosition::LeftOfActive));
    // The insert lands between a and b and takes focus.
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("n"), QStringLiteral("b")}));
    QCOMPARE(strip.activeColumnIndex(), 1);

    const ResolvedStrip after = strip.relayout(params);
    QVERIFY(resolveContains(after, QStringLiteral("n")));
    const QRect nRect = rectOf(after, QStringLiteral("n"));
    // n plus a gap plus the previously-focused b overflows the work area, so
    // OnOverflow centers n. With a stale prevIdx the arm is dead and n would
    // keep the entering-edge pin instead.
    QCOMPARE(nRect.x(), (params.workArea.width() - nRect.width()) / 2);
}

void TestScrollStripOps::firstInsertAnchorsAtTheStripHead()
{
    // The companion position. First had no behavioural coverage at all, so
    // this pins what it actually does: the new column heads the strip and
    // takes focus. Index 0 is not a special case for the anchor — the new
    // column overflows against its neighbour exactly like the LeftOfActive
    // row above, so OnOverflow centers it rather than pinning it flush left.
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::OnOverflow;
    ScrollStrip strip;
    const ColumnWidth wide = ColumnWidth::makeProportion(0.9);
    QVERIFY(strip.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, params));

    QVERIFY(strip.insertWindow(QStringLiteral("n"), ColumnWidth::makeProportion(0.5), ColumnDisplay::Normal, params, 0,
                               0, ScrollInsertPosition::First));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("n"), QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(strip.activeColumnIndex(), 0);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("n"));

    const ResolvedStrip after = strip.relayout(params);
    QVERIFY(resolveContains(after, QStringLiteral("n")));
    const QRect nRect = rectOf(after, QStringLiteral("n"));
    QCOMPARE(nRect.x(), (params.workArea.width() - nRect.width()) / 2);
}

void TestScrollStripOps::centerVisibleColumnsCentersTheSpanAndFallsBack()
{
    const auto params = defaultParams();
    const int workW = params.workArea.width();
    // Three narrow columns that all fit the viewport: the verb centers the
    // whole span as a block, not just the active column.
    ScrollStrip strip;
    const ColumnWidth narrow = ColumnWidth::makeProportion(0.25);
    QVERIFY(strip.insertWindow(QStringLiteral("a"), narrow, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), narrow, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), narrow, ColumnDisplay::Normal, params));
    QVERIFY(strip.centerVisibleColumns(params));
    const ResolvedStrip r = strip.relayout(params);
    QVERIFY(resolveContains(r, QStringLiteral("a")));
    const int colW = rectOf(r, QStringLiteral("a")).width();
    const int spanW = 3 * colW + 2 * params.gap;
    QCOMPARE(rectOf(r, QStringLiteral("a")).x(), (workW - spanW) / 2);

    // Already centered: NO change reported (same contract as
    // centerActiveColumn — the return value gates relayout and the OSD).
    QVERIFY(!strip.centerVisibleColumns(params));

    // No column fully visible: columnWidthPx caps every column at the work
    // area, so the only route there is a view parked BETWEEN columns — the
    // shape a raw restored anchor can legitimately take (restoreViewAnchor
    // is deliberately unclamped). The verb then falls back to centering the
    // ACTIVE column.
    // Columns at 0.55 of the work area: two of them plus a gap overflow the
    // viewport, so a view parked 60px into the strip clips a on the left and
    // b on the right, with c fully off — nothing fully visible.
    ScrollStrip straddle;
    const ColumnWidth wideCol = ColumnWidth::makeProportion(0.55);
    QVERIFY(straddle.insertWindow(QStringLiteral("a"), wideCol, ColumnDisplay::Normal, params));
    QVERIFY(straddle.insertWindow(QStringLiteral("b"), wideCol, ColumnDisplay::Normal, params));
    QVERIFY(straddle.insertWindow(QStringLiteral("c"), wideCol, ColumnDisplay::Normal, params));
    const ResolvedStrip pre = straddle.relayout(params);
    QVERIFY(resolveContains(pre, QStringLiteral("a")));
    const int cStripX = 2 * (rectOf(pre, QStringLiteral("a")).width() + params.gap);
    straddle.restoreViewAnchor(cStripX - 60, params); // viewX = 60
    // The fallback's precondition, asserted rather than hand-derived: at
    // this view NOTHING is fully visible (a clipped left, b overflowing
    // right, c past the edge), which is the only state that reaches the
    // centerActiveColumn fallback — without this check the expected value
    // below would also be produced by the span path centering just {c}.
    {
        const ResolvedStrip parked = straddle.relayout(params);
        const int viewLeft = 0;
        const int viewRight = workW;
        bool anyFullyVisible = false;
        for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            const QRect rect = rectOf(parked, id);
            if (!rect.isNull() && rect.left() >= viewLeft && rect.right() < viewRight) {
                anyFullyVisible = true;
            }
        }
        QVERIFY2(!anyFullyVisible, "precondition: no column fully visible, or the fallback arm is not exercised");
    }
    QVERIFY(straddle.centerVisibleColumns(params));
    const ResolvedStrip sr = straddle.relayout(params);
    QVERIFY(resolveContains(sr, QStringLiteral("c")));
    const QRect cRect = rectOf(sr, QStringLiteral("c"));
    QCOMPARE(cRect.x(), (workW - cRect.width()) / 2);
}

void TestScrollStripOps::focusTileAtEndSeeksEndsAndSkipsMinimized()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    QVERIFY(strip.focusTileAtEnd(false));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("a"));
    // Already at the top: no change, same contract as focusAdjacentTile.
    QVERIFY(!strip.focusTileAtEnd(false));
    QVERIFY(strip.focusTileAtEnd(true));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    // A minimized END tile is skipped: top seeks b once a is minimized, and
    // bottom stops at b once c is minimized too.
    QVERIFY(strip.setWindowMinimized(QStringLiteral("a"), true, params));
    QVERIFY(strip.focusTileAtEnd(false));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));
    QVERIFY(strip.setWindowMinimized(QStringLiteral("c"), true, params));
    QVERIFY(!strip.focusTileAtEnd(true)); // b is the last non-minimized tile
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));

    // Every tile minimized: the walk runs off the end and refuses both ways
    // (no crash, no phantom focus of a hidden tile).
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    QVERIFY(!strip.focusTileAtEnd(false));
    QVERIFY(!strip.focusTileAtEnd(true));

    // A single-tile column: both ends ARE the focused tile, so both seeks
    // give the honest no-op verdict.
    ScrollStrip lone;
    QVERIFY(lone.insertWindow(QStringLiteral("only"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(!lone.focusTileAtEnd(false));
    QVERIFY(!lone.focusTileAtEnd(true));
    QCOMPARE(lone.activeWindowId(), QStringLiteral("only"));

    // An empty strip refuses without touching anything.
    ScrollStrip empty;
    QVERIFY(!empty.focusTileAtEnd(false));
    QVERIFY(!empty.focusTileAtEnd(true));
}

void TestScrollStripOps::scrollViewByClampsTheDeltaNotThePosition()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    // Three columns at 0.55 of the work area: the strip overflows, so there
    // is somewhere to scroll to.
    const ColumnWidth wide = ColumnWidth::makeProportion(0.55);
    QVERIFY(strip.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), wide, ColumnDisplay::Normal, params));

    const auto viewX = [&]() {
        return strip.relayout(params).viewX;
    };
    // Refusals first: no delta, and a degenerate work area, both leave the
    // anchor alone. Neither is reachable from the engine's tick (it guards
    // ahead of the call), so this is the only place they are exercised.
    const int before = viewX();
    QVERIFY(!strip.scrollViewBy(0, params));
    QCOMPARE(viewX(), before);
    ScrollLayoutParams degenerate = params;
    degenerate.workArea.setWidth(0);
    QVERIFY(!strip.scrollViewBy(10, degenerate));
    QCOMPARE(viewX(), before);

    // A step moves the view by exactly the delta, in the requested direction.
    QVERIFY(strip.scrollViewBy(-20, params));
    QCOMPARE(viewX(), before - 20);
    QVERIFY(strip.scrollViewBy(20, params));
    QCOMPARE(viewX(), before);

    // Both ends stop rather than overshoot, and report no movement once
    // pinned — which is what lets the caller stop asking.
    QVERIFY(strip.scrollViewBy(-100000, params));
    QCOMPARE(viewX(), 0);
    QVERIFY(!strip.scrollViewBy(-100000, params));
    QCOMPARE(viewX(), 0);
    QVERIFY(strip.scrollViewBy(100000, params));
    const int maxViewX = viewX();
    QVERIFY(maxViewX > 0);
    QVERIFY(!strip.scrollViewBy(100000, params));
    QCOMPARE(viewX(), maxViewX);

    // The reason the clamp is on the DELTA. Centering the FIRST column parks
    // the view left of the strip's start — an anchor whose derived viewX is
    // out of range, stored deliberately (centerActiveColumn's own comment
    // says so), and during a drag nothing re-clamps it before the first tick
    // arrives. Clamping the absolute position would snap the whole way back
    // into range in one step: hundreds of pixels, and on the leading band in
    // the direction OPPOSITE to the one asked for. Clamping the delta walks
    // it back a tick at a time and never moves it the wrong way.
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.centerActiveColumn(params));
    const int centered = viewX();
    QVERIFY2(centered < 0, qPrintable(QStringLiteral("expected an out-of-range viewX, got %1").arg(centered)));
    QVERIFY(strip.scrollViewBy(1, params));
    QCOMPARE(viewX(), centered + 1); // one step, not a snap to 0
    // ...and it cannot be pushed further out of range.
    QVERIFY(!strip.scrollViewBy(-1, params));
    QCOMPARE(viewX(), centered + 1);
}

QTEST_APPLESS_MAIN(TestScrollStripOps)
#include "test_scrollstrip_ops.moc"
