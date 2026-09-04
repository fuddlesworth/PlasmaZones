// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The maximize-to-edges flag: what a flagged column RESOLVES to, and what
// clears it again.
//
// Split out of test_scrollstrip_sizing once that file passed the size ceiling.
// The two are the same shape (every slot builds its own strip against the
// shared screen constants and asserts geometry as well as the verb's bool);
// this one is the maximize concern alone.
//
// The contract these slots pin, in one place because each half is easy to
// break without the other: while the flag is set the column resolves against
// the RAW work area on both axes, and every tile in it is resolved as an Auto
// weighted share of that extent — its stored Fixed/Preset height intent is
// overridden, not consulted. The intents themselves are untouched, so clearing
// the flag re-renders them exactly. A column that honoured its stored height
// while flagged went full width at a short height, and the centre-short-columns
// policy then floated it mid-screen while the compositor's maximize state said
// full.

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

class TestScrollStripMaximize : public QObject
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
    void maximizeToEdgesResolvesTheRawAreaGapFree();
    void maximizeToEdgesOverridesAnExplicitHeightIntent();
    void maximizeToEdgesRestoresAStackedIntentWhenCleared();
    void maximizeToEdgesSharesTheRawExtentByWeight();
    void maximizeToEdgesStillHonoursTheClientMinimum();
    void scrollingPastAMaximizedColumnMovesItByExactlyTheViewDelta();
    void maximizeToEdgesRestoreIsJustTheStoredIntentAgain();
    void widthAndHeightVerbsClearMaximizeToEdges();
    void equalizeClearsTheActiveColumnsMaximizeToEdges();
    void maximizeToEdgesIgnoresCenteringAndRestoresTheHeightIntent();
    void maximizeToEdgesLandsOnTheRawAreaUnderMainAxisCentering();
    void maximizeToEdgesLandsOnTheRawAreaWhenFocusArrivesFromTheTrailingSide();
};

// The geometry contract of the maximize-to-edges flag: the column resolves
// against the RAW work area (pre outer gaps) on BOTH axes, its stacked tiles
// divide the raw cross extent with no inner gap, and a sibling column keeps
// its ordinary gapped resolution.
void TestScrollStripMaximize::maximizeToEdgesResolvesTheRawAreaGapFree()
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
    // rect is shifted low by the outer gap, so a column at the anchor lands on
    // the raw area exactly.
    QCOMPARE(a.united(b), params.rawWorkArea);
}

// Maximize-to-edges must override an explicit Fixed/Preset height intent the
// same way the tabbed branch overrides its owner's intent. Before the fix a
// lone tile kept its short height while the column's main extent (and KWin's
// maximize state) went full, and with the centre-short-columns policy on the
// window floated mid-screen — the live "maximize only widens" report. The
// intent itself must survive for the restore.
void TestScrollStripMaximize::maximizeToEdgesOverridesAnExplicitHeightIntent()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);
    params.centerShortColumns = true;

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")), params.rawWorkArea);

    // A two-tile stack still partitions the raw cross extent when one tile
    // carries an explicit height. Built fresh so the insert cannot disturb
    // the flag under test.
    ScrollStrip stacked;
    QVERIFY(stacked.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(stacked.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(stacked.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    QVERIFY(stacked.toggleMaximizeToEdgesActiveColumn(params));
    {
        const ResolvedStrip resolved = stacked.relayout(params);
        const QRect a = rectOf(resolved, QStringLiteral("a"));
        const QRect b = rectOf(resolved, QStringLiteral("b"));
        QCOMPARE(Ax::crossLen(a) + Ax::crossLen(b), Ax::crossLen(params.rawWorkArea));
        QCOMPARE(a.united(b), params.rawWorkArea);
    }
}

// The other half of the override contract, for a STACK: clearing the flag has
// to hand every tile its stored intent back. The lone-tile restore is covered
// by the seam test at the end of this file; without this one, a stacked
// override that quietly rewrote the stored intents instead of shadowing them
// would still pass everything above, and the damage would only show when the
// user unmaximized.
void TestScrollStripMaximize::maximizeToEdgesRestoresAStackedIntentWhenCleared()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    const int fixedBefore = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));
    QCOMPARE(fixedBefore, 300);

    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))) != 300);

    // Toggling back restores the stored 300, and the sibling goes back to
    // filling what is left rather than keeping its override share.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    const ResolvedStrip restored = strip.relayout(params);
    QCOMPARE(Ax::crossLen(rectOf(restored, QStringLiteral("b"))), 300);
    QCOMPARE(Ax::crossLen(rectOf(restored, QStringLiteral("a"))) + 300 + params.gap, Ax::crossLen(params.workArea));
}

// Under the override every tile resolves as an Auto share, and Auto shares are
// WEIGHTED. A Fixed tile's weight therefore starts mattering the moment the
// flag goes on, where before the flag it was never consulted for that tile.
// Pinned because it is the kind of contract that gets silently changed to an
// equal split: with equal weights the two are indistinguishable.
void TestScrollStripMaximize::maximizeToEdgesSharesTheRawExtentByWeight()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // b carries twice a's weight. Its intent kind is irrelevant under the
    // override; the weight is what survives into the share.
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeAuto(2.0)));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));

    const ResolvedStrip resolved = strip.relayout(params);
    const int a = Ax::crossLen(rectOf(resolved, QStringLiteral("a")));
    const int b = Ax::crossLen(rectOf(resolved, QStringLiteral("b")));
    QCOMPARE(a + b, Ax::crossLen(params.rawWorkArea));
    // 1:2, to the rounding the integer split allows.
    QVERIFY2(qAbs(b - 2 * a) <= 2, "a weighted Auto share must survive the maximize-to-edges override");
}

// The override resolves every tile as an Auto share, but it does not get to
// resolve a tile smaller than its client's minimum: that floor is the client's
// to set, not the layout's. The rebalance that honours it must still leave the
// stack inside the raw extent rather than overflowing it.
void TestScrollStripMaximize::maximizeToEdgesStillHonoursTheClientMinimum()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // Well above b's equal share of the raw cross extent, so the floor has to
    // bind and a's share has to give way.
    const int floorPx = Ax::crossLen(params.rawWorkArea) * 3 / 4;
    // Transposed through Ax so the vertical arm asks for a minimum on the same
    // axis the assertions read.
    const QSize minSize = Ax::t(QSize(10, floorPx));
    strip.setWindowMinimumSize(QStringLiteral("b"), minSize.width(), minSize.height());
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));

    const ResolvedStrip resolved = strip.relayout(params);
    const QRect a = rectOf(resolved, QStringLiteral("a"));
    const QRect b = rectOf(resolved, QStringLiteral("b"));
    QVERIFY2(Ax::crossLen(b) >= floorPx, "the client minimum must survive the maximize-to-edges override");
    QVERIFY2(Ax::crossLen(a) + Ax::crossLen(b) <= Ax::crossLen(params.rawWorkArea),
             "the floored stack must not overflow the raw extent it is dividing");
}

// The maximize-to-edges rect is shifted low by the outer gap on EVERY frame,
// not only while the column covers the viewport. The batch's viewDelta is
// differenced from the view coordinate alone, so a shift that appears and
// disappears with coverage is motion the effect is never told about: it
// differences the window's live rect against viewDelta, finds an outer gap of
// residual, and runs a second per-window spring for it beside the one-spring-
// per-output view slide. On a column the size of the screen that shows up as
// two animations playing at once. The invariant that forbids it is here: a
// maximized column's main position moves by exactly the view's own travel.
void TestScrollStripMaximize::scrollingPastAMaximizedColumnMovesItByExactlyTheViewDelta()
{
    ScrollLayoutParams params = defaultParams();
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -20, -20);

    // Four columns so the strip is longer than the viewport and a focus
    // change actually moves the view — with a strip that fits, the anchor
    // policy re-centres and the view offset never leaves zero, which would
    // make the comparison below vacuous.
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("d"), kHalf, ColumnDisplay::Normal, params));
    // "d" is the active column: maximize it, then focus its neighbour so the
    // maximized column leaves the viewport it was covering.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    // Re-anchor onto the now-maximized column so it starts out COVERING the
    // viewport. That is the state the old gate treated specially, and a
    // fixture that never enters it cannot tell the two behaviours apart.
    QVERIFY(strip.focusAdjacentColumn(-1, params));
    QVERIFY(strip.focusAdjacentColumn(1, params));

    const ResolvedStrip covering = strip.relayout(params);
    QCOMPARE(Ax::mainPos(rectOf(covering, QStringLiteral("d"))), Ax::mainPos(params.rawWorkArea));
    // Premise: the flag really took, so the column below is the wide one and
    // not an ordinary column that would ride the view either way.
    QCOMPARE(Ax::mainLen(rectOf(covering, QStringLiteral("d"))), Ax::mainLen(params.rawWorkArea));

    // Move the view itself rather than leaning on the anchor policy: what is
    // under test is that the maximized column tracks the view exactly, and a
    // direct scroll states the travel without the fixture having to be shaped
    // so a focus change happens to produce one.
    QVERIFY(strip.scrollViewBy(-300, params));
    const ResolvedStrip scrolled = strip.relayout(params);

    const int viewTravel = covering.viewOffset - scrolled.viewOffset;
    QVERIFY2(viewTravel != 0, "the fixture must actually scroll the view");
    QCOMPARE(Ax::mainPos(rectOf(scrolled, QStringLiteral("d"))) - Ax::mainPos(rectOf(covering, QStringLiteral("d"))),
             viewTravel);
    // The un-maximized neighbour is the control: it rides the same view, and
    // the maximized column must not be special.
    QCOMPARE(Ax::mainPos(rectOf(scrolled, QStringLiteral("c"))) - Ax::mainPos(rectOf(covering, QStringLiteral("c"))),
             viewTravel);
}

// Un-maximizing is "stop overriding": the stored width intent was never
// touched, so one toggle out re-renders exactly the pre-toggle rects with no
// pre-maximize slot involved.
void TestScrollStripMaximize::maximizeToEdgesRestoreIsJustTheStoredIntentAgain()
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
void TestScrollStripMaximize::widthAndHeightVerbsClearMaximizeToEdges()
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
    QVERIFY2(strip.equalizeActiveColumnHeights(), "the height reset must report the drop");
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
void TestScrollStripMaximize::equalizeClearsTheActiveColumnsMaximizeToEdges()
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

// The centre-short-columns policy, whose whole surface is the cross-axis
// ORIGIN of a column that resolved shorter than the work area. The four slots
// below pin both verdicts (off is the historical top-hugging layout, on is
// the centred one), the case that must be identical under either (a column
// that fills), and that the measurement is the stack rather than one tile.
// Each asserts the cross EXTENT alongside the position: a policy that
// accidentally resized instead of moving would satisfy a position-only
// assertion.

// The seam between the two features: with the flag overriding every height
// intent there is no short stack inside a maximized column any more, so the
// centre policy has no slack to act on and the tile lands on the raw rect
// exactly. This test used to assert the opposite (the 300px intent honoured
// and centred in the raw slack), which was the live "maximize only widens"
// bug: full main extent and compositor maximize state beside a short centred
// window. The asymmetric-gap fixture is kept so any regression back to
// intent-honouring also re-exposes the raw-vs-gapped centring question it
// originally probed.
void TestScrollStripMaximize::maximizeToEdgesIgnoresCenteringAndRestoresTheHeightIntent()
{
    ScrollLayoutParams params = defaultParams();
    params.centerShortColumns = true;
    // Outer gaps, the whole point of the case: rawWorkArea is the pre-gap
    // rect, workArea the inset one the ordinary columns resolve against.
    //
    // ASYMMETRIC on purpose. Under a symmetric inset the two candidate answers
    // are arithmetically identical — the low edge moves in by g while the
    // extent loses 2g, and centring halves that back to g — so a symmetric
    // fixture cannot tell "centred in the raw area" from "centred in the
    // gapped one" and the slot would pass either way.
    params.rawWorkArea = params.workArea;
    params.workArea = params.workArea.adjusted(20, 20, -60, -60);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.setActiveWindowHeight(WindowHeight::makeFixed(300)));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));

    const QRect r = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(r, params.rawWorkArea);
    // And the 300px intent survives the round trip untouched.
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("a"))), 300);
}

// The MAIN-axis twin of the slot above, and the live "maximize with centering
// on is still broken" report. The cross-axis centre policy above is
// centerShortColumns; this one is centerFocusedColumn, which places the column
// by moving the VIEW rather than by moving the column within its area.
//
// The two halves of the maximized position have to agree on which area the
// column is centred in. relayout already shifts a flagged column low by the
// outer gap so an anchor of "sits at the strip cursor" lands it on the raw
// area, so the anchor policy must centre it in the RAW extent too. Centring it
// in the gapped extent instead spent the gap a second time and the column
// resolved a full outer gap off the low edge of the output, which is what the
// user saw: a maximized window hanging past the screen edge.
//
// ASYMMETRIC main-axis inset, for the reason the slot above gives: under a
// symmetric one the two candidate answers are arithmetically identical and the
// fixture could not tell them apart.
void TestScrollStripMaximize::maximizeToEdgesLandsOnTheRawAreaUnderMainAxisCentering()
{
    ScrollLayoutParams params = defaultParams();
    params.centerFocusedColumn = CenterFocusedColumn::Always;
    params.rawWorkArea = params.workArea;
    // MAIN-axis inset only: the cross axis is the sibling slot's concern, and
    // leaving it alone keeps a cross-axis regression from being reported here.
    params.workArea =
        Ax::vertical() ? params.rawWorkArea.adjusted(0, 20, 0, -60) : params.rawWorkArea.adjusted(20, 0, -60, 0);

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    // The policy owns the view, so re-apply it the way applyLayout does before
    // reading the geometry back.
    strip.updateViewForFocus(params);

    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")), params.rawWorkArea);
}

// The same raw-versus-gapped question with the centring policy OFF, which is
// where it survived the fix above. A maximized column's extent is the RAW main
// extent, so it is strictly wider than the gapped viewport and the pin arm's
// over-wide branch owns it. That branch pins to a viewport edge measured in
// GAPPED coordinates, and it has two directions. Arriving from the leading
// side asks for a zero offset, which the clamp cannot narrow for a column this
// wide, so that direction lands correctly. Arriving from the TRAILING side
// asks for viewMain - colMain, which put the column its two main-axis insets
// together off the low edge of the output.
//
// Focus therefore has to arrive from the trailing side here, and the inset is
// asymmetric for this file's usual reason: under a symmetric one the two
// candidate answers coincide.
void TestScrollStripMaximize::maximizeToEdgesLandsOnTheRawAreaWhenFocusArrivesFromTheTrailingSide()
{
    ScrollLayoutParams params = defaultParams();
    // The policy that does NOT move the view for the focused column, so the
    // pin arm rather than the centring arm decides the anchor.
    params.centerFocusedColumn = CenterFocusedColumn::Never;
    params.rawWorkArea = params.workArea;
    params.workArea =
        Ax::vertical() ? params.rawWorkArea.adjusted(0, 20, 0, -60) : params.rawWorkArea.adjusted(20, 0, -60, 0);

    // Two columns so focus can arrive at the maximized one from its trailing
    // neighbour. "a" is inserted first and so sits leadward of "b".
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // Maximize "a", then leave it and come back from the trailing side.
    QVERIFY(strip.focusAdjacentColumn(-1, params));
    QVERIFY(strip.toggleMaximizeToEdgesActiveColumn(params));
    QVERIFY(strip.focusAdjacentColumn(1, params));
    QVERIFY(strip.focusAdjacentColumn(-1, params));

    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("a")), params.rawWorkArea);
}

QTEST_APPLESS_MAIN(TestScrollStripMaximize)
#include "test_scrollstrip_maximize.moc"
