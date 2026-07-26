// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

ScrollLayoutParams defaultParams()
{
    ScrollLayoutParams p;
    p.workArea = QRect(0, 0, 1200, 800);
    p.gap = 10;
    return p;
}

const ColumnWidth kHalf = ColumnWidth::makeProportion(0.5);

QRect rectOf(const ResolvedStrip& resolved, const QString& windowId)
{
    for (const ResolvedColumn& rc : resolved.columns) {
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                return rt.rect;
            }
        }
    }
    return {};
}

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

/// a | b | c as three single-tile columns, focus on c.
ScrollStrip threeColumns(const ScrollLayoutParams& params)
{
    ScrollStrip strip;
    strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params);
    strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params);
    strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params);
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
    void takeWindowLeavesFocusPolicyAlone();
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
    QCOMPARE(presetPx, qRound(1.0 / 3.0 * (800 + 10)) - 10);
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
    // Only the active tile (c) shows, at full column height.
    QVERIFY(!isHidden(r, QStringLiteral("c")));
    QVERIFY(isHidden(r, QStringLiteral("a")));
    QVERIFY(isHidden(r, QStringLiteral("b")));
    QCOMPARE(rectOf(r, QStringLiteral("c")).height(), 800);

    // Tab cycling: focus-window-up moves the active tab.
    QVERIFY(strip.focusAdjacentTile(-1));
    r = strip.relayout(params);
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

QTEST_APPLESS_MAIN(TestScrollStripOps)
#include "test_scrollstrip_ops.moc"
