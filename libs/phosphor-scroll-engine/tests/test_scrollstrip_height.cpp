// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The WHOLE-SIZE window-height verbs: the maximize toggle and its restore, the
// minimize press, and grow-into-empty-space. The mirror of the source split —
// these are the verbs that live in scrollstrip_sizing_height.cpp — and split
// out of test_scrollstrip_sizing for the same reason that file was split out
// of test_scrollstrip_ops: it reached the size ceiling. That file keeps the
// REPEATABLE verbs (adjust, the preset cycles) and the tabbed-ownership and
// centering contracts.
//
// What every slot here is really guarding is a verb that reports a change and
// moves nothing, or moves something the user did not ask for. Each therefore
// asserts resolved GEOMETRY or the stored INTENT beside the verdict, never the
// verdict alone: these verbs sit on a key repeat, and a bool that says
// "changed" while the screen stands still costs a relayout and a success OSD
// per press, forever.

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

class TestScrollStripHeight : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }
    // Declaration order IS definition order, and the run order, as in the
    // sibling suites.
    void aMinimizePressNeverHandsATabbedColumnToAnAutoOwner();
    void growIntoEmptySpaceRefusesAFullTabbedColumnRatherThanUnMaximizingIt();
    void unMaximizingAWindowHeightPutsBackTheHeightItDisplaced();
    void aBudgetChangeBetweenPressesDoesNotStrandTheMaximizeToggle();
};

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
void TestScrollStripHeight::aMinimizePressNeverHandsATabbedColumnToAnAutoOwner()
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
void TestScrollStripHeight::growIntoEmptySpaceRefusesAFullTabbedColumnRatherThanUnMaximizingIt()
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

// The maximize toggle remembers the height it displaced, so un-maximizing puts
// that height back instead of dropping to Auto. Auto is only the answer when
// there is nothing remembered — a tile that reached the budget by some other
// route, which the round trip in the ops suite covers by entering from Auto.
//
// The second half is the other half of the contract: the memory is not
// permanent. A height the user picks by ANY other means countermands the
// maximize, and the settled frame of an interactive resize is one of those —
// it arrives through reconcileWindowSize, which is reached only from a user's
// finished resize gesture.
void TestScrollStripHeight::unMaximizingAWindowHeightPutsBackTheHeightItDisplaced()
{
    const ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));

    // "b" is active and given a height of its own. Budget is 800 - one 10px
    // gap = 790, so the Auto sibling takes the 490 that is left.
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(Ax::crossLen(rectOf(r, QStringLiteral("b"))), 300);
    QCOMPARE(Ax::crossLen(rectOf(r, QStringLiteral("a"))), 490);

    // Maximize. "a" is squeezed to the one-pixel floor, which is why "b"
    // renders 789 rather than the 790 its intent asks for.
    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));
    r = strip.relayout(params);
    QCOMPARE(Ax::crossLen(rectOf(r, QStringLiteral("b"))), 789);

    // Un-maximize returns the 300 the maximize displaced, NOT Auto's even
    // share. This is the assertion the restore slot exists for: without it
    // both tiles come back at 395.
    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));
    r = strip.relayout(params);
    QCOMPARE(Ax::crossLen(rectOf(r, QStringLiteral("b"))), 300);
    QCOMPARE(Ax::crossLen(rectOf(r, QStringLiteral("a"))), 490);
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    const Tile& b = col->tiles.at(col->indexOfWindow(QStringLiteral("b")));
    QCOMPARE(b.height.kind, WindowHeight::Fixed);
    QCOMPARE(b.height.fixedPx, 300);
    QVERIFY2(!b.preMaximizeHeight.has_value(), "un-maximizing must spend the slot, not keep it");

    // Maximize again, then settle an interactive resize on top of it. The
    // resize is the user choosing a height, so it replaces the memory rather
    // than being undone by the next press.
    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));
    QVERIFY(strip.reconcileWindowSize(QStringLiteral("b"), Ax::t(QSize(999, 500)), /*mainChanged=*/false,
                                      /*crossChanged=*/true, params));
    col = strip.activeColumn();
    QVERIFY(col);
    QVERIFY2(!col->tiles.at(col->indexOfWindow(QStringLiteral("b"))).preMaximizeHeight.has_value(),
             "a settled user resize must countermand the maximize it lands on");

    // So the next press maximizes from 500, and the one after that comes back
    // to 500 — never to the 300 from before the resize.
    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));
    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 500);
}

// The restore slot is a MEMORY, so the thing to guard is it going stale. The
// budget is not a constant: it is the cross extent less one gap per inter-tile
// seam, so closing a sibling — or tabbing the column — makes it GROW between
// two presses of the same key.
//
// A tile left holding Fixed(oldBudget) then reads as not-maximized against the
// larger budget and the press takes the maximize arm again. Before the slot
// existed that was harmless: the arm wrote Fixed(newBudget) and the press after
// it un-maximized to Auto. With a slot it is not, because the maximize arm
// would record the near-full height it just displaced as the height to go back
// to, throwing away what the user actually had, and the toggle would then
// alternate between two full-height values forever with Auto unreachable.
//
// What stops it is that the slot ITSELF answers "am I maximized", so a budget
// that moves under a maximized tile cannot re-enter the maximize arm at all.
void TestScrollStripHeight::aBudgetChangeBetweenPressesDoesNotStrandTheMaximizeToggle()
{
    const ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    // Three tiles spend two gaps, so the budget is 780. "c" is the active one.
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("c"))), 300);

    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));

    // Closing a sibling leaves one gap, so the budget becomes 790 — larger than
    // the height the maximize just wrote.
    QVERIFY(strip.removeWindow(QStringLiteral("a"), params));

    // The next press must still read this tile as maximized and hand back the
    // 300. Re-inferring maximized state from the height alone puts Fixed(780)
    // under the new 790 budget, so the press maximizes again and the 300 is
    // overwritten with 780.
    QVERIFY(strip.toggleMaximizeActiveWindowHeight(params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("c"))), 300);
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    const Tile& c = col->tiles.at(col->indexOfWindow(QStringLiteral("c")));
    QCOMPARE(c.height.kind, WindowHeight::Fixed);
    QCOMPARE(c.height.fixedPx, 300);
    QVERIFY2(!c.preMaximizeHeight.has_value(), "un-maximizing must spend the slot");
}

QTEST_APPLESS_MAIN(TestScrollStripHeight)
#include "test_scrollstrip_height.moc"
