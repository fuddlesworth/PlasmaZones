// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

using ScrollTestUtils::defaultParams;
using ScrollTestUtils::kHalf;
using ScrollTestUtils::rectOf;

bool isHidden(const ResolvedStrip& resolved, const QString& windowId)
{
    for (const ResolvedColumn& rc : resolved.columns) {
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                return rt.hidden;
            }
        }
    }
    return false;
}

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
    QVERIFY(ScrollTestUtils::resolveContains(r, QStringLiteral("c")));
    QVERIFY(!isHidden(r, QStringLiteral("c")));
    QVERIFY(isHidden(r, QStringLiteral("a")));
    QVERIFY(isHidden(r, QStringLiteral("b")));
    QCOMPARE(rectOf(r, QStringLiteral("c")).height(), 800);

    // Tab cycling: focus-window-up moves the active tab.
    QVERIFY(strip.focusAdjacentTile(-1));
    r = strip.relayout(params);
    QVERIFY(ScrollTestUtils::resolveContains(r, QStringLiteral("b")));
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
    for (const char* id : {"a", "b", "c", "d", "e"}) {
        if (five.isEmpty()) {
            QVERIFY(five.insertWindow(QString::fromLatin1(id), kHalf, ColumnDisplay::Normal, params));
        } else {
            QVERIFY(five.insertWindowIntoActiveColumn(QString::fromLatin1(id), kHalf, ColumnDisplay::Normal, params));
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
    ScrollStrip strip = threeColumns(params);
    QVERIFY(strip.focusColumn(0, params));

    // Taking an unfocused window (transfer path) leaves focus on a.
    QVERIFY(strip.takeWindow(QStringLiteral("b"), params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("a"));
    QCOMPARE(strip.columnCount(), 2);
    QVERIFY(!strip.containsWindow(QStringLiteral("b")));
    QVERIFY(!strip.takeWindow(QStringLiteral("b"), params));
}

void TestScrollStripOps::onOverflowIgnoresShiftedPrevIdxOnRemoval()
{
    // removeWindowInternal's prevIdx fixup pin: closing a column LEFT of
    // the active one shifts indices, and an unadjusted prevIdx would make
    // the OnOverflow entering-edge test read the RIGHT neighbour's width
    // as prevW. With d wide enough that c+gap+d overflows, the stale index
    // centers c; the adjusted index resolves to the active column itself,
    // the OnOverflow test is skipped, and the view shifts by exactly the
    // removed column's span.
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::OnOverflow;
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.5), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeProportion(0.25), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeProportion(0.25), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("d"), ColumnWidth::makeProportion(0.8), ColumnDisplay::Normal, params));
    // Arrive at c from b (b+gap+c fits, so this focus itself does not
    // center — a centered starting anchor would make the stale-prevIdx
    // outcome indistinguishable from the correct one).
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.focusColumn(2, params));

    const ResolvedStrip before = strip.relayout(params);
    const QRect cBefore = rectOf(before, QStringLiteral("c"));
    const int removedSpan = rectOf(before, QStringLiteral("a")).width() + params.gap;

    QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
    const ResolvedStrip after = strip.relayout(params);
    const QRect cAfter = rectOf(after, QStringLiteral("c"));
    const int centeredX = (params.workArea.width() - cAfter.width()) / 2;
    QCOMPARE(cAfter.x(), cBefore.x() - removedSpan);
    QVERIFY(cAfter.x() != centeredX);
}

QTEST_APPLESS_MAIN(TestScrollStripOps)
#include "test_scrollstrip_ops.moc"
