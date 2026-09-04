// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The repeatable SIZE verbs' refusal and entry contract: where
// adjustActiveWindowHeight stops, which states it and the two preset cycles
// decline outright, and which vocabulary entry a cycle press lands on when
// the list is not what the shipped defaults look like. adjustActiveColumnWidth
// is named here only for its clamp (the widen that lands exactly on the work
// area); its own refusal contract lives with the width verbs in
// test_scrollstrip_ops.cpp. Placed in its own file rather than grown into test_scrollstrip_ops,
// which owns the per-operation surface over one shared strip fixture and already
// carries a file-size exception; every slot here builds its own strip against
// the shared screen constants.
//
// These verbs are held down on a key repeat, so a bool that says "changed"
// while nothing moved is the failure mode worth pinning: it costs a relayout
// and a success OSD per press, forever. Each slot therefore asserts the
// resulting GEOMETRY as well as the verdict, because a verdict-only
// assertion passes for a refusal that already mutated the model.
//
// The file's second concern is the centre-short-columns policy, which shares
// this fixture rather than earning its own: it is a cross-axis ORIGIN applied
// to the very heights these verbs resolve, so it is pinned against the same
// resolved stacks. Its block starts at aShortColumnHugsTheStartEdgeByDefault.

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
    void heightPresetCycleMeasuresATabbedColumnInColumnSpace();
    void maximizeToggleRefusesAColumnPinnedByItsMinimum();
    void heightAdjustMeasuresATabbedColumnInColumnSpace();
    void heightAdjustFloorsATabbedColumnAtTheTallestTabsMinimum();
    void switchingTabsDoesNotResizeATabbedColumn();
    void aHeightPressTakesTabbedOwnershipFromTheOtherTab();
    void aZeroMovementHeightPressStillReportsTheOwnershipClaim();
    void aMinimizePressNeverHandsATabbedColumnToAnAutoOwner();
    void growIntoEmptySpaceRefusesAFullTabbedColumnRatherThanUnMaximizingIt();
    void tabbingAStackPicksTheShownTabAndUntabbingRestoresEveryHeight();
    void closingTheOwningTabHandsTheExtentToTheTabOnShow();
    void heightGrowLeavesTheColumnTilingItsBudget();
    void bothPresetCyclesRefuseADeltaThatIsNotOneStep();
    void widthPresetCycleWrapsByExtentNotByPosition();
    void maximizeToggleEntersOnRenderedWidthNotIntentKind();
    void maximizeUnmaximizeSkipsAStaleFullWidthRestoreSlot();
    void aShortColumnHugsTheStartEdgeByDefault();
    void centeringPutsAShortColumnsSlackOnBothSides();
    void centeringLeavesAColumnThatFillsTheCrossExtentAlone();
    void centeringMeasuresTheWholeStackIncludingItsGaps();
    void centeringMovesAShortTabbedColumnAndItsTabsTogether();
    void centeringMeasuresFromTheWorkAreasCrossOriginNotTheScreens();
    void centeringMovesOnlyTheShortColumnAndNotItsFullHeightNeighbour();
    void centeringYieldsNoOffsetWhenTheStackOverflowsTheCrossExtent();
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
// at the full work area, which is taller than every entry, so a backward press
// takes the nearest shorter entry — cyclePresetIndexByExtent's rule, reached
// here through the column's own extent rather than a tile's share of it. This
// slot drives the backward walk and the step back up; the forward wrap from
// Auto is the width analogue's subject (widthPresetCycleWrapsByExtentNotBy
// Position) and is deliberately not restated here.
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
void TestScrollStripSizing::heightPresetCycleMeasuresATabbedColumnInColumnSpace()
{
    // The CYCLE verb's half of the reservation correction, which its adjust
    // twin above has always covered and it has not. The two share one entry
    // rule — enter in COLUMN space, so the tab lands one reservation under the
    // column rather than the column being shortened from the tab's own extent
    // — and deleting the correction from the cycle alone left the suite green,
    // because the only tabbed cycle test runs on defaultParams where the
    // reservation is zero.
    ScrollLayoutParams params = defaultParams();
    params.tabIndicator.placeWithinColumn = true;
    // Picked by axis for the reason the twin states: a Left/Right indicator is
    // thick along the CROSS axis only while the strip runs vertically, so a
    // hardcoded position leaves one of the two registered arms reserving
    // nothing and testing nothing.
    params.tabIndicator.position = params.axis.isHorizontal() ? TabIndicatorPosition::Top : TabIndicatorPosition::Left;
    const int reservation = params.tabIndicator.reservedThickness(2);
    QVERIFY2(reservation > 0, "the fixture must actually reserve, or the slot proves nothing");
    const int crossExtent = Ax::crossLen(ScrollTestUtils::defaultScreenRect());
    params.presetWindowHeights = {0.5, 1.0};

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // The RELATIONSHIP, read from the strip, not a predicted absolute: what
    // the entry rule decides is whether the tab sits exactly one reservation
    // under its own column, and asserting a computed pixel figure would also
    // pin what the preset fraction is a fraction OF, which is a separate
    // contract this slot has no business restating.
    const auto tabUnderItsColumn = [&](const char* whenLabel) {
        const auto resolved = strip.relayout(params);
        const int tabCross = Ax::crossLen(rectOf(resolved, QStringLiteral("b")));
        const int columnCross = Ax::crossLen(resolved.columns.at(0).rect);
        QVERIFY2(columnCross > reservation, whenLabel);
        QCOMPARE(tabCross, columnCross - reservation);
    };
    tabUnderItsColumn("the starting column must be taller than the reservation");

    // And it still holds after the cycle moves the column, which is the press
    // whose entry rule was uncovered.
    QVERIFY(strip.cycleActiveWindowPresetHeight(1, params));
    tabUnderItsColumn("the cycled column must be taller than the reservation");
    QVERIFY2(Ax::crossLen(strip.relayout(params).columns.at(0).rect) != crossExtent,
             "the cycle must have moved the column, or the entry rule is untested");
}

void TestScrollStripSizing::maximizeToggleRefusesAColumnPinnedByItsMinimum()
{
    // The verb-side twin of the published flag's min-pinned exclusion, and the
    // arm this suite exists for: a bool that says "changed" while nothing moved
    // costs a relayout and a success OSD per press, forever.
    //
    // A column whose tiles' declared minimum alone reaches the work area
    // renders full width whatever the intent says, so both toggle arms are
    // dead for it — maximizing changes nothing visible, and un-maximizing
    // cannot shrink it past its own floor. The verb must refuse rather than
    // report success.
    ScrollLayoutParams params = defaultParams();
    const int mainExtent = Ax::mainLen(ScrollTestUtils::defaultScreenRect());

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    // BY ROLE: a symmetric minimum would set the cross floor past the cross
    // extent, which is a different refusal entirely.
    const QSize pinned = Ax::t(QSize(mainExtent, 0));
    // Verdict wrapped like the ~25 siblings in this file: this was the only
    // bare call, and a setter that started refusing would leave the column
    // unpinned, turning the refusal assertion below into a test of nothing.
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("a"), pinned.width(), pinned.height()));

    const int beforeMain = Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a")));
    QCOMPARE(beforeMain, mainExtent);
    QVERIFY2(!strip.toggleMaximizeActiveColumn(params),
             "a column pinned full by its minimum must refuse, not report a change");
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), beforeMain);
}

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
    // The RELATIONSHIP, asserted alongside the computed figure above: the tab
    // sits exactly one reservation under the column it belongs to. The figure
    // restates the verb's own percentage-of-cross formula, so a shared error in
    // that formula would reproduce in the expectation and pass; this line is
    // independent of it and pins what the column-space conversion is actually
    // for.
    {
        const ResolvedStrip resolved = strip.relayout(params);
        QCOMPARE(Ax::crossLen(rectOf(resolved, QStringLiteral("b"))),
                 Ax::crossLen(resolved.columns.first().rect) - reservation);
    }

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

// The write half of that invariant: a height press takes ownership of the
// column's extent. Ownership is an explicit pointer (Column::heightOwnerId),
// so the tab that loses it KEEPS its own height — the column is decided by one
// tab without the others' intents being destroyed to arrange it. Without the
// claim the resolver would fall back to whichever non-Auto tab it met first,
// which is a layout that depends on tile order.
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
    //
    // The pointer is read across several relayout() calls below, which is safe
    // because relayout() is const and cannot reallocate m_columns. Any
    // MUTATING call in between would invalidate it, so re-fetch rather than
    // extending this pointer's life past one — the sibling slot below does
    // exactly that after its ownership press.
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("a"));
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height.kind, WindowHeight::Fixed);
    // "b" is no longer the owner, but its own intent is untouched — losing the
    // claim is not losing the height. This is what makes the tab toggle
    // reversible: untabbing hands "b" back the height it had.
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height.kind, WindowHeight::Fixed);
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), aHeight);

    // So switching back to "b" still shows the column "a" set.
    QVERIFY(strip.focusAdjacentTile(1));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), aHeight);
}

// The one arm of these verbs that returns TRUE without writing a height, and
// the inverse of the failure shape this file's header says it exists to pin.
//
// adjustActiveWindowHeight claims tabbed ownership BEFORE its no-move bail and
// then returns whether the claim happened, so a press whose own target lands
// exactly on the current pixel is still a real state change. It is emphatically
// NOT a no-op: a tabbed column resolves its extent from the OWNER's intent, so
// moving the owner re-resolves the column even though this press wrote no
// height of its own. Here the claiming tab still carries Auto, which resolves
// to the whole work area, and the column jumps from the outgoing owner's 600
// to 800.
//
// That is exactly why the bail returns `claimed` rather than false. The verdict
// is what drives the caller's relayout; a false here would leave the column
// painted at the old owner's extent while the model had already moved on.
//
// Untested until now, which matters because the bail reads like a redundant
// `target != currentPx` waiting to be tidied up. That edit looks obviously
// right and breaks the handover silently.
void TestScrollStripSizing::aZeroMovementHeightPressStillReportsTheOwnershipClaim()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // "b" (the shown tab) takes ownership with a press that DOES move.
    QVERIFY(strip.adjustActiveWindowHeight(-25.0, params));
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("b"));
    const int settled = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));

    // Now "a" presses by an amount that rounds to zero pixels. deltaPercent is
    // a percentage of the work-area cross extent, so this is well under half a
    // pixel and target lands exactly on currentPx.
    QVERIFY(strip.focusAdjacentTile(-1));
    // Strictly UNDER half a pixel: the verb qRounds the pixel delta, and
    // qRound rounds a half away from zero, so exactly 0.5 would move one pixel
    // and stop testing the zero-movement arm.
    const qreal subPixel = 0.2 / Ax::crossLen(ScrollTestUtils::defaultScreenRect()) * 100.0;
    QVERIFY2(strip.adjustActiveWindowHeight(-subPixel, params),
             "a press that moves nothing but takes the extent owner is still a change");

    // The claim landed...
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("a"));
    // ...and "a" wrote NO height of its own: it still carries the Auto it was
    // inserted with. This is the arm's signature — the true verdict came from
    // the claim, not from a resize.
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height.kind, WindowHeight::Auto);
    // Which is why the verdict has to be true. The column now resolves from
    // "a"'s Auto (the whole work area) instead of the 600 "b" had set, so a
    // caller told "nothing happened" would skip the relayout and leave the
    // column painted at a value the model no longer holds.
    const int afterClaim = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a")));
    QVERIFY2(afterClaim != settled, "moving the extent owner must re-resolve the column");
    QCOMPARE(afterClaim, Ax::crossLen(ScrollTestUtils::defaultScreenRect()));

    // Pressing again as the SITTING owner, still by a sub-pixel amount, has
    // nothing left to claim and nothing to move: now it must report false.
    QVERIFY2(!strip.adjustActiveWindowHeight(-subPixel, params),
             "with the claim already held and no movement there is nothing to report");
}

// The claim's other half, and the reason minimize cannot simply copy the slot
// above. claimTabbedHeightOwnership moves the extent owner and writes NOTHING
// else, so whoever claims must already hold the height the claim was made for.
// The zero-movement ADJUST press above is allowed to leave the new owner on
// Auto because its target IS the current extent, so there is no other value to
// write. Minimize always has one — the smallest preset — and when the tile is
// already sitting there the press moves no pixels and once bailed out before
// writing it. That left the active tab owning the column while still carrying
// the Auto it was inserted with, and tabbedColumnCrossPx resolves an Auto
// owner to the WHOLE work area: a press asking to shrink grew the column to
// full height instead.
void TestScrollStripSizing::aMinimizePressNeverHandsATabbedColumnToAnAutoOwner()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    // "a" is given exactly the extent the smallest preset resolves to, so the
    // minimize press below has nothing to move and takes the no-movement arm.
    // 1/3 of the gap-aware span, less the gap, is 260 (the ops suite pins the
    // same literal).
    QVERIFY(strip.focusAdjacentTile(-1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(260)));
    QVERIFY(strip.toggleActiveColumnTabbed());

    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 260);

    // Show "b", which is still the Auto it was inserted with. It renders at
    // the column "a" sized, which is the premise: the press that follows finds
    // the pixels already where it wants them.
    QVERIFY(strip.focusAdjacentTile(1));
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height.kind, WindowHeight::Auto);
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 260);

    // True because the claim lands, even though no pixel moves: "b" takes the
    // extent owner from "a". Asserted so the slot cannot pass against a verb
    // that does nothing at all.
    QVERIFY2(strip.minimizeActiveWindowHeight(params), "taking the extent owner is a change and must be reported");

    // A minimize press may never GROW the column. This is the assertion that
    // fails on the unfixed engine, at 800 against 260.
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 260);
    // And the owner and the intent agree: whichever tab ends up owning the
    // extent is holding a height that resolves to it, never a bare Auto.
    const WindowHeight ownerHeight = col->tiles.at(col->indexOfWindow(col->heightOwnerId)).height;
    QVERIFY2(ownerHeight.kind != WindowHeight::Auto,
             "the tab owning a tabbed column's extent must not be left on Auto by a minimize press");
}

// "Grow into empty space" on a TABBED column that is already full must refuse,
// not un-maximize. The verb routes a tabbed column through the maximize toggle
// because a tab has no leftover WITHIN the column — "fill what is left" means
// the whole budget there — but the toggle has two arms, and reaching it while
// the column is already at the budget lands on the wrong one. The width twin
// takes its is_full_width early-out ahead of BOTH its toggle routes for
// exactly this reason; the height verb once took the tabbed route first, so a
// grow press on a full tab answered a cheerful true and threw the Fixed intent
// away for an Auto. The pixels happen not to move (an Auto owner also resolves
// to the whole extent), which is precisely why this asserts the INTENT.
void TestScrollStripSizing::growIntoEmptySpaceRefusesAFullTabbedColumnRatherThanUnMaximizingIt()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    // Short the column first, so the grow press below has something real to
    // claim and the test is not starting from the answer.
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(400)));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 400);

    // The grow press fills the column, through the toggle's maximize arm.
    QVERIFY(strip.expandActiveWindowToAvailableHeight(params));
    const int full = Ax::crossLen(ScrollTestUtils::defaultScreenRect());
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), full);
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height.kind, WindowHeight::Fixed);

    // The second press has nothing left to grow into and must say so.
    QVERIFY2(!strip.expandActiveWindowToAvailableHeight(params),
             "a grow press on a column that already fills its budget must refuse");
    col = strip.activeColumn();
    QVERIFY(col);
    QVERIFY2(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height.kind == WindowHeight::Fixed,
             "a refused grow press must not un-maximize the tab it declined to grow");
}

// The tab toggle is REVERSIBLE, which is the whole reason ownership is a
// pointer rather than a wipe. A Normal column legitimately holds several sized
// tiles — nothing arbitrates a stack, so the height verbs cannot maintain a
// single owner there — and the flip into tabbed has to choose one of them
// WITHOUT destroying the rest, or untabbing returns a stack the user never
// built. It also has to choose the tab on show: picking by stack order hands
// the column to whichever tile sits first, regardless of what was in front of
// the user when they pressed the key.
void TestScrollStripSizing::tabbingAStackPicksTheShownTabAndUntabbingRestoresEveryHeight()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    // Two sized tiles in a NORMAL column, which is ordinary: each governs its
    // own slice of the stack, so both intents are legitimate at once.
    QVERIFY(strip.focusAdjacentTile(-1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(200)));
    QVERIFY(strip.focusAdjacentTile(1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));

    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height, WindowHeight::makeFixed(200));
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height, WindowHeight::makeFixed(300));

    // "b" is the tab on show when the flip lands, so "b" owns the extent —
    // NOT "a", which is the tile stack order would have handed it to.
    QVERIFY(strip.toggleActiveColumnTabbed());
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 300);

    // Nothing was destroyed to arrange that: "a" still carries its own height
    // while "b" owns the column.
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height, WindowHeight::makeFixed(200));

    // So untabbing gives the stack back exactly as it was, and the ownership
    // is dropped rather than left to go stale.
    QVERIFY(strip.toggleActiveColumnTabbed());
    col = strip.activeColumn();
    QVERIFY(col);
    QVERIFY(col->heightOwnerId.isEmpty());
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height, WindowHeight::makeFixed(200));
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).height, WindowHeight::makeFixed(300));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 200);
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 300);

    // An owner named at the TRANSITION may legitimately be Auto, and then the
    // column takes the whole work area rather than scanning past it to a sized
    // sibling — the tab on show asked for full height, so that is the answer.
    QVERIFY(strip.focusAdjacentTile(-1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeAuto()));
    QVERIFY(strip.toggleActiveColumnTabbed());
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), ScrollTestUtils::kCrossExtent);

    // But WRITING Auto is not a bid for the column. "b" takes it with a real
    // height, and a later Auto write on "a" must not seize it back and blow
    // the column up to full height. This is the rule that stops every
    // arrival — a cross-output move, an unfloat, a drag cancel, a stash
    // restore, each of which re-states a tile's intent — from resizing a
    // tabbed column it merely joined.
    QVERIFY(strip.focusAdjacentTile(1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(250)));
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("b"));
    QVERIFY(strip.focusAdjacentTile(-1));
    // The verdict is asserted rather than discarded, and it is FALSE: the tile
    // already holds Auto, so the write moves nothing and the verb correctly
    // refuses. Spelling that out is the point — a bare discarded call reads as
    // an oversight in a suite whose whole subject is which presses report a
    // change, and a future reader would not know whether the refusal was
    // expected or unnoticed.
    QVERIFY(!strip.setActiveWindowHeight(WindowHeight::makeAuto()));
    // Re-fetched: focusAdjacentTile and the height write above are both
    // mutating calls, and this file's rule is that a Column* does not survive
    // one. The refusal means nothing moved THIS time, which is not the same as
    // the pointer being guaranteed good.
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 250);
}

// The owner is a window id, so a tab leaving the column would otherwise leave
// it dangling. The resolver would still answer (it falls back to a scan), but
// by STACK ORDER — so closing a neighbour could resize the column to a tab the
// user is not looking at. The ownership follows the tab on show instead.
void TestScrollStripSizing::closingTheOwningTabHandsTheExtentToTheTabOnShow()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    // "a" is sized and sits FIRST, so it is what a stack-order fallback would
    // pick. "c" is sized too and is the tab on show.
    QVERIFY(strip.focusAdjacentTile(-1)); // c -> b
    QVERIFY(strip.focusAdjacentTile(-1)); // b -> a
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(200)));
    QVERIFY(strip.focusAdjacentTile(1)); // a -> b
    QVERIFY(strip.focusAdjacentTile(1)); // b -> c
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));

    QVERIFY(strip.toggleActiveColumnTabbed());
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("c"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("c"))), 300);

    // Close the owner. "b" is the tab that comes on show, and it is Auto, so
    // the column takes the whole work area — NOT "a"'s 200, which is what the
    // stack-order fallback would have answered.
    QVERIFY(strip.removeWindow(QStringLiteral("c"), params));
    col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->heightOwnerId, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), ScrollTestUtils::kCrossExtent);
    // "a" kept its height throughout, so untabbing still restores the stack.
    QCOMPARE(col->tiles.at(col->indexOfWindow(QStringLiteral("a"))).height, WindowHeight::makeFixed(200));
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
// SURVIVING MUTATION until now: dropping `(delta != -1 && delta != 1)` from
// either preset cycle failed nothing in the repo. Both verbs are wired to a
// one-step-per-press shortcut, so every existing fixture passes ±1 and the
// guard was never exercised.
//
// It is not decoration. Both cycles hand delta to cyclePresetIndexByExtent,
// whose contract is a SINGLE step in a direction; a 0 asks for a step with no
// direction, and anything larger asks it to skip entries the extent search is
// not written to skip. The refusal is also the only thing standing between a
// D-Bus caller and that state, since both verbs are reachable from the wire.
//
// Zero is the boundary this slot cares about most (it is what an
// uninitialised or mis-parsed delta looks like) and is one of the three
// boundary values no fixture in this file previously carried.
void TestScrollStripSizing::bothPresetCyclesRefuseADeltaThatIsNotOneStep()
{
    ScrollLayoutParams params = defaultParams();
    params.presetColumnWidths = {0.5, 1.0 / 3.0, 2.0 / 3.0};
    params.presetWindowHeights = {0.5, 1.0};

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makePreset(0.5), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    const int widthBefore = Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a")));
    const int heightBefore = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));

    // Verdict AND geometry on every arm: a verdict-only assertion passes for a
    // refusal that already mutated the model, which is this file's own rule.
    for (const int delta : {0, 2, -2, 7}) {
        QVERIFY2(!strip.cycleActiveColumnPresetWidth(delta, params), "a width cycle must step by exactly one");
        QVERIFY2(!strip.cycleActiveWindowPresetHeight(delta, params), "a height cycle must step by exactly one");
        QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), widthBefore);
        QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), heightBefore);
    }

    // Positive control: ±1 still works, so the refusals above are attributable
    // to the delta and not to a fixture that could never cycle at all.
    QVERIFY(strip.cycleActiveColumnPresetWidth(1, params));
    QVERIFY(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))) != widthBefore);
    QVERIFY(strip.cycleActiveWindowPresetHeight(1, params));
    // Geometry on the height arm too, not just its verdict — the same rule the
    // refusal loop above follows, and the reason it exists: a verdict-only
    // check here would pass for a cycle that reported a change and moved
    // nothing, which is precisely the failure this slot is about.
    QVERIFY(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))) != heightBefore);
}

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

void TestScrollStripSizing::maximizeToggleEntersOnRenderedWidthNotIntentKind()
{
    // The toggle decides "is this column already maximized" in RESOLVED
    // PIXELS, not on the ColumnWidth value. operator== compares kind first,
    // so Fixed(<work area main>) is not == Proportion(1.0) even though the two
    // render identically — and a Fixed full-width column is ordinary, not
    // exotic: adjustActiveColumnWidth clamps to exactly that when a widen
    // press hits the work area, and so does an interactive edge-drag's
    // reconcile.
    //
    // On the old kind compare such a column took the MAXIMIZE arm: it stored
    // the current width in the slot, wrote Proportion(1.0), rendered exactly
    // the same pixels, and reported success. The user's press did nothing
    // visible and the titlebar button snapped straight back.
    const auto params = defaultParams();
    const int workMain = Ax::mainLen(params.workArea);
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(workMain), ColumnDisplay::Normal, params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), workMain);

    // The press must UN-maximize, so the column has to get narrower.
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QVERIFY2(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))) < workMain,
             "a Fixed full-width column must un-maximize, not silently re-store itself");

    // And the press back returns it to full width, so the verb is a genuine
    // toggle from this entry rather than a one-way trip.
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), workMain);
}

void TestScrollStripSizing::maximizeUnmaximizeSkipsAStaleFullWidthRestoreSlot()
{
    // The stored pre-maximize intent is re-validated against the CURRENT work
    // area rather than trusted. Nothing invalidates the slot when the output
    // resizes, so a Fixed width captured on a WIDER work area resolves
    // clamped back to full width on a narrower one — restoring it would spend
    // the slot, move nothing, and report success.
    ScrollLayoutParams params = defaultParams();
    const int wideMain = Ax::mainLen(params.workArea);
    ScrollStrip strip;
    QVERIFY(
        strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(wideMain - 100), ColumnDisplay::Normal, params));
    // Maximize on the wide area: the slot now holds Fixed(wideMain - 100).
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))), wideMain);

    // The output shrinks below the stored width, so that intent now resolves
    // to the full (narrower) work area.
    const int narrowMain = wideMain - 400;
    const int cross = Ax::crossLen(params.workArea);
    params.workArea = Ax::vertical() ? QRect(0, 0, cross, narrowMain) : QRect(0, 0, narrowMain, cross);

    // The un-maximize press must still leave the column NARROWER than the new
    // work area, taking the default-width arm rather than consuming the slot
    // on a value that renders full width.
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QVERIFY2(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a"))) < narrowMain,
             "restoring a stale full-width slot must fall through to the default width");
}

// The centre-short-columns policy, whose whole surface is the cross-axis
// ORIGIN of a column that resolved shorter than the work area. The four slots
// below pin both verdicts (off is the historical top-hugging layout, on is
// the centred one), the case that must be identical under either (a column
// that fills), and that the measurement is the stack rather than one tile.
// Each asserts the cross EXTENT alongside the position: a policy that
// accidentally resized instead of moving would satisfy a position-only
// assertion.
void TestScrollStripSizing::aShortColumnHugsTheStartEdgeByDefault()
{
    ScrollLayoutParams params = defaultParams();
    QVERIFY(!params.centerShortColumns);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));

    const QRect r = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(r), 300);
    QCOMPARE(Ax::crossPos(r), 0);
}

void TestScrollStripSizing::centeringPutsAShortColumnsSlackOnBothSides()
{
    ScrollLayoutParams params = defaultParams();
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));

    const QRect r = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(r), 300);
    QCOMPARE(Ax::crossPos(r), (ScrollTestUtils::kCrossExtent - 300) / 2);
}

void TestScrollStripSizing::centeringLeavesAColumnThatFillsTheCrossExtentAlone()
{
    ScrollLayoutParams params = defaultParams();
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));

    // Auto on a lone tile takes the whole column, so there is no slack to
    // split and the policy must be a no-op rather than a rounded-down offset.
    const QRect r = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(r), ScrollTestUtils::kCrossExtent);
    QCOMPARE(Ax::crossPos(r), 0);
}

void TestScrollStripSizing::centeringMeasuresTheWholeStackIncludingItsGaps()
{
    ScrollLayoutParams params = defaultParams();
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusAdjacentTile(-1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(200)));
    QVERIFY(strip.focusAdjacentTile(1));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));

    // 200 + gap + 300 = 510 of the 800 available, so 145 of slack leads.
    // Asserting the SECOND tile too is what pins the gap term: an offset
    // computed from the heights alone would still place the first one right.
    const int expectedStart = (ScrollTestUtils::kCrossExtent - (200 + params.gap + 300)) / 2;
    const auto resolved = strip.relayout(params);
    const QRect a = rectOf(resolved, QStringLiteral("a"));
    const QRect b = rectOf(resolved, QStringLiteral("b"));
    QCOMPARE(Ax::crossPos(a), expectedStart);
    QCOMPARE(Ax::crossLen(a), 200);
    QCOMPARE(Ax::crossPos(b), expectedStart + 200 + params.gap);
    QCOMPARE(Ax::crossLen(b), 300);
}

// A tabbed column is the second arm the policy touches: it takes its own
// cross extent from the owning tab's intent, so it can be short for a reason
// the stack branch never sees. Its tiles ride the column rect, so proving the
// column moved and the hidden tab came with it is the whole contract.
void TestScrollStripSizing::centeringMovesAShortTabbedColumnAndItsTabsTogether()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(400)));

    const auto before = strip.relayout(params);
    const QRect uncentred = rectOf(before, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(uncentred), 400);
    QCOMPARE(Ax::crossPos(uncentred), 0);
    // Pinned so the indicator assertion below cannot pass vacuously on a null
    // rect: the indicator is enabled by default and this column carries two
    // tabs, so it genuinely resolves.
    QVERIFY(before.columns.first().tabIndicatorRect.isValid());

    params.centerShortColumns = true;
    const auto resolved = strip.relayout(params);
    const QRect shown = rectOf(resolved, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(shown), 400);
    QCOMPARE(Ax::crossPos(shown), (ScrollTestUtils::kCrossExtent - 400) / 2);
    // The hidden tab shares the shown tab's rect, so it must have moved too.
    QCOMPARE(rectOf(resolved, QStringLiteral("a")), shown);
    // The indicator is derived from the column rect, which the tabbed branch
    // moves outright, so it has to travel with the tiles rather than stay
    // pinned to the start edge.
    const QRect indicator = resolved.columns.first().tabIndicatorRect;
    // Derived from the shared cross constant and the column's own height, the
    // way every sibling line here does: hardcoding 800 restates
    // ScrollTestUtils::kCrossExtent, so a change to the fixture screen would
    // leave this line asserting a shift the strip no longer makes.
    QCOMPARE(Ax::crossPos(indicator),
             Ax::crossPos(before.columns.first().tabIndicatorRect) + (ScrollTestUtils::kCrossExtent - 400) / 2);
}

void TestScrollStripSizing::centeringMeasuresFromTheWorkAreasCrossOriginNotTheScreens()
{
    // Every other fixture here is anchored at the origin, which makes the
    // work area's cross ORIGIN indistinguishable from zero — an offset
    // computed as slack/2 alone would satisfy them all. A panel or an outer
    // gap insets the work area in production, so the origin has to be added.
    ScrollLayoutParams params = defaultParams();
    params.workArea = Ax::t(QRect(0, 40, 1200, 720));
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    // Odd slack (720 - 301 = 419), so the halving's rounding direction is
    // pinned too: floor toward the start edge, not round-half-up.
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(301)));

    const QRect r = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(r), 301);
    QCOMPARE(Ax::crossPos(r), 40 + (720 - 301) / 2);
}

void TestScrollStripSizing::centeringMovesOnlyTheShortColumnAndNotItsFullHeightNeighbour()
{
    // The offset is per COLUMN, measured from that column's own resolved
    // stack. Every other centring slot has a single column on the strip, so
    // hoisting the computation out of the per-column loop would satisfy them.
    ScrollLayoutParams params = defaultParams();
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    // A second COLUMN, not a second tile: left on Auto so it fills the cross
    // extent and must not move at all.
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    const auto resolved = strip.relayout(params);
    const QRect shortColumn = rectOf(resolved, QStringLiteral("a"));
    QCOMPARE(Ax::crossLen(shortColumn), 300);
    QCOMPARE(Ax::crossPos(shortColumn), (ScrollTestUtils::kCrossExtent - 300) / 2);

    const QRect fullColumn = rectOf(resolved, QStringLiteral("b"));
    QCOMPARE(Ax::crossLen(fullColumn), ScrollTestUtils::kCrossExtent);
    QCOMPARE(Ax::crossPos(fullColumn), 0);
}

void TestScrollStripSizing::centeringYieldsNoOffsetWhenTheStackOverflowsTheCrossExtent()
{
    // The clamp that keeps an overflowing stack at the start edge instead of
    // a NEGATIVE origin. Client minimums are the reachable route: the
    // rebalance cannot cut below them, so the resolved stack genuinely
    // exceeds the cross extent.
    ScrollLayoutParams params = defaultParams();
    QVERIFY(params.respectMinimumSize); // the arm that makes the overflow reachable
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // Three fifths each, so the two floors plus the gap cannot fit the extent.
    const int floorPx = ScrollTestUtils::kCrossExtent * 3 / 5;
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("a"), floorPx, floorPx));
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("b"), floorPx, floorPx));

    const auto resolved = strip.relayout(params);
    const QRect first = rectOf(resolved, QStringLiteral("a"));
    QVERIFY(Ax::crossLen(first) >= floorPx);
    QCOMPARE(Ax::crossPos(first), 0);
}

QTEST_APPLESS_MAIN(TestScrollStripSizing)
#include "test_scrollstrip_sizing.moc"
