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
    void viewOffsetShiftsStrip();
    void autoTilesShareColumnHeight();
    void fixedHeightTileTakesSpaceFirst();
    void presetHeightResolved();
    void fixedWidthColumn();
    void minimizedTileExcluded();
    void fullyMinimizedColumnCollapses();
    void collapsedFocusedColumnIgnoresViewOffset();
    void visibilityHelper();
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
    state.addColumnForWindow(QStringLiteral("b")); // focus = b (index 1)

    QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    // The focused column sits flush against the inner-left edge; "a" is
    // off-screen to the left.
    QCOMPARE(geometry.value(QStringLiteral("b")), QRectF(10.0, 10.0, 490.0, 780.0));
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(-490.0, 10.0, 490.0, 780.0));

    QVERIFY(state.focusColumn(-1)); // focus "a"
    geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    QCOMPARE(geometry.value(QStringLiteral("a")), QRectF(10.0, 10.0, 490.0, 780.0));
    QCOMPARE(geometry.value(QStringLiteral("b")), QRectF(510.0, 10.0, 490.0, 780.0));
}

void TestScrollLayout::viewOffsetShiftsStrip()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.setViewOffset(-100.0); // viewport begins 100px left of the focused column

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

void TestScrollLayout::collapsedFocusedColumnIgnoresViewOffset()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // [a][b]

    // Minimize "b": its column collapses to zero width. Then focus that
    // collapsed column directly — viewOffset is an offset *within* the
    // focused column, meaningless against a zero-width one, so resolution
    // must anchor on the column's strip position and ignore the offset.
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    QVERIFY(state.focusWindow(QStringLiteral("b")));

    state.setViewOffset(0.0);
    const QHash<QString, QRectF> noOffset = resolveScrollLayout(state, kWorkArea, standardConfig());
    state.setViewOffset(500.0);
    const QHash<QString, QRectF> withOffset = resolveScrollLayout(state, kWorkArea, standardConfig());

    QVERIFY(!withOffset.contains(QStringLiteral("b"))); // collapsed — no geometry
    QVERIFY(withOffset.contains(QStringLiteral("a")));
    // The non-zero viewOffset had no effect: the focused column is collapsed,
    // so "a" lands in exactly the same place as with a zero offset.
    QCOMPARE(withOffset.value(QStringLiteral("a")), noOffset.value(QStringLiteral("a")));
}

void TestScrollLayout::visibilityHelper()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // focus = b; "a" off-screen left

    const QHash<QString, QRectF> geometry = resolveScrollLayout(state, kWorkArea, standardConfig());
    const QStringList visible = scrollVisibleWindows(geometry, kWorkArea);
    QVERIFY(visible.contains(QStringLiteral("b")));
    QVERIFY(!visible.contains(QStringLiteral("a"))); // fully left of the working area
}

QTEST_GUILESS_MAIN(TestScrollLayout)
#include "test_scroll_layout.moc"
