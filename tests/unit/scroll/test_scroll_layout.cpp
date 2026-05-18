// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorScrollEngine/ScrollLayout.h>

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

/// Working area 1000x800 at the origin, with a 10px outer gap and 10px inner
/// gap — usable area is therefore (10, 10, 980, 780).
ScrollLayoutConfig standardConfig()
{
    ScrollLayoutConfig config;
    config.outerGap = 10.0;
    config.innerGap = 10.0;
    return config;
}

const QRectF kWorkArea(0.0, 0.0, 1000.0, 800.0);

} // namespace

class TestScrollLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyStrip();
    void singleWindowFillsColumn();
    void twoColumnsSideBySide();
    void scrollXShiftsStrip();
    void autoTilesShareColumnHeight();
    void fixedHeightTileTakesSpaceFirst();
    void presetHeightResolved();
    void fixedWidthColumn();
    void minimizedTileExcluded();
    void fullyMinimizedColumnCollapses();
    void soleColumnCollapsesToEmptyLayout();
    void visibilityHelper();
    void viewportFitBringsColumnIntoView();
    void viewportFitLeavesVisibleColumnPut();
    void viewportFitScrollsLeftToColumn();
    void viewportCenteredCentersColumn();
    void viewportCollapsedFocusedColumnAnchorsVisible();
    void viewportCollapsedMidColumnAnchorsForward();
    void viewportAllMinimizedKeepsScroll();
    void viewportWideColumnClampsToColumn();
    void sharedMetricsMatchInternalResolve();
};

void TestScrollLayout::emptyStrip()
{
    ScrollScreenState state(QStringLiteral("S1"));
    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QVERIFY(geometry.isEmpty());
}

void TestScrollLayout::singleWindowFillsColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(static_cast<int>(geometry.size()), 1);
    // Default column width is proportion 0.5 -> 0.5 * 980 = 490, full height 780.
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::twoColumnsSideBySide()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // [a][b]

    // resolveScrollLayout positions the strip purely from scrollX (0 here):
    // columns pack left-to-right from the inner edge, separated by the gap.
    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 780.0));
    QCOMPARE(geometry.value(QStringLiteral("b")), QRectF(510.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::scrollXShiftsStrip()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    // scrollX is the strip-x coordinate mapped to the inner-left edge; a
    // negative value pushes the strip to the right.
    state.setScrollX(-100.0);

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(110.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::autoTilesShareColumnHeight()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b"));

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    // 780 usable height - 10 inner gap = 770, split evenly -> 385 each.
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 385.0));
    QCOMPARE(geometry.value(QStringLiteral("b")), QRectF(10.0, 405.0, 490.0, 385.0));
}

void TestScrollLayout::fixedHeightTileTakesSpaceFirst()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b"));
    QVERIFY(state.focusTile(-1)); // focus "a"
    state.setActiveTileHeight(WindowHeight::fixed(200.0));

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 200.0));
    // "b" (Auto) takes the leftover: 770 - 200 = 570.
    QCOMPARE(geometry.value(QStringLiteral("b")), QRectF(10.0, 220.0, 490.0, 570.0));
}

void TestScrollLayout::presetHeightResolved()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b"));
    QVERIFY(state.focusTile(-1)); // focus "a"
    state.setActiveTileHeight(WindowHeight::preset(0));

    ScrollLayoutConfig config = standardConfig();
    config.presetWindowHeights = {0.25};

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, config);
    // preset 0 = 0.25 * usable height (780) = 195.
    QCOMPARE(geometry.value(QStringLiteral("a")).height(), 195.0);
    QCOMPARE(geometry.value(QStringLiteral("b")).height(), 575.0); // 770 - 195
}

void TestScrollLayout::fixedWidthColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.setActiveColumnWidth(ColumnWidth::fixed(300.0));

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(geometry.value(QStringLiteral("a")).width(), 300.0);
}

void TestScrollLayout::minimizedTileExcluded()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b")); // column [a, b]

    // With "b" minimized, "a" alone fills the column's full height — the
    // minimized tile takes no slot in the vertical layout.
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(static_cast<int>(geometry.size()), 1);
    QVERIFY(!geometry.contains(QStringLiteral("b")));
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::fullyMinimizedColumnCollapses()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // [a][b][c]
    QVERIFY(state.focusWindow(QStringLiteral("a")));

    // Minimizing the whole middle column collapses it out of the strip with
    // no gap: "c" sits flush after "a", exactly where "b" used to be.
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QVERIFY(!geometry.contains(QStringLiteral("b")));
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 780.0));
    QCOMPARE(geometry.value(QStringLiteral("c")), QRectF(510.0, 10.0, 490.0, 780.0));

    // Restoring "b" brings it back in its original slot, between "a" and "c".
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), false));
    const QHash<QString, QRectF> restored = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(restored.value(QStringLiteral("b")), QRectF(510.0, 10.0, 490.0, 780.0));
    QCOMPARE(restored.value(QStringLiteral("c")), QRectF(1010.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::soleColumnCollapsesToEmptyLayout()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));

    // The strip's only window is minimized: its column collapses, leaving no
    // visible tile anywhere. resolveScrollLayout must return an empty hash
    // (not a stale or zero-size rect, not a crash).
    QVERIFY(state.setWindowMinimized(QStringLiteral("a"), true));
    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QVERIFY(geometry.isEmpty());

    // Restoring it brings the window back — default column width 0.5 * 980.
    QVERIFY(state.setWindowMinimized(QStringLiteral("a"), false));
    const QHash<QString, QRectF> restored = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(restored.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::visibilityHelper()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // [a][b]

    // Scroll the strip far enough right that "a" is fully off the left edge of
    // the working area while "b" stays partly visible.
    state.setScrollX(600.0);
    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    const QStringList visible = scrollVisibleWindows(geometry, kWorkArea);
    QVERIFY(visible.contains(QStringLiteral("b")));
    QVERIFY(!visible.contains(QStringLiteral("a"))); // fully left of the working area
}

void TestScrollLayout::viewportFitBringsColumnIntoView()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c"));
    state.addColumnForWindow(QStringLiteral("d"));
    state.addColumnForWindow(QStringLiteral("e")); // addColumnForWindow focuses "e"

    // "e" sits at strip-x 4 * (490 + 10) = 2000, width 490. From scrollX 0 it
    // is off the right edge; fit scrolls right just enough: 2000 + 490 - 980.
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 1510.0);
}

void TestScrollLayout::viewportFitLeavesVisibleColumnPut()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c"));
    QVERIFY(state.focusWindow(QStringLiteral("b"))); // strip-x 500, width 490

    // "b" spans strip-x [500, 990]; a viewport of [100, 1080] fully contains
    // it, so fit must leave the scroll position untouched.
    state.setScrollX(100.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 100.0);
}

void TestScrollLayout::viewportFitScrollsLeftToColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c"));
    QVERIFY(state.focusWindow(QStringLiteral("a"))); // strip-x 0

    // Viewport scrolled right past "a"; fit scrolls left exactly to its edge.
    state.setScrollX(300.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 0.0);

    // A mid-strip column off the left edge scrolls left to its non-zero
    // strip-x: "b" sits at strip-x 500.
    QVERIFY(state.focusWindow(QStringLiteral("b")));
    state.setScrollX(900.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 500.0);
}

void TestScrollLayout::viewportCenteredCentersColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // focus "c": strip-x 1000, width 490

    ScrollLayoutConfig config = standardConfig();
    config.viewportMode = ScrollViewportMode::Centered;
    // Centered: colLeft + (colWidth - usableW) / 2 = 1000 + (490 - 980) / 2.
    QCOMPARE(computeViewportScroll(state, kWorkArea, config), 755.0);
}

void TestScrollLayout::viewportCollapsedFocusedColumnAnchorsVisible()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // [a][b]

    // Minimize "b" so its column collapses, then focus that collapsed column.
    // computeViewportScroll anchors on the nearest visible column ("a"), not
    // the zero-width collapsed slot.
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    QVERIFY(state.focusWindow(QStringLiteral("b")));
    // "a" (strip-x 0, width 490) is already fully visible from scrollX 0.
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 0.0);

    // Centered anchors on "a" too: 0 + (490 - 980) / 2 = -245.
    ScrollLayoutConfig centered = standardConfig();
    centered.viewportMode = ScrollViewportMode::Centered;
    QCOMPARE(computeViewportScroll(state, kWorkArea, centered), -245.0);
}

void TestScrollLayout::viewportCollapsedMidColumnAnchorsForward()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // [a][b][c]

    // Collapse the middle column and focus it. viewportAnchorColumn searches
    // forward first, so the viewport anchors on "c" (strip-x 500 — the
    // collapsed column shares it), not the visible column to the left. "c"
    // spans [500, 990]; fit scrolls right to 990 - 980 = 10. (A backward
    // anchor on the already-visible "a" would have left scrollX at 0.)
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    QVERIFY(state.focusWindow(QStringLiteral("b")));
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 10.0);
}

void TestScrollLayout::viewportAllMinimizedKeepsScroll()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    QVERIFY(state.setWindowMinimized(QStringLiteral("a"), true));
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));

    // Every column collapsed: nothing anchors the viewport, so
    // computeViewportScroll leaves the stored scroll position untouched.
    state.setScrollX(700.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 700.0);
}

void TestScrollLayout::viewportWideColumnClampsToColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // focus "b"
    // Make the focused column wider than the working area; fit cannot fit it.
    // "b" spans strip-x [500, 1700]; usableW is 980, so the viewport stays
    // entirely within the column for scrollX in [500, 720].
    state.setActiveColumnWidth(ColumnWidth::fixed(1200.0));

    // Dead space on the left (scrollX < colLeft): scroll right to the column's
    // left edge — the minimum move.
    state.setScrollX(50.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 500.0);

    // Already edge-to-edge inside the column: fit leaves the scroll untouched.
    state.setScrollX(600.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 600.0);

    // Dead space on the right (scrollX past the column's far extent): scroll
    // left just enough to keep the viewport filled — 1700 - 980 = 720.
    state.setScrollX(900.0);
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig()), 720.0);
}

void TestScrollLayout::sharedMetricsMatchInternalResolve()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c"));
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true)); // a collapsed column
    QVERIFY(state.focusWindow(QStringLiteral("c")));
    state.setScrollX(300.0);

    // resolveColumnMetrics resolves widths + strip-x: "a" and "c" are 490 wide,
    // the collapsed "b" is zero-width and shares "c"'s strip-x (500).
    const ScrollColumnMetrics metrics = resolveColumnMetrics(state, kWorkArea, standardConfig());
    QCOMPARE(metrics.widths, (QVector<qreal>{490.0, 0.0, 490.0}));
    QCOMPARE(metrics.stripX, (QVector<qreal>{0.0, 500.0, 500.0}));

    // Passing pre-resolved metrics must yield byte-identical results to the
    // internal (nullptr) resolve — the shared-metrics fast path is pure reuse.
    QCOMPARE(computeViewportScroll(state, kWorkArea, standardConfig(), &metrics),
             computeViewportScroll(state, kWorkArea, standardConfig()));
    QCOMPARE(resolveScrollLayout(state, kWorkArea, standardConfig(), &metrics),
             resolveScrollLayout(state, kWorkArea, standardConfig()));
}

QTEST_GUILESS_MAIN(TestScrollLayout)
#include "test_scroll_layout.moc"
