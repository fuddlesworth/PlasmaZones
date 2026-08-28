// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The strip's VIEW ownership and the width re-flow verbs: who owns the view
// anchor after a pan (the detach latch), which verbs hand it back to the
// centering policy, and the three verbs that rewrite several column widths
// at once (equalize, minimize, reset). Placed in its own file rather than
// grown into test_scrollstrip_ops, which owns the per-operation surface over
// one shared strip fixture and already carries a file-size exception; every
// slot here builds its own strip against the shared screen constants.

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

class TestScrollStripView : public QObject
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
    void scrollViewByDetachesTheViewFromTheCenteringPolicy();
    void viewDetachmentEndsAtFocusAndAtBothCenteringVerbs();
    void equalizeSharesTheViewportAmongFullyVisibleColumns();
    void minimizeColumnWidthTakesTheSmallestPreset();
    void resetToDefaultsRestoresEveryIntent();
    void viewDetachmentEndsWhenAStructuralChangeMovesFocus();
    void equalizeHoldsTheGroupsLeadEdgeWhenTheActiveColumnIsNotFirst();
    void equalizeLeavesALeadEdgeStraddlerAlone();
    void equalizeRefusesWhenTheActiveColumnStraddlesAnEdge();
    void equalizeDetachesTheViewSoAlwaysCenteringCannotUndoIt();
    void equalizeGivesAMinimumBoundColumnItsFloor();
    void expandReclaimsAStraddlersPixels();
    void expandMaximizesWhenTheActiveColumnIsAloneOnScreen();
    void expandRefusesForAStraddlingActiveColumn();
    void expandMaximizesUnderACenteringPolicy();
    void resetToDefaultsClearsThePreMaximizeSlot();
    void predictedFocusScrollMatchesTheScrollAFocusActuallyCosts();
    void predictedFocusScrollIsZeroForEveryFailOpenInput();
};

void TestScrollStripView::scrollViewByDetachesTheViewFromTheCenteringPolicy()
{
    // The pan's whole promise: a view the USER scrolled to survives the next
    // applyLayout. updateViewForFocus runs at the top of every one, and under
    // Always it re-derives unconditionally — so without the detach latch the
    // second QCOMPARE below reads the centered offset back, and the pan is a
    // single frame the user never sees.
    ScrollLayoutParams params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip strip;
    const ColumnWidth wide = ColumnWidth::makeProportion(0.55);
    QVERIFY(strip.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), wide, ColumnDisplay::Normal, params));
    const auto viewX = [&]() {
        return strip.relayout(params).viewOffset;
    };

    // A strip nobody has panned is the policy's, and stays the policy's across
    // any number of passes.
    QVERIFY(!strip.viewDetached());
    strip.updateViewForFocus(params);
    const int centered = viewX();
    strip.updateViewForFocus(params);
    QCOMPARE(viewX(), centered);

    // Centering the LAST column parks the view past the strip's end by design,
    // so the pan that is possible from here is the backward one. Its direction
    // does not matter to the claim under test; that it STICKS does.
    QVERIFY(strip.scrollViewBy(-20, params));
    QVERIFY(strip.viewDetached());
    QCOMPARE(viewX(), centered - 20);
    strip.updateViewForFocus(params);
    QCOMPARE(viewX(), centered - 20);
    QVERIFY(strip.viewDetached());

    // And it is not one pass of grace: the latch holds until something asks
    // for the policy back.
    strip.updateViewForFocus(params);
    strip.updateViewForFocus(params);
    QCOMPARE(viewX(), centered - 20);

    // No re-clamp on the way out either, and this is the arm that proves it:
    // the pan inherited a centered anchor and so sits PAST the strip's end,
    // where clamping a detached view would snap it the whole way back in one
    // pass — the exact motion scrollViewBy's delta clamp exists to prevent,
    // arriving through the other door. Derived from the resolve rather than
    // from the 0.55 literal, because proportions are gap-aware and a hand
    // computation here would encode a second, drifting copy of that rule.
    const int maxViewOffset = qMax(0, strip.relayout(params).stripExtent - ScrollTestUtils::kMainExtent);
    QVERIFY2(viewX() > maxViewOffset,
             qPrintable(QStringLiteral("expected a past-max viewX, got %1 (max %2)").arg(viewX()).arg(maxViewOffset)));

    // A refused pan leaves the latch exactly as it found it — a caller holding
    // a scroll key against the strip's end must not change what the next
    // layout pass does. Probed from the ATTACHED side, where a spurious set is
    // what would show.
    QVERIFY(strip.focusColumn(0, params)); // re-attaches, per the sibling test
    QVERIFY(!strip.viewDetached());
    QVERIFY(!strip.scrollViewBy(0, params));
    QVERIFY(!strip.viewDetached());
}

void TestScrollStripView::viewDetachmentEndsAtFocusAndAtBothCenteringVerbs()
{
    // The three ways back. Each hands the view to the policy, so each must
    // clear the latch — a verb that moved the view while leaving it detached
    // would make the NEXT layout pass behave differently than it does
    // everywhere else, which is the kind of divergence nothing else catches.
    const auto params = defaultParams();
    const ColumnWidth wide = ColumnWidth::makeProportion(0.55);
    // Fills @p s and pans it. A void helper, not a factory: QVERIFY returns
    // from the enclosing function on failure, so a value-returning lambda
    // could not carry these preconditions at all.
    const auto pan = [&](ScrollStrip& s) {
        QVERIFY(s.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, params));
        QVERIFY(s.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, params));
        QVERIFY(s.insertWindow(QStringLiteral("c"), wide, ColumnDisplay::Normal, params));
        QVERIFY(s.scrollViewBy(-30, params));
        QVERIFY(s.viewDetached());
    };

    // 1. A focus change. reanchorAfterFocusChange is the chokepoint every
    //    focus-driven re-anchor passes through, so this covers the focus verbs
    //    and the structural paths in one.
    ScrollStrip byFocus;
    pan(byFocus);
    QVERIFY(byFocus.focusColumn(0, params));
    QVERIFY(!byFocus.viewDetached());

    // 2. center-column.
    ScrollStrip byCenter;
    pan(byCenter);
    byCenter.centerActiveColumn(params);
    QVERIFY(!byCenter.viewDetached());

    // 3. center-visible-columns.
    ScrollStrip bySpan;
    pan(bySpan);
    bySpan.centerVisibleColumns(params);
    QVERIFY(!bySpan.viewDetached());

    // Centering re-attaches even when it reports no movement: the anchor it
    // wants can be the one the pan happens to have landed on, and a verb that
    // answered "nothing to do" while leaving the view detached would strand
    // the latch where no later verb obviously clears it.
    // The detached-but-already-centered state is built on purpose: centering
    // once (which moves, and re-attaches on the moving path) and then panning
    // forward and back by the same step lands the anchor exactly where the
    // policy wants it while the pan's latch is set. Only a clear placed
    // ABOVE the no-move bail survives the verb that then refuses; a clear
    // below it would leave every assertion here green while stranding the
    // latch. The MIDDLE column is centered: a centered end column sits
    // outside the scroll range, where scrollViewBy's delta clamp refuses to
    // retrace a step (it never pushes an out-of-range view further out).
    ScrollStrip settled;
    pan(settled);
    QVERIFY(settled.focusColumn(1, params));
    settled.centerActiveColumn(params);
    QVERIFY(!settled.viewDetached());
    QVERIFY(settled.scrollViewBy(-1, params));
    QVERIFY(settled.scrollViewBy(1, params));
    QVERIFY(settled.viewDetached());
    QVERIFY(!settled.centerActiveColumn(params)); // nothing to move
    QVERIFY(!settled.viewDetached());
    ScrollStrip settledSpan;
    pan(settledSpan);
    QVERIFY(settledSpan.focusColumn(1, params));
    settledSpan.centerVisibleColumns(params);
    QVERIFY(!settledSpan.viewDetached());
    QVERIFY(settledSpan.scrollViewBy(-1, params));
    QVERIFY(settledSpan.scrollViewBy(1, params));
    QVERIFY(settledSpan.viewDetached());
    QVERIFY(!settledSpan.centerVisibleColumns(params)); // nothing to move
    QVERIFY(!settledSpan.viewDetached());

    // The two halves of a dying screen, which pull opposite ways on purpose.
    ScrollLayoutParams degenerate = params;
    degenerate.workArea = QRect();

    // A centering verb that refuses OUTRIGHT changes neither half of the view:
    // it is a no-op, and a no-op must not hand the view to a policy that
    // cannot compute a position against nothing.
    ScrollStrip refused;
    pan(refused);
    QVERIFY(!refused.centerVisibleColumns(degenerate));
    QVERIFY(refused.viewDetached());

    // A focus change is NOT a no-op — the focus moved — so it re-attaches even
    // here, and leaves the position for the first relayout against a real work
    // area to derive. Without this arm the latch would outlive the pan's
    // meaning: nothing revisits the ownership question afterwards.
    ScrollStrip refocused;
    pan(refocused);
    const int anchorBefore = refocused.viewAnchor();
    QVERIFY(refocused.focusColumn(0, degenerate));
    QVERIFY(!refocused.viewDetached());
    QCOMPARE(refocused.viewAnchor(), anchorBefore);
}

void TestScrollStripView::equalizeSharesTheViewportAmongFullyVisibleColumns()
{
    const auto params = defaultParams();
    ScrollStrip strip;
    // 0.30 + 0.30 fit the viewport with room to spare; the 0.55 third
    // column straddles the trailing edge, which is the column the verb must
    // leave alone. Focus stays on the first so the view sits at the start.
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.30), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeProportion(0.30), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeProportion(0.55), ColumnDisplay::Normal, params));
    QVERIFY(strip.focusColumn(0, params));
    const ColumnWidth straddlerBefore = strip.columns().at(2).width;

    QVERIFY(strip.equalizeVisibleColumnWidths(params));
    // Two fully visible columns share the MAIN extent net of the gap between
    // them: (1200 - 10) / 2 = 595 each, remainder 0. Written as Fixed, like
    // adjustActiveColumnWidth.
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeFixed(595));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeFixed(595));
    // The straddler is untouched, intent and all.
    QCOMPARE(strip.columns().at(2).width, straddlerBefore);
    // The group tiles edge to edge: b ends exactly at the viewport's end.
    const ResolvedStrip after = strip.relayout(params);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("b"))) + Ax::mainLen(rectOf(after, QStringLiteral("b"))),
             ScrollTestUtils::kMainExtent);

    // Already equal: refuses, so the engine skips a pointless relayout and
    // the OSD says no_target rather than claiming a change.
    QVERIFY(!strip.equalizeVisibleColumnWidths(params));

    // Three in view with a remainder: 1200 - 2*10 = 1180, 1180 / 3 = 393 r1.
    // The odd pixel goes to the LAST column so nothing is left as dead space.
    ScrollStrip three;
    for (const char* id : {"a", "b", "c"}) {
        QVERIFY(three.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.30), ColumnDisplay::Normal,
                                   params));
    }
    QVERIFY(three.focusColumn(0, params));
    QVERIFY(three.equalizeVisibleColumnWidths(params));
    QCOMPARE(three.columns().at(0).width, ColumnWidth::makeFixed(393));
    QCOMPARE(three.columns().at(1).width, ColumnWidth::makeFixed(393));
    QCOMPARE(three.columns().at(2).width, ColumnWidth::makeFixed(394));

    // One column fully visible has nothing to equalize against — that is
    // maximize's job, and reporting success here would be a lie.
    ScrollStrip lone;
    QVERIFY(lone.insertWindow(QStringLiteral("only"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(!lone.equalizeVisibleColumnWidths(params));
    QCOMPARE(lone.columns().at(0).width, kHalf);
}

void TestScrollStripView::minimizeColumnWidthTakesTheSmallestPreset()
{
    ScrollLayoutParams params = defaultParams();
    // Deliberately NOT sorted: no producer sorts the preset list (the schema
    // and the engine's parser both keep the user's order), so the verb has
    // to search for the minimum rather than read the first entry. A sorted
    // fixture here would pass either way.
    params.presetColumnWidths = {0.5, 1.0 / 3.0, 2.0 / 3.0};
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));

    // Lands on the smallest preset as a PRESET intent, so it follows the
    // vocabulary if the list is later edited.
    QVERIFY(strip.minimizeActiveColumnWidth(params));
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makePreset(1.0 / 3.0));
    // Already there: refuses.
    QVERIFY(!strip.minimizeActiveColumnWidth(params));

    // Refusal is by RENDERED pixels, not intent: a Proportion that renders
    // to the same extent as the smallest preset is already minimized, and
    // rewriting it would move nothing while reporting success.
    ScrollStrip same;
    QVERIFY(
        same.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(1.0 / 3.0), ColumnDisplay::Normal, params));
    QVERIFY(!same.minimizeActiveColumnWidth(params));
    // The same refusal when the column's MINIMUM SIZE already pins it at or
    // above the smallest preset: the floor is what renders (equalize's
    // reason), so rewriting the intent would move nothing on screen, and a
    // first press that reported success for that would be a no-op with an
    // OSD. The minimum is set on both axes so it binds on either arm.
    ScrollStrip floored;
    QVERIFY(floored.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(floored.setWindowMinimumSize(QStringLiteral("a"), 600, 600));
    QVERIFY(!floored.minimizeActiveColumnWidth(params));
    QCOMPARE(floored.columns().at(0).width, kHalf);

    // No vocabulary at all: the engine floor, as a Proportion.
    ScrollLayoutParams noPresets = params;
    noPresets.presetColumnWidths.clear();
    ScrollStrip floor;
    QVERIFY(floor.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, noPresets));
    QVERIFY(floor.minimizeActiveColumnWidth(noPresets));
    QCOMPARE(floor.columns().at(0).width, ColumnWidth::makeProportion(MinColumnWidthFraction));

    // A degenerate work area writes nothing: persisted intent must not be
    // rewritten against a viewport that does not exist.
    ScrollLayoutParams degenerate = params;
    degenerate.workArea = QRect();
    ScrollStrip dying;
    QVERIFY(dying.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(!dying.minimizeActiveColumnWidth(degenerate));
    QCOMPARE(dying.columns().at(0).width, kHalf);
}

void TestScrollStripView::resetToDefaultsRestoresEveryIntent()
{
    ScrollLayoutParams params = defaultParams();
    params.defaultColumnWidth = ColumnWidth::makeProportion(0.4);
    ScrollStrip strip;
    // Two columns, the second tabbed with a stacked pair, every intent
    // pushed off its default: a Fixed width, a tabbed display, a fixed
    // window height, and a maximize on the first so the pre-maximize slot
    // holds something.
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(strip.consumeWindowIntoColumn(params)); // c joins b
    QVERIFY(strip.toggleActiveColumnTabbed());
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeProportion(1.0));

    QVERIFY(strip.resetToDefaults(params.defaultColumnWidth, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));
    for (const Column& col : strip.columns()) {
        QCOMPARE(col.width, params.defaultColumnWidth);
        QCOMPARE(col.display, ColumnDisplay::Normal);
        for (const Tile& tile : col.tiles) {
            QCOMPARE(tile.height, WindowHeight::makeAuto());
        }
    }
    // The toggle still round-trips afterwards. This does NOT prove the
    // pre-maximize slot was cleared (a maximize from a non-full width
    // overwrites the slot before the un-maximize reads it);
    // resetToDefaultsClearsThePreMaximizeSlot is the arm that can tell.
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(0).width, params.defaultColumnWidth);

    // Already at the defaults: refuses.
    QVERIFY(!strip.resetToDefaults(params.defaultColumnWidth, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));

    // No default width at all ("the client decides"): the widths are left
    // exactly as they are, and only the display and heights reset.
    ScrollStrip clientSized;
    QVERIFY(clientSized.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(640), ColumnDisplay::Normal, params));
    QVERIFY(clientSized.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(480), ColumnDisplay::Normal, params));
    QVERIFY(clientSized.toggleActiveColumnTabbed());
    QVERIFY(clientSized.resetToDefaults(std::nullopt, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));
    QCOMPARE(clientSized.columns().at(0).width, ColumnWidth::makeFixed(640));
    QCOMPARE(clientSized.columns().at(1).width, ColumnWidth::makeFixed(480));
    QCOMPARE(clientSized.columns().at(1).display, ColumnDisplay::Normal);
    // And with nothing but widths off their (absent) default, there is
    // nothing to reset.
    QVERIFY(!clientSized.resetToDefaults(std::nullopt, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));

    // No default HEIGHT either ("the client decides" on the cross axis): the
    // tile heights are left exactly as they are, while the width and display
    // still reset. The two absences are independent — this strip has a width
    // default and no height one.
    ScrollStrip clientTall;
    QVERIFY(clientTall.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(clientTall.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(clientTall.focusColumn(0, params));
    QVERIFY(clientTall.consumeWindowIntoColumn(params)); // b joins a
    QVERIFY(clientTall.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    QVERIFY(clientTall.setActiveColumnWidth(ColumnWidth::makeFixed(640)));
    const QList<WindowHeight> heightsBefore = [&] {
        QList<WindowHeight> out;
        for (const Tile& tile : clientTall.columns().at(0).tiles) {
            out.append(tile.height);
        }
        return out;
    }();
    QVERIFY(clientTall.resetToDefaults(params.defaultColumnWidth, std::nullopt, ColumnDisplay::Normal, params));
    QCOMPARE(clientTall.columns().at(0).width, params.defaultColumnWidth);
    for (int i = 0; i < heightsBefore.size(); ++i) {
        QCOMPARE(clientTall.columns().at(0).tiles.at(i).height, heightsBefore.at(i));
    }
    // Nothing but the height off its (absent) default: nothing to reset.
    QVERIFY(!clientTall.resetToDefaults(params.defaultColumnWidth, std::nullopt, ColumnDisplay::Normal, params));

    // Intent compare, not pixels: a Fixed that renders to the default's
    // extent is still reset, because it would not follow a later work-area
    // change the way the default would.
    ScrollStrip fixed;
    const int defaultPx = ScrollStrip::resolveColumnWidthPx(params.defaultColumnWidth, params);
    QVERIFY(fixed.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(defaultPx), ColumnDisplay::Normal, params));
    QVERIFY(fixed.resetToDefaults(params.defaultColumnWidth, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));
    QCOMPARE(fixed.columns().at(0).width, params.defaultColumnWidth);

    // The display default is an argument, and a Tabbed default is honoured.
    ScrollStrip tabbed;
    QVERIFY(tabbed.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(tabbed.resetToDefaults(params.defaultColumnWidth, WindowHeight::makeAuto(), ColumnDisplay::Tabbed, params));
    QCOMPARE(tabbed.columns().at(0).display, ColumnDisplay::Tabbed);
}

void TestScrollStripView::viewDetachmentEndsWhenAStructuralChangeMovesFocus()
{
    // The class doc promises that a FOCUS CHANGE ends a pan, and not every
    // focus change arrives through a focus verb: a removal that takes the
    // active column with it, and the strip emptying and refilling, move the
    // active column without passing reanchorAfterFocusChange. Each must
    // re-attach, or a pan outlives the column it was made against.
    const ColumnWidth wide = ColumnWidth::makeProportion(0.55);
    const auto fillAndPan = [&](ScrollStrip& s, const ScrollLayoutParams& p) {
        QVERIFY(s.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, p));
        QVERIFY(s.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, p));
        QVERIFY(s.insertWindow(QStringLiteral("c"), wide, ColumnDisplay::Normal, p));
        QVERIFY(s.scrollViewBy(-30, p));
        QVERIFY(s.viewDetached());
    };

    // 1. Closing the focused window under Never: the neighbour takes focus
    //    while fully visible, the arm that skips reanchorAfterFocusChange.
    const auto never = defaultParams();
    ScrollStrip closedNever;
    fillAndPan(closedNever, never);
    QVERIFY(closedNever.removeWindow(QStringLiteral("c"), never));
    QCOMPARE(closedNever.activeWindowId(), QStringLiteral("b"));
    QVERIFY(!closedNever.viewDetached());

    // 2. The same under Always, where the removal path writes the centered
    //    anchor itself rather than through the chokepoint.
    ScrollLayoutParams always = never;
    always.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip closedAlways;
    fillAndPan(closedAlways, always);
    QVERIFY(closedAlways.removeWindow(QStringLiteral("c"), always));
    QVERIFY(!closedAlways.viewDetached());

    // 3. Closing a NON-focused window keeps focus, so under Never the pan is
    //    the user's and holds (the focused column stays fully visible, so
    //    the removal has no reason to scroll it in); under Always the policy
    //    re-centers the focused column and so takes the view back. Focus is
    //    moved to the MIDDLE column first so the pan leaves it fully on
    //    screen: a pan that left the focused column clipped would be
    //    scrolled back in by the removal, which is the policy's call and not
    //    what this arm is about.
    ScrollStrip bystanderNever;
    fillAndPan(bystanderNever, never);
    QVERIFY(bystanderNever.focusColumn(1, never)); // re-attaches; pan again
    QVERIFY(bystanderNever.scrollViewBy(-30, never));
    QVERIFY(bystanderNever.viewDetached());
    QVERIFY(bystanderNever.removeWindow(QStringLiteral("a"), never));
    QCOMPARE(bystanderNever.activeWindowId(), QStringLiteral("b"));
    QVERIFY(bystanderNever.viewDetached());
    ScrollStrip bystanderAlways;
    fillAndPan(bystanderAlways, always);
    QVERIFY(bystanderAlways.focusColumn(1, always));
    QVERIFY(bystanderAlways.scrollViewBy(-30, always));
    QVERIFY(bystanderAlways.viewDetached());
    QVERIFY(bystanderAlways.removeWindow(QStringLiteral("a"), always));
    QVERIFY(!bystanderAlways.viewDetached());
    //    And under Never with the focused column deliberately PANNED PARTLY
    //    OFF the viewport (fillAndPan leaves the last column clipped by the
    //    pan): the removal's scroll-it-in arm would re-anchor exactly the
    //    view the user just made, and focus never moved, so the latch holds
    //    and the anchor is untouched. When the focused column itself goes,
    //    arm 1 above, the re-anchor still lands.
    ScrollStrip clippedNever;
    fillAndPan(clippedNever, never);
    const int clippedAnchor = clippedNever.viewAnchor();
    QVERIFY(clippedNever.removeWindow(QStringLiteral("a"), never));
    QCOMPARE(clippedNever.activeWindowId(), QStringLiteral("c"));
    QVERIFY(clippedNever.viewDetached());
    QCOMPARE(clippedNever.viewAnchor(), clippedAnchor);

    // 4. takeWindow of the focused window moves focus without the refocus
    //    policy, and still re-attaches.
    ScrollStrip taken;
    fillAndPan(taken, never);
    QVERIFY(taken.takeWindow(QStringLiteral("c"), never));
    QVERIFY(!taken.viewDetached());

    // 5. The strip emptying, then the first insert into it: no column is
    //    left for a pan to be attached to, and the fresh strip's first
    //    column must not inherit a latch from the one that emptied.
    ScrollStrip emptied;
    fillAndPan(emptied, never);
    for (const char* id : {"a", "b", "c"}) {
        QVERIFY(emptied.removeWindow(QString::fromLatin1(id), never));
    }
    QVERIFY(emptied.isEmpty());
    QVERIFY(!emptied.viewDetached());
    QVERIFY(emptied.insertWindow(QStringLiteral("d"), wide, ColumnDisplay::Normal, never));
    QVERIFY(!emptied.viewDetached());

    // 6. A consume under Always re-centers the focused column through
    //    keepOrRecenterAnchor, so the policy has the view again; under Never
    //    the same verb only re-clamps, and the pan holds.
    ScrollStrip consumedAlways;
    fillAndPan(consumedAlways, always);
    QVERIFY(consumedAlways.focusColumn(1, always)); // re-attaches; pan again
    QVERIFY(consumedAlways.scrollViewBy(-30, always));
    QVERIFY(consumedAlways.viewDetached());
    QVERIFY(consumedAlways.consumeWindowIntoColumn(always)); // c joins b
    QVERIFY(!consumedAlways.viewDetached());
    ScrollStrip consumedNever;
    fillAndPan(consumedNever, never);
    QVERIFY(consumedNever.focusColumn(1, never));
    QVERIFY(consumedNever.scrollViewBy(-30, never));
    QVERIFY(consumedNever.consumeWindowIntoColumn(never));
    QVERIFY(consumedNever.viewDetached());
}

void TestScrollStripView::equalizeHoldsTheGroupsLeadEdgeWhenTheActiveColumnIsNotFirst()
{
    // The anchor is stored relative to the ACTIVE column. With the active
    // column in the middle of the group, equalizing grows the column ahead
    // of it, which moves the active column's strip position; an anchor left
    // as it was would hold the active column's screen position instead and
    // slide the first column off the lead edge. The sibling test's
    // focusColumn(0) fixture cannot see this, because nothing is ahead of
    // column 0.
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b", "c"}) {
        QVERIFY(strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.30), ColumnDisplay::Normal,
                                   params));
    }
    QVERIFY(strip.focusColumn(1, params));
    QCOMPARE(strip.relayout(params).viewOffset, 0);

    QVERIFY(strip.equalizeVisibleColumnWidths(params));
    const ResolvedStrip after = strip.relayout(params);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("a"))), 0);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("c"))) + Ax::mainLen(rectOf(after, QStringLiteral("c"))),
             ScrollTestUtils::kMainExtent);
    // Focus did not move.
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));
}

void TestScrollStripView::equalizeLeavesALeadEdgeStraddlerAlone()
{
    // Four 0.30 columns with the LAST focused: the view scrolls so d ends at
    // the trailing edge, which clips a at the LEAD edge. a is the column the
    // `viewPos >= 0` half of fullyVisibleColumnIndices exists for; the sibling test
    // only ever clips the trailing edge.
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b", "c", "d"}) {
        QVERIFY(strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.30), ColumnDisplay::Normal,
                                   params));
    }
    QCOMPARE(strip.activeWindowId(), QStringLiteral("d"));
    const ResolvedStrip before = strip.relayout(params);
    QVERIFY2(Ax::mainPos(rectOf(before, QStringLiteral("a"))) < 0, "fixture: a must straddle the lead edge");
    QVERIFY2(Ax::mainPos(rectOf(before, QStringLiteral("b"))) >= 0, "fixture: b must be fully visible");
    const ColumnWidth straddlerBefore = strip.columns().at(0).width;

    QVERIFY(strip.equalizeVisibleColumnWidths(params));
    QCOMPARE(strip.columns().at(0).width, straddlerBefore);
    // b, c, d share 1200 - 2*10 = 1180: 393, 393, 394.
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeFixed(393));
    QCOMPARE(strip.columns().at(2).width, ColumnWidth::makeFixed(393));
    QCOMPARE(strip.columns().at(3).width, ColumnWidth::makeFixed(394));
    // The group now tiles the viewport from its lead edge, which pushes the
    // straddler fully out of view rather than leaving it clipped under a
    // group that no longer fits beside it.
    const ResolvedStrip after = strip.relayout(params);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("b"))), 0);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("d"))) + Ax::mainLen(rectOf(after, QStringLiteral("d"))),
             ScrollTestUtils::kMainExtent);
    QVERIFY(Ax::mainPos(rectOf(after, QStringLiteral("a"))) + Ax::mainLen(rectOf(after, QStringLiteral("a"))) <= 0);
}

void TestScrollStripView::equalizeRefusesWhenTheActiveColumnStraddlesAnEdge()
{
    // A pan can leave the FOCUSED column clipped at an edge. The verb's
    // re-anchor puts the group's first column at the lead edge, which would
    // push such an active column fully off screen, and the detached view it
    // leaves behind would keep it there. So an active column outside the
    // fully visible group is a refusal, and the pan's own latch is left as
    // the verb found it.
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b", "c", "d"}) {
        QVERIFY(strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.30), ColumnDisplay::Normal,
                                   params));
    }
    QCOMPARE(strip.activeWindowId(), QStringLiteral("d"));
    // d sits at the trailing edge; a pan toward the strip's start clips it.
    QVERIFY(strip.scrollViewBy(-30, params));
    QVERIFY(strip.viewDetached());
    const ResolvedStrip before = strip.relayout(params);
    QVERIFY2(Ax::mainPos(rectOf(before, QStringLiteral("d"))) + Ax::mainLen(rectOf(before, QStringLiteral("d")))
                 > ScrollTestUtils::kMainExtent,
             "fixture: the active column must straddle the trailing edge");
    const ColumnWidth bBefore = strip.columns().at(1).width;
    const int anchorBefore = strip.viewAnchor();

    QVERIFY(!strip.equalizeVisibleColumnWidths(params));
    QCOMPARE(strip.columns().at(1).width, bBefore);
    QCOMPARE(strip.viewAnchor(), anchorBefore);
    QVERIFY(strip.viewDetached());
}

void TestScrollStripView::equalizeDetachesTheViewSoAlwaysCenteringCannotUndoIt()
{
    // Under Always the policy re-centers the active column on every layout
    // pass. An equalize that tiled the group edge to edge and then let the
    // policy re-center would clip the group's last column, and a second
    // press would find a smaller group to split: a verb that never settles.
    // The verb detaches the view instead, so the pass after it leaves the
    // group where the verb put it and the second press refuses.
    ScrollLayoutParams params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip strip;
    for (const char* id : {"a", "b", "c", "d"}) {
        QVERIFY(strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.20), ColumnDisplay::Normal,
                                   params));
    }
    QVERIFY(strip.focusColumn(1, params));
    QVERIFY(!strip.viewDetached());
    strip.updateViewForFocus(params);
    const ResolvedStrip centered = strip.relayout(params);
    QVERIFY2(Ax::mainPos(rectOf(centered, QStringLiteral("a"))) > 0, "fixture: centering b must leave room ahead of a");

    QVERIFY(strip.equalizeVisibleColumnWidths(params));
    QVERIFY(strip.viewDetached());
    const auto tilesEdgeToEdge = [&]() {
        const ResolvedStrip r = strip.relayout(params);
        return Ax::mainPos(rectOf(r, QStringLiteral("a"))) == 0
            && Ax::mainPos(rectOf(r, QStringLiteral("d"))) + Ax::mainLen(rectOf(r, QStringLiteral("d")))
            == ScrollTestUtils::kMainExtent;
    };
    QVERIFY(tilesEdgeToEdge());
    // The layout pass the engine runs after the verb: the policy would
    // re-center b here, and must not.
    strip.updateViewForFocus(params);
    QVERIFY(tilesEdgeToEdge());
    // Idempotent: the same group, the same shares, nothing to do.
    const ColumnWidth aAfter = strip.columns().at(0).width;
    QVERIFY(!strip.equalizeVisibleColumnWidths(params));
    QCOMPARE(strip.columns().at(0).width, aAfter);
    QVERIFY(tilesEdgeToEdge());
    // And a focus change hands the view back to the policy as usual.
    QVERIFY(strip.focusColumn(2, params));
    QVERIFY(!strip.viewDetached());
}

void TestScrollStripView::equalizeGivesAMinimumBoundColumnItsFloor()
{
    // What renders is columnExtentPx, which floors a column at its tiles'
    // minimum. A share written below that floor would render at the floor,
    // overflow the viewport, and read back as "already equal" on the next
    // press. The minimum is set on BOTH axes so the floor binds on either
    // arm of the suite.
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b", "c"}) {
        QVERIFY(strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.20), ColumnDisplay::Normal,
                                   params));
    }
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("b"), 600, 600));
    const ResolvedStrip before = strip.relayout(params);
    QVERIFY2(Ax::mainPos(rectOf(before, QStringLiteral("c"))) + Ax::mainLen(rectOf(before, QStringLiteral("c")))
                 <= ScrollTestUtils::kMainExtent,
             "fixture: all three must be fully visible");

    // 1180 / 3 = 393 is under b's 600 floor, so b keeps 600 and a, c split
    // the remaining 580 evenly. b's INTENT is left alone: it already renders
    // at its floor, and the resolved-pixel compare does not rewrite an
    // intent for an extent it already has (toggleMaximizeActiveColumn's
    // reason).
    QVERIFY(strip.equalizeVisibleColumnWidths(params));
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeFixed(290));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeProportion(0.20));
    QCOMPARE(strip.columns().at(2).width, ColumnWidth::makeFixed(290));
    const ResolvedStrip after = strip.relayout(params);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("c"))) + Ax::mainLen(rectOf(after, QStringLiteral("c"))),
             ScrollTestUtils::kMainExtent);
    // A second press finds nothing to change.
    QVERIFY(!strip.equalizeVisibleColumnWidths(params));

    // Floors that alone outrun the viewport: refuse, touch nothing.
    ScrollStrip stuck;
    for (const char* id : {"a", "b"}) {
        QVERIFY(stuck.insertWindow(QString::fromLatin1(id), ColumnWidth::makeProportion(0.20), ColumnDisplay::Normal,
                                   params));
    }
    QVERIFY(stuck.setWindowMinimumSize(QStringLiteral("a"), 700, 700));
    QVERIFY(stuck.setWindowMinimumSize(QStringLiteral("b"), 700, 700));
    const ColumnWidth aBefore = stuck.columns().at(0).width;
    QVERIFY(!stuck.equalizeVisibleColumnWidths(params));
    QCOMPARE(stuck.columns().at(0).width, aBefore);
}

void TestScrollStripView::resetToDefaultsClearsThePreMaximizeSlot()
{
    // The slot only matters when the DEFAULT is itself full width: after
    // the reset the column IS full, so a stale slot for it would hand the
    // pre-maximize width back to the next toggle instead of the half-width
    // fallback the toggle uses when it has no stored intent. With any other
    // default the toggle overwrites the slot before reading it, which is why
    // resetToDefaultsRestoresEveryIntent cannot observe the clear.
    ScrollLayoutParams params = defaultParams();
    params.defaultColumnWidth = ColumnWidth::makeProportion(1.0);
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleMaximizeActiveColumn(params)); // slot = 0.3
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeProportion(1.0));
    // Nothing but the slot is off its default, so the reset reports no
    // change, and still clears the slot.
    QVERIFY(!strip.resetToDefaults(params.defaultColumnWidth, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(0).width, ColumnWidth::makeProportion(0.5));

    // Under "the client decides" (no default width) the reset leaves widths
    // alone, so a maximized column STAYS maximized and its slot must survive
    // with it: the next un-maximize hands back the width the user had, not
    // the half-width fallback.
    ScrollStrip clientSized;
    QVERIFY(
        clientSized.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.3), ColumnDisplay::Normal, params));
    QVERIFY(clientSized.toggleMaximizeActiveColumn(params)); // slot = 0.3
    QVERIFY(!clientSized.resetToDefaults(std::nullopt, WindowHeight::makeAuto(), ColumnDisplay::Normal, params));
    QCOMPARE(clientSized.columns().at(0).width, ColumnWidth::makeProportion(1.0));
    QVERIFY(clientSized.toggleMaximizeActiveColumn(params));
    QCOMPARE(clientSized.columns().at(0).width, ColumnWidth::makeProportion(0.3));
}

void TestScrollStripView::expandReclaimsAStraddlersPixels()
{
    // niri's accounting for expand-column-to-available-width: only the
    // columns lying ENTIRELY in the viewport count as taking space. A
    // straddler's on-screen pixels are reclaimable, because the expansion is
    // what pushes it out of view.
    //
    // Three 500px columns (strip positions 0, 510, 1020) with the LAST
    // focused puts the view at offset 320, which clips a at the lead edge and
    // leaves b and c fully visible. Measuring the strip's COVERED interval
    // instead — the form this replaced — counts a's 180 on-screen pixels and
    // answers "nothing left over", so the verb refused.
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b", "c"}) {
        QVERIFY(
            strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeFixed(500), ColumnDisplay::Normal, params));
    }
    QVERIFY(strip.focusColumn(1, params));
    const ResolvedStrip before = strip.relayout(params);
    QVERIFY2(Ax::mainPos(rectOf(before, QStringLiteral("a"))) < 0, "fixture: a must straddle the lead edge");
    QCOMPARE(strip.fullyVisibleColumnIndices(params), QVector<int>({1, 2}));

    // Taken = 500 + 500 + one gap between them = 1010, so 190 is reclaimable
    // and b grows to 690.
    QVERIFY(strip.expandActiveColumnToAvailableWidth(params));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("b"))), 690);
    QCOMPARE(strip.columns().at(2).width, ColumnWidth::makeFixed(500)); // c untouched
}

void TestScrollStripView::expandMaximizesWhenTheActiveColumnIsAloneOnScreen()
{
    // Two 700px columns (strip positions 0 and 710) with the last focused:
    // the view ends at offset 210, so a is clipped at the lead edge and b is
    // the ONLY fully visible column. b is therefore about to take the whole
    // viewport, which niri routes through the maximize toggle rather than a
    // width write "as it lets you back out of it more intuitively".
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b"}) {
        QVERIFY(
            strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeFixed(700), ColumnDisplay::Normal, params));
    }
    QCOMPARE(strip.fullyVisibleColumnIndices(params), QVector<int>({1}));

    QVERIFY(strip.expandActiveColumnToAvailableWidth(params));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeProportion(1.0));
    QCOMPARE(Ax::mainLen(rectOf(strip.relayout(params), QStringLiteral("b"))), ScrollTestUtils::kMainExtent);
    // The toggle stored the pre-expand intent, so there IS a way back out —
    // the behaviour a bare Fixed(viewport) write destroyed.
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeFixed(700));

    // A column already filling the viewport has nowhere to expand, and must
    // NOT fall into the toggle's un-maximize arm: this verb only ever grows.
    QVERIFY(strip.setActiveColumnWidth(ColumnWidth::makeProportion(1.0)));
    QVERIFY(!strip.expandActiveColumnToAvailableWidth(params));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeProportion(1.0));
}

void TestScrollStripView::expandRefusesForAStraddlingActiveColumn()
{
    // The leftover is measured against a viewport position the active column
    // does not fully occupy, so there is no meaningful answer: refuse rather
    // than commit a width computed from a partial view (niri's
    // `active_col_x.is_none()` bail).
    const auto params = defaultParams();
    ScrollStrip strip;
    for (const char* id : {"a", "b"}) {
        QVERIFY(
            strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeFixed(700), ColumnDisplay::Normal, params));
    }
    // b sits at the trailing edge; a pan toward the strip's start clips it.
    QVERIFY(strip.scrollViewBy(-30, params));
    const ResolvedStrip before = strip.relayout(params);
    QVERIFY2(Ax::mainPos(rectOf(before, QStringLiteral("b"))) + Ax::mainLen(rectOf(before, QStringLiteral("b")))
                 > ScrollTestUtils::kMainExtent,
             "fixture: the active column must straddle the trailing edge");
    const int anchorBefore = strip.viewAnchor();

    QVERIFY(!strip.expandActiveColumnToAvailableWidth(params));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeFixed(700));
    QCOMPARE(strip.viewAnchor(), anchorBefore);
}

void TestScrollStripView::expandMaximizesUnderACenteringPolicy()
{
    // Always-centering pins the active column to the middle of the viewport,
    // so its position after the resize is not the strip's to choose and
    // "fill what is left" has no stable answer. niri takes the simple way out
    // and maximizes; so do we, and the toggle keeps it reversible.
    auto params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip strip;
    for (const char* id : {"a", "b"}) {
        QVERIFY(
            strip.insertWindow(QString::fromLatin1(id), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    }

    QVERIFY(strip.expandActiveColumnToAvailableWidth(params));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeProportion(1.0));
    QVERIFY(strip.toggleMaximizeActiveColumn(params));
    QCOMPARE(strip.columns().at(1).width, ColumnWidth::makeFixed(300));
}

void TestScrollStripView::predictedFocusScrollMatchesTheScrollAFocusActuallyCosts()
{
    // The whole contract of the focus-follows-mouse scroll cap: the prediction
    // must be the same number the focus would actually produce. It is not a
    // second estimate of the policy — it runs the policy — so this test is
    // what proves the shared helper stayed shared. A drift here is invisible
    // in use: the cap would simply refuse the wrong windows.
    //
    // All three centering policies, because each takes a different arm and the
    // fit arm is the only one a naive "how far off screen is it" guess would
    // ever agree with.
    for (const CenterFocusedColumn policy :
         {CenterFocusedColumn::Never, CenterFocusedColumn::Always, CenterFocusedColumn::OnOverflow}) {
        ScrollLayoutParams params = defaultParams();
        params.centerFocusedColumn = policy;
        const ColumnWidth wide = ColumnWidth::makeProportion(0.55);
        // Built fresh per target, because measuring the real cost means
        // actually focusing, and that leaves the view somewhere else.
        const auto build = [&](ScrollStrip& s, int startColumn) {
            QVERIFY(s.insertWindow(QStringLiteral("a"), wide, ColumnDisplay::Normal, params));
            QVERIFY(s.insertWindow(QStringLiteral("b"), wide, ColumnDisplay::Normal, params));
            QVERIFY(s.insertWindow(QStringLiteral("c"), wide, ColumnDisplay::Normal, params));
            QVERIFY(s.insertWindow(QStringLiteral("d"), wide, ColumnDisplay::Normal, params));
            // The last insert already focused the trailing column, and
            // focusColumn refuses a no-op, so only move when there is a move.
            if (s.activeColumnIndex() != startColumn) {
                QVERIFY(s.focusColumn(startColumn, params));
            }
            QCOMPARE(s.activeColumnIndex(), startColumn);
        };
        // Both directions. Two arms of the shared policy are direction-scoped
        // — OnOverflow measures from the neighbour on the side the focus is
        // arriving from, and the fit arm pins the target to the entering edge
        // — so a run that only ever moves forward exercises one side of each
        // and would pass with the sign wrong on the other.
        for (const int start : {0, 3}) {
            for (int target = 0; target < 4; ++target) {
                ScrollStrip strip;
                build(strip, start);
                const int before = strip.relayout(params).viewOffset;
                const int predicted = strip.predictedFocusScrollPx(target, params);
                if (target == start) {
                    // The column build() left focused. focusColumn refuses a
                    // no-op, so there is no motion to measure — and the
                    // prediction for it must be exactly that.
                    QCOMPARE(predicted, 0);
                    continue;
                }
                QVERIFY(strip.focusColumn(target, params));
                const int actual = qAbs(strip.relayout(params).viewOffset - before);
                QCOMPARE(predicted, actual);
            }
        }
    }
}

void TestScrollStripView::predictedFocusScrollIsZeroForEveryFailOpenInput()
{
    // Zero is the fail-open answer, and four separate inputs must give it: the
    // active column (focusing it moves nothing), an index off either end of the
    // strip, a strip with no active column, and a degenerate work area. A
    // caller uses this to REFUSE a focus, so a question it cannot answer must
    // never come back as a refusal.
    ScrollLayoutParams params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // The second insert already focused column 1; move away and back so the
    // fixture states the focus rather than inheriting it.
    QVERIFY(strip.focusColumn(0, params));
    QVERIFY(strip.focusColumn(1, params));

    QCOMPARE(strip.predictedFocusScrollPx(1, params), 0);
    QCOMPARE(strip.predictedFocusScrollPx(-1, params), 0);
    QCOMPARE(strip.predictedFocusScrollPx(2, params), 0);

    ScrollLayoutParams degenerate = params;
    degenerate.workArea = QRect();
    QCOMPARE(strip.predictedFocusScrollPx(0, degenerate), 0);

    // No active column to move away FROM. An empty strip has no column to name
    // either, so the question is unanswerable from both ends at once.
    ScrollStrip unfocused;
    QCOMPARE(unfocused.activeColumnIndex(), -1);
    QCOMPARE(unfocused.predictedFocusScrollPx(0, params), 0);
}

QTEST_APPLESS_MAIN(TestScrollStripView)
#include "test_scrollstrip_view.moc"
