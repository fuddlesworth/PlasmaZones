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

} // namespace

class TestScrollStripCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void openInsertsColumnAndResizesNothing();
    void openScrollsOnlyWhenNeeded();
    void closeKeepsNeighboursAnchored();
    void closeSelectsSensibleFocus();
    void lastWindowClosedEmptiesStrip();
    void focusNeverModePinsEnteredEdge();
    void focusAlwaysModeCenters();
    void focusOnOverflowMode();
    void alwaysCenterSingleColumn();
    void minimizeKeepsSlotAndRestores();
    void fullyMinimizedColumnCollapses();
    void externalFocusFollowsWindow();
    void stripsAreIndependent();
    void viewAnchorSurvivesLeftInsert();
    void tabIndicatorResolvesOnlyForTabbedColumns();
    void tabIndicatorHidesForASingleTab();
    void tabIndicatorSitsOutsideTheColumnByDefault();
    void tabIndicatorNegativeGapDrawsOverTheWindow();
    void tabIndicatorWithinColumnShrinksTheTiles();
    void tabIndicatorLengthIsCenteredOnTheEdge();
    void tabIndicatorGapKeepsMovingWithinColumn();
};

void TestScrollStripCore::openInsertsColumnAndResizesNothing()
{
    ScrollStrip strip;
    const auto params = defaultParams();

    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    QCOMPARE(strip.columnCount(), 1);
    const QRect aBefore = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(aBefore, QRect(0, 0, 400, 800));

    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.columnCount(), 2);
    QCOMPARE(strip.activeColumnIndex(), 1);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));

    const ResolvedStrip after = strip.relayout(params);
    // The §0 invariant: a's geometry is untouched — same size, same position.
    QCOMPARE(rectOf(after, QStringLiteral("a")), aBefore);
    QCOMPARE(rectOf(after, QStringLiteral("b")).x(), 410);
    QCOMPARE(rectOf(after, QStringLiteral("b")).width(), ScrollStrip::resolveColumnWidthPx(kHalf, params));
}

void TestScrollStripCore::openScrollsOnlyWhenNeeded()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    const int halfPx = ScrollStrip::resolveColumnWidthPx(kHalf, params); // 595

    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // a and b both fit: no scroll happened.
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.viewX, 0);
    QCOMPARE(rectOf(r, QStringLiteral("a")).x(), 0);
    QCOMPARE(rectOf(r, QStringLiteral("b")).x(), halfPx + params.gap);

    // c does not fit — the view scrolls the minimum amount to show it, and
    // NOBODY changes size.
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    r = strip.relayout(params);
    QVERIFY(r.viewX > 0);
    QCOMPARE(rectOf(r, QStringLiteral("c")).width(), halfPx);
    QCOMPARE(rectOf(r, QStringLiteral("a")).width(), halfPx);
    // c's right edge is pinned to the viewport's right edge (minimal scroll).
    QCOMPARE(rectOf(r, QStringLiteral("c")).right() + 1, params.workArea.width());
}

void TestScrollStripCore::closeKeepsNeighboursAnchored()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    const QRect bBefore = rectOf(strip.relayout(params), QStringLiteral("b"));
    // Closing the focused rightmost column: focus falls to b, which must not
    // jump on screen.
    QVERIFY(strip.removeWindow(QStringLiteral("c"), params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("b")), bBefore);
}

void TestScrollStripCore::closeSelectsSensibleFocus()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    // Focus the middle column and close it: the right neighbour takes focus.
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.removeWindow(QStringLiteral("b"), params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));

    QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
    // Closing an unfocused column keeps the focused window focused.
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
}

void TestScrollStripCore::lastWindowClosedEmptiesStrip()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
    QVERIFY(strip.isEmpty());
    QCOMPARE(strip.activeColumnIndex(), -1);
    QCOMPARE(strip.activeWindowId(), QString());
    QVERIFY(strip.relayout(params).columns.isEmpty());
}

void TestScrollStripCore::focusNeverModePinsEnteredEdge()
{
    ScrollStrip strip;
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Never;
    for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}) {
        QVERIFY(strip.insertWindow(id, kHalf, ColumnDisplay::Normal, params));
    }
    // Focused d (rightmost). Focus a: it enters from the left, so its left
    // edge pins to the viewport's left edge.
    QVERIFY(strip.focusFirstColumn(params));
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("a")).x(), 0);

    // Focus b: already fully visible — no scroll at all.
    const int viewBefore = r.viewX;
    QVERIFY(strip.focusAdjacentColumn(+1, params));
    r = strip.relayout(params);
    QCOMPARE(r.viewX, viewBefore);

    // Focus d again: enters from the right, so its right edge pins to the
    // viewport's right edge.
    QVERIFY(strip.focusLastColumn(params));
    r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("d")).right() + 1, params.workArea.width());
}

void TestScrollStripCore::focusAlwaysModeCenters()
{
    ScrollStrip strip;
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
        QVERIFY(strip.insertWindow(id, kHalf, ColumnDisplay::Normal, params));
    }
    QVERIFY(strip.focusColumn(1, params));
    const ResolvedStrip r = strip.relayout(params);
    const QRect b = rectOf(r, QStringLiteral("b"));
    const int centerOffset = (params.workArea.width() - b.width()) / 2;
    QCOMPARE(b.x(), centerOffset);
}

void TestScrollStripCore::focusOnOverflowMode()
{
    ScrollStrip strip;
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::OnOverflow;

    // Two narrow columns that fit together: behaves like Never (no centering).
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.focusColumn(1, params));
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.viewX, 0);
    QCOMPARE(rectOf(r, QStringLiteral("b")).x(), 310);

    // Opening a second wide column centers it: the INSERT's reanchor sees
    // prevIdx = the old column and takes the same OnOverflow branch a
    // focus change would (no explicit focus call happens here).
    ScrollStrip wide;
    QVERIFY(wide.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(700), ColumnDisplay::Normal, params));
    QVERIFY(wide.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(700), ColumnDisplay::Normal, params));
    r = wide.relayout(params);
    const QRect b = rectOf(r, QStringLiteral("b"));
    QCOMPARE(b.x(), (params.workArea.width() - 700) / 2);
}

void TestScrollStripCore::alwaysCenterSingleColumn()
{
    ScrollStrip strip;
    auto params = defaultParams();
    params.alwaysCenterSingleColumn = true;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    const QRect a = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(a.x(), (params.workArea.width() - 400) / 2);

    // A second column ends the lone-column special case.
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.viewX, 0);
}

void TestScrollStripCore::minimizeKeepsSlotAndRestores()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.columnCount(), 1);

    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    QVERIFY(strip.isWindowMinimized(QStringLiteral("b")));
    // b is out of the layout but keeps its slot in the order.
    QVERIFY(rectOf(strip.relayout(params), QStringLiteral("b")).isNull());
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), false, params));
    QCOMPARE(strip.windowsInOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    // The restored window takes the column's active slot back.
    {
        const Column& col = strip.columns().at(0);
        QCOMPARE(col.tiles.at(col.activeTileIdx).windowId, QStringLiteral("b"));
    }
    QVERIFY(!rectOf(strip.relayout(params), QStringLiteral("b")).isNull());
    // Restored in the middle slot: b sits between a and c vertically.
    const ResolvedStrip r = strip.relayout(params);
    QVERIFY(rectOf(r, QStringLiteral("a")).y() < rectOf(r, QStringLiteral("b")).y());
    QVERIFY(rectOf(r, QStringLiteral("b")).y() < rectOf(r, QStringLiteral("c")).y());
}

void TestScrollStripCore::fullyMinimizedColumnCollapses()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));

    const int stripBefore = strip.relayout(params).stripWidth;
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.stripWidth, stripBefore - 300 - params.gap);
    // b's column contributes nothing; c closed up next to a.
    QCOMPARE(rectOf(r, QStringLiteral("c")).x(), rectOf(r, QStringLiteral("a")).x() + 300 + params.gap);
}

void TestScrollStripCore::externalFocusFollowsWindow()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b2"), kHalf, ColumnDisplay::Normal, params));

    QVERIFY(strip.focusWindow(QStringLiteral("a"), params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("a"));
    QVERIFY(strip.focusWindow(QStringLiteral("b2"), params));
    QCOMPARE(strip.activeColumnIndex(), 1);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b2"));
    // Re-focusing the already-focused window reports no change.
    QVERIFY(!strip.focusWindow(QStringLiteral("b2"), params));
    // Unknown window: untouched.
    QVERIFY(!strip.focusWindow(QStringLiteral("nope"), params));
}

void TestScrollStripCore::stripsAreIndependent()
{
    ScrollStrip left;
    ScrollStrip right;
    const auto params = defaultParams();
    QVERIFY(left.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(right.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(left.removeWindow(QStringLiteral("a"), params));
    QCOMPARE(right.windowCount(), 1);
    QCOMPARE(right.activeWindowId(), QStringLiteral("b"));
}

void TestScrollStripCore::viewAnchorSurvivesLeftInsert()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    // Restore-insert a column at index 0 (left of everything): the focused
    // column c must not move on screen.
    const QRect cBefore = rectOf(strip.relayout(params), QStringLiteral("c"));
    QVERIFY(strip.insertWindowAt(0, QStringLiteral("z"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("c")), cBefore);
}

// ── tab indicator ───────────────────────────────────────────────────────────
// The indicator's geometry is resolved by the relayout, not by the overlay, so
// these assert against the ResolvedColumn the strip hands back. The pixel
// numbers all derive from the shared 1200x800 / 10px-gap fixture: a half-width
// column is 595 px wide (1200 halved, minus half the gap), full height 800.

namespace {

/// A one-column tabbed strip holding @p windowCount windows, laid out under
/// @p indicator. Returns the resolved column so a case can assert on both the
/// indicator rect and the tile rects it left behind.
ResolvedColumn tabbedColumn(ScrollStrip& strip, int windowCount, const TabIndicatorParams& indicator)
{
    ScrollLayoutParams params = defaultParams();
    params.tabIndicator = indicator;
    // One column, built directly: the first window opens it tabbed and the
    // rest join it, so the fixture never depends on the default insert
    // position or on a consume loop to collapse stray columns.
    strip.insertWindow(QStringLiteral("w0"), kHalf, ColumnDisplay::Tabbed, params);
    for (int i = 1; i < windowCount; ++i) {
        strip.insertWindowIntoActiveColumn(QStringLiteral("w%1").arg(i), kHalf, ColumnDisplay::Tabbed, params);
    }
    const ResolvedStrip resolved = strip.relayout(params);
    return resolved.columns.isEmpty() ? ResolvedColumn{} : resolved.columns.first();
}

} // namespace

void TestScrollStripCore::tabIndicatorResolvesOnlyForTabbedColumns()
{
    // A NORMAL column never resolves an indicator, however the settings are
    // configured — the flag that matters is the column's display, not the
    // family's enabled bit.
    ScrollStrip strip;
    ScrollLayoutParams params = defaultParams();
    params.tabIndicator.enabled = true;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    const ResolvedStrip resolved = strip.relayout(params);
    QCOMPARE(resolved.columns.size(), 1);
    QVERIFY(!resolved.columns.first().tabbed);
    QVERIFY(resolved.columns.first().tabIndicatorRect.isNull());

    // A tabbed one does.
    ScrollStrip tabbed;
    TabIndicatorParams indicator;
    indicator.enabled = true;
    QVERIFY(!tabbedColumn(tabbed, 2, indicator).tabIndicatorRect.isNull());

    // ...unless the whole family is switched off, which is the single gate the
    // payload emitter reads.
    ScrollStrip off;
    indicator.enabled = false;
    QVERIFY(tabbedColumn(off, 2, indicator).tabIndicatorRect.isNull());
}

void TestScrollStripCore::tabIndicatorHidesForASingleTab()
{
    TabIndicatorParams indicator;
    indicator.enabled = true;
    indicator.hideWhenSingleTab = true;

    ScrollStrip single;
    QVERIFY(tabbedColumn(single, 1, indicator).tabIndicatorRect.isNull());

    ScrollStrip pair;
    QVERIFY(!tabbedColumn(pair, 2, indicator).tabIndicatorRect.isNull());

    // Off, a single-window tabbed column still advertises that it is tabbed.
    indicator.hideWhenSingleTab = false;
    ScrollStrip shown;
    QVERIFY(!tabbedColumn(shown, 1, indicator).tabIndicatorRect.isNull());
}

void TestScrollStripCore::tabIndicatorSitsOutsideTheColumnByDefault()
{
    // Outside means clear of the column by the gap, which is exactly niri's
    // "the indicator draws outside the column, so it can overlay other windows
    // or go off-screen" — including going NEGATIVE off the left edge here.
    TabIndicatorParams indicator;
    indicator.position = TabIndicatorPosition::Left;
    indicator.gap = 5;
    indicator.width = 4;

    ScrollStrip strip;
    const ResolvedColumn column = tabbedColumn(strip, 2, indicator);
    QCOMPARE(column.tabIndicatorRect.width(), 4);
    // Column starts at x=0, so the indicator lands at -(gap + width).
    QCOMPARE(column.tabIndicatorRect.x(), -9);
    // The tiles keep the whole column: nothing was reserved.
    QCOMPARE(column.tiles.first().rect, column.rect);
}

void TestScrollStripCore::tabIndicatorNegativeGapDrawsOverTheWindow()
{
    // A negative gap is meaningful, not a validation escape: it slides the
    // indicator back ONTO the window.
    TabIndicatorParams indicator;
    indicator.position = TabIndicatorPosition::Left;
    indicator.gap = -4;
    indicator.width = 4;

    ScrollStrip strip;
    const ResolvedColumn column = tabbedColumn(strip, 2, indicator);
    // -(gap + width) = -(-4 + 4) = 0, i.e. flush over the column's left edge.
    QCOMPARE(column.tabIndicatorRect.x(), 0);
    QCOMPARE(column.tiles.first().rect, column.rect);
}

void TestScrollStripCore::tabIndicatorWithinColumnShrinksTheTiles()
{
    TabIndicatorParams indicator;
    indicator.position = TabIndicatorPosition::Left;
    indicator.placeWithinColumn = true;
    indicator.gap = 5;
    indicator.width = 4;

    ScrollStrip strip;
    const ResolvedColumn column = tabbedColumn(strip, 2, indicator);
    const int reserved = 9; // width + gap

    // The indicator sits INSIDE, flush against the column's own edge...
    QCOMPARE(column.tabIndicatorRect.x(), column.rect.x());
    // ...and the tiles start after the band it reserved. The column rect
    // itself is deliberately unchanged: it is the full extent, and the
    // reservation shows up on the tiles.
    QCOMPARE(column.tiles.first().rect.x(), column.rect.x() + reserved);
    QCOMPARE(column.tiles.first().rect.width(), column.rect.width() - reserved);

    // Every tab shares the active tile's rect, so the reservation applies to
    // all of them rather than only to the one on screen.
    for (const ResolvedTile& tile : column.tiles) {
        QCOMPARE(tile.rect, column.tiles.first().rect);
    }

    // A top indicator reserves off the HEIGHT instead, on the same terms.
    indicator.position = TabIndicatorPosition::Top;
    ScrollStrip topStrip;
    const ResolvedColumn top = tabbedColumn(topStrip, 2, indicator);
    QCOMPARE(top.tiles.first().rect.y(), top.rect.y() + reserved);
    QCOMPARE(top.tiles.first().rect.height(), top.rect.height() - reserved);
    QCOMPARE(top.tiles.first().rect.width(), top.rect.width());
}

void TestScrollStripCore::tabIndicatorLengthIsCenteredOnTheEdge()
{
    // Shortening the indicator trims BOTH ends evenly rather than anchoring it
    // to a corner, so a short indicator stays visually attached to its column.
    TabIndicatorParams indicator;
    indicator.position = TabIndicatorPosition::Top;
    indicator.lengthProportion = 0.5;

    ScrollStrip strip;
    const ResolvedColumn column = tabbedColumn(strip, 2, indicator);
    const int expectedLength = qRound(column.rect.width() * 0.5);
    QCOMPARE(column.tabIndicatorRect.width(), expectedLength);
    QCOMPARE(column.tabIndicatorRect.x(), column.rect.x() + (column.rect.width() - expectedLength) / 2);

    // A full-length indicator spans the whole edge.
    indicator.lengthProportion = 1.0;
    ScrollStrip full;
    const ResolvedColumn spanning = tabbedColumn(full, 2, indicator);
    QCOMPARE(spanning.tabIndicatorRect.width(), spanning.rect.width());
    QCOMPARE(spanning.tabIndicatorRect.x(), spanning.rect.x());
}

void TestScrollStripCore::tabIndicatorGapKeepsMovingWithinColumn()
{
    // The gap must stay a LIVE control across its whole range in within-column
    // mode. It has two jobs there, and the first one runs out: it shrinks the
    // reservation until that hits zero at gap == -thickness, and past that it
    // has to keep moving the indicator inward over the window. It used to
    // freeze instead, so every press below -thickness did nothing and the
    // setting was indistinguishable from broken.
    TabIndicatorParams indicator;
    indicator.position = TabIndicatorPosition::Top;
    indicator.placeWithinColumn = true;
    indicator.width = 10;

    const auto topOf = [&](int gap) {
        indicator.gap = gap;
        ScrollStrip strip;
        const ResolvedColumn column = tabbedColumn(strip, 2, indicator);
        return column.tabIndicatorRect.y() - column.rect.y();
    };

    // Down to the bottoming-out point the indicator is flush with the column
    // edge and the RESERVATION is what the gap is spending itself on.
    QCOMPARE(topOf(5), 0);
    QCOMPARE(topOf(0), 0);
    QCOMPARE(topOf(-10), 0);

    // Past it every further pixel moves the indicator inward, one for one.
    QCOMPARE(topOf(-11), 1);
    QCOMPARE(topOf(-20), 10);
    QCOMPARE(topOf(-40), 30);

    // The reservation stays floored at zero throughout, so the window keeps
    // the whole column rather than being grown by an over-negative gap.
    indicator.gap = -40;
    QCOMPARE(indicator.reservedThickness(2), 0);

    // OUTSIDE the column the gap was always continuous; pinned here so the
    // two modes cannot drift apart again.
    indicator.placeWithinColumn = false;
    QCOMPARE(topOf(-11), 1);
    QCOMPARE(topOf(-20), 10);
}

QTEST_APPLESS_MAIN(TestScrollStripCore)
#include "test_scrollstrip_core.moc"
