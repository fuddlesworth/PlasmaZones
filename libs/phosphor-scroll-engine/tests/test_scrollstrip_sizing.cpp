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
    void heightPresetCycleMeasuresATabbedColumnInColumnSpace();
    void maximizeToggleRefusesAColumnPinnedByItsMinimum();
    void heightAdjustFloorsATabbedColumnAtTheTallestTabsMinimum();
    void switchingTabsDoesNotResizeATabbedColumn();
    void aHeightPressTakesTabbedOwnershipFromTheOtherTab();
    void aZeroMovementHeightPressStillReportsTheOwnershipClaim();
    void bothPresetCyclesRefuseADeltaThatIsNotOneStep();
    void tabbingAStackPicksTheShownTabAndUntabbingRestoresEveryHeight();
    void closingTheOwningTabHandsTheExtentToTheTabOnShow();
    void heightGrowLeavesTheColumnTilingItsBudget();
    void widthPresetCycleWrapsByExtentNotByPosition();
    void maximizeToggleEntersOnRenderedWidthNotIntentKind();
    void maximizeUnmaximizeSkipsAStaleFullWidthRestoreSlot();
    void maximizeToEdgesResolvesTheRawAreaGapFree();
    void maximizeToEdgesRestoreIsJustTheStoredIntentAgain();
    void widthAndHeightVerbsClearMaximizeToEdges();
    void equalizeClearsTheActiveColumnsMaximizeToEdges();
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

// The geometry contract of the maximize-to-edges flag: the column resolves
// against the RAW work area (pre outer gaps) on BOTH axes, its stacked tiles
// divide the raw cross extent with no inner gap, and a sibling column keeps
// its ordinary gapped resolution.
void TestScrollStripSizing::maximizeToEdgesResolvesTheRawAreaGapFree()
{
    ScrollLayoutParams params = defaultParams();
    // Simulated outer gaps: the gapped work area sits 20px inside the raw
    // screen rect on every side. rawWorkArea is what engine_query captures
    // BEFORE the shrink.
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));

    const ResolvedStrip resolved = strip.relayout(params);
    const QRect a = rectOf(resolved, QStringLiteral("a"));
    const QRect b = rectOf(resolved, QStringLiteral("b"));
    QCOMPARE(Ax::mainLen(a), Ax::mainLen(params.rawWorkArea));
    QCOMPARE(Ax::mainLen(b), Ax::mainLen(params.rawWorkArea));
    // Gap-free stack: the two cross extents partition the raw cross exactly.
    QCOMPARE(Ax::crossLen(a) + Ax::crossLen(b), Ax::crossLen(params.rawWorkArea));
    // And the union is the raw rect itself, corners included — the emitted
    // rect snaps to the raw area whenever the column covers the viewport.
    QCOMPARE(a.united(b), params.rawWorkArea);
}

// Un-maximizing is "stop overriding": the stored width intent was never
// touched, so one toggle out re-renders exactly the pre-toggle rects with no
// pre-maximize slot involved.
void TestScrollStripSizing::maximizeToEdgesRestoreIsJustTheStoredIntentAgain()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(377), ColumnDisplay::Normal, params));
    const QRect before = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(Ax::mainLen(before), 377);

    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QCOMPARE(strip.columns().first().width, ColumnWidth::makeFixed(377)); // intent untouched while flagged
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")), before);
}

// D3: any width or height verb on a flagged column drops the flag first and
// reports the drop as a change, so a verb that would otherwise refuse still
// visibly un-maximizes.
//
// EVERY clearing site in this file is driven, not a representative sample: the
// contract is spelled out per verb across a dozen sites, so a slot that drove
// four of them left deleting the clear from the other eight green. The one
// site this fixture cannot reach is the per-member drop inside
// equalizeVisibleColumnWidths' write loop: the fixture sets a 20px outer gap,
// so a flagged column renders at the RAW main extent, fills the viewport
// alone, and the verb's two-fully-visible-columns gate refuses before the loop
// runs. It IS reachable with zero outer gap, where the raw extent equals the
// viewport (see scrollstrip_sizing.cpp's note on that loop). The
// active-column drop above it is covered below.
//
// Each step asserts the GEOMETRY as well as the verdict, this file's rule: the
// fixture gives the raw area a 20px outer gap on every side, so a column that
// is still flagged renders WIDER than the gapped work area and a
// verdict-only pass cannot hide a flag that was reported dropped and was not.
void TestScrollStripSizing::widthAndHeightVerbsClearMaximizeToEdges()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);
    const int gappedMain = Ax::mainLen(params.workArea);
    const int rawMain = Ax::mainLen(params.rawWorkArea);
    QVERIFY2(rawMain > gappedMain, "the fixture must be able to tell a flagged column from an un-flagged one");

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));

    const auto flagged = [&strip] {
        return strip.columns().first().maximizedToEdges;
    };
    const auto renderedMain = [&strip, &params] {
        return Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("a")));
    };

    // The premise the geometry assertions rest on: while flagged the column
    // renders at the RAW extent, so "back inside the gapped area" is a real
    // statement about the flag and not a tautology.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY(flagged());
    QCOMPARE(renderedMain(), rawMain);

    // -- Width verbs --------------------------------------------------
    //
    // Already flagged from the premise check above.
    QVERIFY2(strip.cycleActiveColumnPresetWidth(1, params), "the preset cycle must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.adjustActiveColumnWidth(10, params), "the width adjust must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // An explicit width SET that matches the stored intent still clears, and
    // the clear alone is the reported change.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    const ColumnWidth stored = strip.columns().first().width;
    QVERIFY2(strip.setActiveColumnWidth(stored), "an equal-intent width set must still report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.minimizeActiveColumnWidth(params), "the minimize verb must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // Expand: the sole column takes the whole viewport, which the verb routes
    // through the width-maximize toggle. Either way the flag is gone and the
    // column is back inside the gapped area.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.expandActiveColumnToAvailableWidth(params), "the expand verb must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // The width-maximize toggle is a width verb too. Entered from the full
    // width the expand above left behind, so this press takes the un-maximize
    // arm and the column ends narrower than the viewport either way.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.toggleMaximizeActiveColumn(params), "the width-maximize toggle must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // "Back to the layout's defaults" with a default width supplied.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.resetToDefaults(kHalf, std::nullopt, ColumnDisplay::Normal, params),
             "a reset carrying a default width must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // -- Height verbs -------------------------------------------------
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)), "a height write must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.resetActiveColumnHeights(), "the height reset must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // Driven from the Auto the reset above left, so the cycle genuinely lands
    // on a different intent and the clear is not riding a no-op.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.cycleActiveWindowPresetHeight(1, params), "the height preset cycle must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.adjustActiveWindowHeight(-25.0, params), "the height adjust must report the drop");
    QVERIFY(!flagged());
    QVERIFY(renderedMain() <= gappedMain);

    // -- The interactive resize ack -----------------------------------
    //
    // The acked main extent EQUALS what the stored intent already resolves to,
    // which is the case the drop used to be skipped in: the next relayout then
    // snapped the column back to the raw extent and reverted the user's drag.
    const QRect settled = rectOf(strip.relayout(params), QStringLiteral("a"));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY2(strip.reconcileWindowSize(QStringLiteral("a"), Ax::t(QSize(Ax::mainLen(settled), Ax::crossLen(settled))),
                                       /*mainChanged=*/true, /*crossChanged=*/false, params),
             "an equal-extent resize ack must still report the drop");
    QVERIFY(!flagged());
    QCOMPARE(renderedMain(), Ax::mainLen(settled));
}

// The active column's drop inside equalizeVisibleColumnWidths, which is a
// separate site from the loop's per-member one: without it a flagged column
// measures at the raw extent, never appears among the fully visible columns,
// and the verb refused outright instead of equalizing.
void TestScrollStripSizing::equalizeClearsTheActiveColumnsMaximizeToEdges()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    // Two columns narrow enough that both sit ENTIRELY inside the gapped
    // viewport, which is what equalize measures over.
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));

    QVERIFY2(strip.equalizeVisibleColumnWidths(params), "equalize must not refuse a maximized active column");
    for (const Column& col : strip.columns()) {
        QVERIFY2(!col.maximizedToEdges, "no column may still be maximized to edges after an equalize");
    }
    // And the verb did its own job as well as the drop: the two columns end at
    // the same extent, both inside the gapped work area.
    const ResolvedStrip resolved = strip.relayout(params);
    const int aMain = Ax::mainLen(rectOf(resolved, QStringLiteral("a")));
    const int bMain = Ax::mainLen(rectOf(resolved, QStringLiteral("b")));
    QCOMPARE(aMain, bMain);
    QVERIFY(aMain <= Ax::mainLen(params.workArea));
}

QTEST_APPLESS_MAIN(TestScrollStripSizing)
#include "test_scrollstrip_sizing.moc"
