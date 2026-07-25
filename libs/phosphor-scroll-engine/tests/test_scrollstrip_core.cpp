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

    // Closing an unfocused column keeps the focused window focused.
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    QVERIFY(strip.removeWindow(QStringLiteral("a"), params));
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

    // A wide pair that cannot both fit: the newly focused column centers.
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
    QVERIFY(strip.insertWindowAt(0, QStringLiteral("z"), kHalf, ColumnDisplay::Normal));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("c")), cBefore);
}

QTEST_APPLESS_MAIN(TestScrollStripCore)
#include "test_scrollstrip_core.moc"
