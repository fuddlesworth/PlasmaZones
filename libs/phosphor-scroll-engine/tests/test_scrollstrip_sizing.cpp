// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The repeatable SIZE verbs' refusal and entry contract: where
// adjustActiveColumnWidth and adjustActiveWindowHeight stop, which states
// they and the two preset cycles decline outright, and which vocabulary entry
// a cycle press lands on when the list is not what the shipped defaults look
// like. Placed in its own file rather than grown into test_scrollstrip_ops,
// which owns the per-operation surface over one shared strip fixture and already
// carries a file-size exception; every slot here builds its own strip against
// the shared screen constants.
//
// These verbs are held down on a key repeat, so a bool that says "changed"
// while nothing moved is the failure mode worth pinning: it costs a relayout
// and a success OSD per press, forever. Each slot therefore asserts the
// resulting GEOMETRY as well as the verdict, because a verdict-only
// assertion passes for a refusal that already mutated the model.

#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

using ScrollTestUtils::defaultParams;
using ScrollTestUtils::kHalf;
using ScrollTestUtils::rectOf;

namespace Ax = ScrollTestUtils::Ax;

} // namespace

class TestScrollStripSizing : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }
    // Declaration order IS definition order, and the run order: the list
    // doubles as the file's table of contents (the ops suite's rule).
    void heightAdjustFloorsAtTheClientMinimum();
    void heightAdjustResizesATabbedColumn();
    void heightPresetCycleResizesATabbedColumn();
    void heightAdjustMeasuresATabbedColumnInColumnSpace();
    void heightAdjustFloorsATabbedColumnAtTheTallestTabsMinimum();
    void switchingTabsDoesNotResizeATabbedColumn();
    void aHeightPressTakesTabbedOwnershipFromTheOtherTab();
    void heightGrowLeavesTheColumnTilingItsBudget();
    void widthPresetCycleWrapsByExtentNotByPosition();
};

// The client half of the height floor, which the engine-minimum slots in the
// ops suite cannot reach: they run with minimum sizes off, where
// columnMinExtentPx answers 0 and tile->minCross drops out of the qMax. Here
// the client minimum is the LARGER of the two floors, so it is the one doing
// the work, and a shrink must stop on it rather than on the fraction.
void TestScrollStripSizing::heightAdjustFloorsAtTheClientMinimum()
{
    ScrollLayoutParams params = defaultParams();
    QVERIFY(params.respectMinimumSize); // the arm under test

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // Well clear of the engine's own floor (a twentieth of the cross extent),
    // so a shrink that stopped on the fraction instead would land far below
    // this and the QCOMPARE would catch it.
    const int clientMin = Ax::crossLen(ScrollTestUtils::defaultScreenRect()) / 4;
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("b"), clientMin, clientMin));
    // "b" is already the active tile: insertWindowIntoActiveColumn focuses
    // what it inserts, and focusWindow answers false for a window that is
    // already focused.
    QCOMPARE(strip.activeColumn()->tiles.at(strip.activeColumn()->activeTileIdx).windowId, QStringLiteral("b"));

    bool everRefused = false;
    for (int i = 0; i < 20; ++i) {
        if (!strip.adjustActiveWindowHeight(-25.0, params)) {
            everRefused = true;
            break;
        }
    }
    QVERIFY2(everRefused, "a repeated shrink must converge onto the floor and then decline");
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), clientMin);
    // Once seated on the floor the verb stays declined rather than alternating.
    QVERIFY(!strip.adjustActiveWindowHeight(-25.0, params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), clientMin);
    // Growing away from the floor still works, so the refusal is the floor
    // and not a wedged verb.
    QVERIFY(strip.adjustActiveWindowHeight(10.0, params));
    QVERIFY(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))) > clientMin);
}

// A tabbed column takes its cross extent from the SHOWN tab's height intent,
// so the adjust verb moves the whole column there (niri parity). Both tabs
// are committed at that one rect, the hidden one included, which is the part
// a verdict-only assertion would miss.
void TestScrollStripSizing::heightAdjustResizesATabbedColumn()
{
    ScrollLayoutParams params = defaultParams();
    const int crossExtent = Ax::crossLen(ScrollTestUtils::defaultScreenRect());

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // Auto while tabbed means the whole work area, which is where a tabbed
    // column used to be pinned for good.
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), crossExtent);

    QVERIFY(strip.adjustActiveWindowHeight(-25.0, params));
    const int shrunk = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));
    QVERIFY2(shrunk < crossExtent, "a shrink press must shorten the tabbed column");
    // The hidden tab rides the same rect, so it cannot be left at the old
    // height for the next tab switch to reveal.
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), shrunk);

    QVERIFY(strip.adjustActiveWindowHeight(25.0, params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), crossExtent);
}

// The preset cycle's half of the same parity. A tabbed column at Auto renders
// at the full work area, which is taller than every entry, so the FORWARD
// press wraps to the vocabulary's smallest and a backward press takes the
// nearest shorter entry — cyclePresetIndexByExtent's rule, reached here
// through the column's own extent rather than a tile's share of it.
void TestScrollStripSizing::heightPresetCycleResizesATabbedColumn()
{
    ScrollLayoutParams params = defaultParams();
    const int crossExtent = Ax::crossLen(ScrollTestUtils::defaultScreenRect());

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    QVERIFY(strip.cycleActiveWindowPresetHeight(-1, params));
    const int tall = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));
    QVERIFY2(tall < crossExtent, "the tallest entry is still shorter than the whole work area");
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), tall);

    QVERIFY(strip.cycleActiveWindowPresetHeight(-1, params));
    const int middle = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));
    QVERIFY2(middle < tall, "a backward press must keep walking down the vocabulary");

    // And back up, so the walk is not one-way.
    QVERIFY(strip.cycleActiveWindowPresetHeight(+1, params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), tall);
}

// The column-space conversion both height verbs make, which the two slots
// above cannot reach: they run with the shipped indicator defaults, where
// placeWithinColumn is off and the reservation is zero, so the column and its
// tabs measure the same. With the indicator placed WITHIN the column and
// eating the CROSS axis the two differ, and a verb that compared in the tab's
// space would enter one reservation off and settle short of the press.
void TestScrollStripSizing::heightAdjustMeasuresATabbedColumnInColumnSpace()
{
    ScrollLayoutParams params = defaultParams();
    params.tabIndicator.placeWithinColumn = true;
    // A Left/Right indicator is thick along x, which is the CROSS axis only
    // while the strip runs vertically; Top/Bottom inverts with it. Picking by
    // axis here is what keeps the transposed run testing the same arm.
    params.tabIndicator.position = params.axis.isHorizontal() ? TabIndicatorPosition::Top : TabIndicatorPosition::Left;
    const int reservation = params.tabIndicator.reservedThickness(2);
    QVERIFY2(reservation > 0, "the fixture must actually reserve, or the slot proves nothing");
    const int crossExtent = Ax::crossLen(ScrollTestUtils::defaultScreenRect());

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // Auto: the column is the whole work area and the tab takes what the
    // indicator left of it.
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), crossExtent - reservation);

    // The delta applies to the COLUMN, so the tab lands exactly one
    // reservation under the shortened column rather than one under a column
    // shortened from the tab's own extent.
    QVERIFY(strip.adjustActiveWindowHeight(-25.0, params));
    const int shrunkColumn = crossExtent - qRound(0.25 * crossExtent);
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), shrunkColumn - reservation);

    // And the matching grow returns to exactly where it started, which is the
    // part an off-by-a-reservation entry rule loses.
    QVERIFY(strip.adjustActiveWindowHeight(25.0, params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), crossExtent - reservation);
}

// The floor a tabbed column shrinks onto is the whole tab SET's, not the
// shown tab's: every tab is committed at the one rect, so relayout clamps the
// column up to the tallest visible minimum. A verb that only knew the shown
// tab's minimum would keep writing under that clamp and keep reporting
// success while the screen stood still — this suite's stated failure mode,
// and it repeats for as long as the key is held.
void TestScrollStripSizing::heightAdjustFloorsATabbedColumnAtTheTallestTabsMinimum()
{
    ScrollLayoutParams params = defaultParams();
    QVERIFY(params.respectMinimumSize); // the arm under test

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());
    // The minimum belongs to the HIDDEN tab. "b" is the shown one and the one
    // the verb writes, so a floor read off the written tile alone misses this
    // entirely.
    const int clientMin = Ax::crossLen(ScrollTestUtils::defaultScreenRect()) / 4;
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("a"), clientMin, clientMin));
    QCOMPARE(strip.activeColumn()->tiles.at(strip.activeColumn()->activeTileIdx).windowId, QStringLiteral("b"));

    bool everRefused = false;
    for (int i = 0; i < 20; ++i) {
        if (!strip.adjustActiveWindowHeight(-25.0, params)) {
            everRefused = true;
            break;
        }
    }
    QVERIFY2(everRefused, "a repeated shrink must converge onto the hidden tab's minimum and then decline");
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), clientMin);
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), clientMin);
    // Seated, not alternating.
    QVERIFY(!strip.adjustActiveWindowHeight(-25.0, params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), clientMin);
}

// A tabbed column's extent comes from its ONE non-Auto tab, never from the
// tab that happens to be focused, so cycling tabs cannot change the column's
// shape. Reading the shown tab instead made a plain tab switch resize the
// column, which also breaks the compositor's tab-switch cross-fade: that
// animation is built on the arriving tab occupying the rect the outgoing one
// just vacated.
void TestScrollStripSizing::switchingTabsDoesNotResizeATabbedColumn()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // Resize while "b" is the shown tab. "a" keeps its Auto height, which is
    // exactly the state that used to make the switch below jump.
    QVERIFY(strip.adjustActiveWindowHeight(-25.0, params));
    const int resized = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));
    QVERIFY(resized < Ax::crossLen(ScrollTestUtils::defaultScreenRect()));

    // Switch to "a" — a focus move and nothing else.
    QVERIFY(strip.focusAdjacentTile(-1));
    QCOMPARE(strip.activeColumn()->tiles.at(strip.activeColumn()->activeTileIdx).windowId, QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), resized);
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), resized);

    // And back, so the invariant is not one-way.
    QVERIFY(strip.focusAdjacentTile(1));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), resized);
}

// The write half of that invariant: a height press claims ownership, clearing
// every other tab back to Auto (niri's convert_heights_to_auto). Without the
// claim two tabs would hold intents at once and the resolver would answer with
// whichever it met first, which is a layout that depends on tile order.
void TestScrollStripSizing::aHeightPressTakesTabbedOwnershipFromTheOtherTab()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // "b" takes the column down first.
    QVERIFY(strip.adjustActiveWindowHeight(-25.0, params));
    const int bHeight = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));

    // Now "a" resizes. It is the EARLIER tile, so before the claim it would
    // have won the resolver's scan on tile order rather than on intent.
    QVERIFY(strip.focusAdjacentTile(-1));
    QVERIFY(strip.adjustActiveWindowHeight(-25.0, params));
    const int aHeight = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a")));
    QVERIFY2(aHeight < bHeight, "the second press must keep shrinking from where the first left the column");

    // One owner, and it is the tab that was pressed.
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height.kind, WindowHeight::Auto);
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height.kind, WindowHeight::Fixed);

    // So switching back to "b" still shows the column "a" set.
    QVERIFY(strip.focusAdjacentTile(1));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), aHeight);
}

// The grow side's budget invariant. Deliberately NOT an exact ceiling
// pixel: what the verb's ceiling OUGHT to be for a multi-tile column is an
// open question (it caps at the work area's cross extent, while relayout can
// only ever hand out that extent less the gaps and the siblings' floors).
// What must hold under any answer is that the column still tiles its budget
// and no sibling is pushed off, so that is what this pins.
void TestScrollStripSizing::heightGrowLeavesTheColumnTilingItsBudget()
{
    ScrollLayoutParams params = defaultParams();
    const int crossExtent = Ax::crossLen(ScrollTestUtils::defaultScreenRect());

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // "b" is already active: the insert focuses what it adds.

    // Far past any plausible ceiling in one press.
    QVERIFY(strip.adjustActiveWindowHeight(500.0, params));

    const ResolvedStrip resolved = strip.relayout(params);
    const int grown = Ax::crossLen(rectOf(resolved, QStringLiteral("b")));
    const int sibling = Ax::crossLen(rectOf(resolved, QStringLiteral("a")));
    // The sibling keeps a positive extent: it was not pushed off the strip.
    QVERIFY2(sibling > 0, "growing one tile must not evict its sibling");
    // And the stack still exactly tiles the cross extent, gap included.
    QCOMPARE(grown + sibling + params.gap, crossExtent);
}

// The preset lists are deduplicated at the boundary but never SORTED, so the
// narrowest entry can sit anywhere in the vocabulary. The cycle is anchored on
// the resolved extent rather than on a stored index, which means its WRAP has
// to be by extent too: wrapping to position 0 in a list typed 1/2, 1/3, 2/3
// hands a forward press the MIDDLE width and leaves 1/3 reachable only
// backwards, so the cycle never comes back round to the narrowest column.
void TestScrollStripSizing::widthPresetCycleWrapsByExtentNotByPosition()
{
    ScrollLayoutParams params = defaultParams();
    // Typed out of size order on purpose: the narrowest entry is at index 1.
    params.presetColumnWidths = {0.5, 1.0 / 3.0, 2.0 / 3.0};

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makePreset(0.5), ColumnDisplay::Normal, params));

    // Literals, not the resolver's own formula: across the gap-aware 1210
    // span, 1/2 is 605 - 10 = 595, 2/3 is 807 - 10 = 797, 1/3 is 403 - 10 = 393.
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 595);

    // Forward takes the NEAREST wider entry, which is 2/3 at index 2.
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 797);

    // Nothing is wider, so the press wraps — to the NARROWEST entry, not to
    // position 0. Position 0 here is 1/2, the answer that stranded 1/3.
    QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 393);

    // And the backward wrap is its mirror: nothing is narrower than 1/3, so
    // the press comes back round to the widest entry.
    QVERIFY(strip.cycleActiveColumnPresetWidth(-1, params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 797);
}

QTEST_APPLESS_MAIN(TestScrollStripSizing)
#include "test_scrollstrip_sizing.moc"
