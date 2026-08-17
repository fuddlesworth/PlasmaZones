// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::defaultParams;
using ScrollTestUtils::kHalf;
using ScrollTestUtils::rectOf;

} // namespace

class TestScrollStripCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite
    /// under a name claiming otherwise.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    /// THE ALGEBRAIC ANCHOR for the whole transposition effort, and the one
    /// test here that does not go through the Ax harness at all: it builds
    /// both ScrollLayoutParams by hand and runs BOTH axes in the SAME process,
    /// so it verifies the ENGINE's mapping rather than agreeing with the
    /// harness's copy of it.
    ///
    /// Property: relayout on a transposed strip is the exact transpose of
    /// relayout on the original, tile for tile.
    ///
    /// Deliberately NOT gated by the suite guard — it runs identically on both
    /// arms, because both axes are constructed locally rather than read from
    /// the environment.
    void relayoutUnderAVerticalAxisIsTheExactTranspose()
    {
        // Mixed intents on purpose: a Proportion column, a Fixed one, a Preset
        // one carrying a client minimum, and a stack holding an Auto/Fixed
        // split. Each resolves through a different arm of the sizing code, so
        // a missed axis read in any single arm breaks this.
        //
        // The client minimum is TRANSPOSED with the rest of the fixture, and it
        // has to be: minWidth/minHeight are physical screen-space facts about
        // the window, so passing the same pair to both builds makes them not
        // transposes of each other, and this test's whole claim is that they
        // are. It is also raised above the preset column's resolved main
        // extent (595px), so the min-size clamp genuinely BITES on both arms.
        // At the old (250, 0) the minimum was inert against a 595px column and
        // the arm resolving it was never exercised at all.
        const auto build = [](const ScrollLayoutParams& p, const QSize& clientMin) {
            ScrollStrip s;
            s.insertWindow(QStringLiteral("a"), ColumnWidth::makeProportion(0.5), ColumnDisplay::Normal, p);
            s.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, p);
            s.insertWindow(QStringLiteral("c"), ColumnWidth::makePreset(0.5), ColumnDisplay::Normal, p,
                           clientMin.width(), clientMin.height());
            s.focusColumn(2, p);
            s.insertWindowIntoActiveColumn(QStringLiteral("d"), kHalf, std::nullopt, p);
            s.setWindowHeightIntent(QStringLiteral("d"), WindowHeight::makeFixed(220));
            s.focusColumn(1, p);
            return s;
        };

        /// Above the 595px the preset resolves to, so the clamp has to run.
        constexpr int kBitingMainMinimum = 700;

        ScrollLayoutParams h;
        h.workArea = QRect(0, 0, 1200, 800);
        h.gap = 10;
        h.axis = StripAxis::horizontal();

        ScrollLayoutParams v = h;
        v.workArea = QRect(0, 0, 800, 1200); // T(workArea)
        v.axis = StripAxis::vertical();

        ScrollStrip hs = build(h, QSize(kBitingMainMinimum, 0));
        ScrollStrip vs = build(v, QSize(0, kBitingMainMinimum)); // T(clientMin)
        const ResolvedStrip hr = hs.relayout(h);
        const ResolvedStrip vr = vs.relayout(v);

        QCOMPARE(vr.stripExtent, hr.stripExtent);
        QCOMPARE(vr.viewOffset, hr.viewOffset);
        QCOMPARE(vr.columns.size(), hr.columns.size());

        // The premise of the biting minimum, pinned so the fixture cannot
        // drift back into an inert one: c's Preset(0.5) asks for 595px, and
        // its column resolved WIDER than that because the client minimum
        // overrode it. Both are literals, never the code under test's own
        // answer used as its own expectation.
        QCOMPARE(hr.columns.size(), 3);
        QCOMPARE(hr.columns.at(2).tiles.first().windowId, QStringLiteral("c"));
        QVERIFY2(hr.columns.at(2).rect.width() >= kBitingMainMinimum,
                 qPrintable(QStringLiteral("c's column must be widened by its %1px minimum past the 595px its preset "
                                           "asks for, got %2")
                                .arg(kBitingMainMinimum)
                                .arg(hr.columns.at(2).rect.width())));

        const auto transposed = [](const QRect& r) {
            return QRect(r.y(), r.x(), r.height(), r.width());
        };

        for (int ci = 0; ci < hr.columns.size(); ++ci) {
            const ResolvedColumn& hc = hr.columns.at(ci);
            const ResolvedColumn& vc = vr.columns.at(ci);
            QCOMPARE(vc.columnIndex, hc.columnIndex);
            QCOMPARE(vc.tiles.size(), hc.tiles.size());
            QCOMPARE(vc.rect, transposed(hc.rect));
            for (int ti = 0; ti < hc.tiles.size(); ++ti) {
                const ResolvedTile& ht = hc.tiles.at(ti);
                const ResolvedTile& vt = vc.tiles.at(ti);
                QCOMPARE(vt.windowId, ht.windowId);
                QCOMPARE(vt.hidden, ht.hidden);
                QVERIFY2(vt.rect == transposed(ht.rect),
                         qPrintable(QStringLiteral("tile %1: horizontal (%2,%3 %4x%5) should transpose to "
                                                   "(%6,%7 %8x%9) but got (%10,%11 %12x%13)")
                                        .arg(ht.windowId)
                                        .arg(ht.rect.x())
                                        .arg(ht.rect.y())
                                        .arg(ht.rect.width())
                                        .arg(ht.rect.height())
                                        .arg(transposed(ht.rect).x())
                                        .arg(transposed(ht.rect).y())
                                        .arg(transposed(ht.rect).width())
                                        .arg(transposed(ht.rect).height())
                                        .arg(vt.rect.x())
                                        .arg(vt.rect.y())
                                        .arg(vt.rect.width())
                                        .arg(vt.rect.height())));
            }
        }
    }

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
    void viewAnchorSurvivesLeadInsert();
    void tabIndicatorResolvesOnlyForTabbedColumns();
    void tabIndicatorHidesForASingleTab();
    void tabIndicatorSitsOutsideTheColumnByDefault();
    void tabIndicatorNegativeGapDrawsOverTheWindow();
    void tabIndicatorWithinColumnShrinksTheTiles();
    void tabIndicatorLengthIsCenteredOnTheEdge();
    void tabIndicatorGapKeepsMovingWithinColumn();
    void tabIndicatorRightAndBottomAnchorTheOppositeEdge();
    void tabIndicatorNarrowerThanItsReservationKeepsTheColumn();
    void tabReservationRaisesTheMinExtentFloorOnTheMainAxisOnly();
    void windowedFullscreenFlagsActiveTileOnly();
    void windowedFullscreenTravelsWithConsume();
};

void TestScrollStripCore::openInsertsColumnAndResizesNothing()
{
    ScrollStrip strip;
    const auto params = defaultParams();

    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    QCOMPARE(strip.columnCount(), 1);
    const QRect aBefore = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(aBefore, Ax::t(QRect(0, 0, 400, 800)));

    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.columnCount(), 2);
    QCOMPARE(strip.activeColumnIndex(), 1);
    QCOMPARE(strip.activeWindowId(), QStringLiteral("b"));

    const ResolvedStrip after = strip.relayout(params);
    // The §0 invariant: a's geometry is untouched — same size, same position.
    QCOMPARE(rectOf(after, QStringLiteral("a")), aBefore);
    QCOMPARE(Ax::mainPos(rectOf(after, QStringLiteral("b"))), 410);
    // 595, the literal, NOT resolveColumnWidthPx: computing the expectation
    // with the code under test makes the assertion agree with any answer that
    // function gives, which is the antipattern test_scrollstrip_ops calls out.
    QCOMPARE(Ax::mainLen(rectOf(after, QStringLiteral("b"))), 595);
}

void TestScrollStripCore::openScrollsOnlyWhenNeeded()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    // The literal a half-width column resolves to on the 1200px main extent at
    // gap 10, spelled out rather than fetched from resolveColumnWidthPx: an
    // expectation computed by the code under test cannot disagree with it.
    const int halfPx = 595;

    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // a and b both fit: no scroll happened.
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.viewOffset, 0);
    QCOMPARE(Ax::mainPos(rectOf(r, QStringLiteral("a"))), 0);
    QCOMPARE(Ax::mainPos(rectOf(r, QStringLiteral("b"))), halfPx + params.gap);

    // c does not fit — the view scrolls the minimum amount to show it, and
    // NOBODY changes size.
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));
    r = strip.relayout(params);
    QVERIFY(r.viewOffset > 0);
    QCOMPARE(Ax::mainLen(rectOf(r, QStringLiteral("c"))), halfPx);
    QCOMPARE(Ax::mainLen(rectOf(r, QStringLiteral("a"))), halfPx);
    // c's trailing edge is pinned to the viewport's trailing edge (minimal
    // scroll).
    QCOMPARE(Ax::mainEnd(rectOf(r, QStringLiteral("c"))) + 1, Ax::mainLen(params.workArea));
}

void TestScrollStripCore::closeKeepsNeighboursAnchored()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    const QRect bBefore = rectOf(strip.relayout(params), QStringLiteral("b"));
    // Closing the focused trail-most column: focus falls to b, which must not
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

    // Focus the middle column and close it: the TRAIL neighbour takes focus.
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
    // Focused d (trail-most). Focus a: it enters from the LEAD end, so its
    // lead edge pins to the viewport's lead edge.
    QVERIFY(strip.focusFirstColumn(params));
    ResolvedStrip r = strip.relayout(params);
    QCOMPARE(Ax::mainPos(rectOf(r, QStringLiteral("a"))), 0);

    // Focus b: already fully visible — no scroll at all.
    const int viewBefore = r.viewOffset;
    QVERIFY(strip.focusAdjacentColumn(+1, params));
    r = strip.relayout(params);
    QCOMPARE(r.viewOffset, viewBefore);

    // Focus d again: enters from the TRAIL end, so its trail edge pins to the
    // viewport's trail edge.
    QVERIFY(strip.focusLastColumn(params));
    r = strip.relayout(params);
    QCOMPARE(Ax::mainEnd(rectOf(r, QStringLiteral("d"))) + 1, Ax::mainLen(params.workArea));
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
    const int centerOffset = (Ax::mainLen(params.workArea) - Ax::mainLen(b)) / 2;
    QCOMPARE(Ax::mainPos(b), centerOffset);
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
    QCOMPARE(r.viewOffset, 0);
    QCOMPARE(Ax::mainPos(rectOf(r, QStringLiteral("b"))), 310);

    // Opening a second wide column centers it: the INSERT's reanchor sees
    // prevIdx = the old column and takes the same OnOverflow branch a
    // focus change would (no explicit focus call happens here).
    ScrollStrip wide;
    QVERIFY(wide.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(700), ColumnDisplay::Normal, params));
    QVERIFY(wide.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(700), ColumnDisplay::Normal, params));
    r = wide.relayout(params);
    const QRect b = rectOf(r, QStringLiteral("b"));
    QCOMPARE(Ax::mainPos(b), (Ax::mainLen(params.workArea) - 700) / 2);
}

void TestScrollStripCore::alwaysCenterSingleColumn()
{
    ScrollStrip strip;
    auto params = defaultParams();
    params.alwaysCenterSingleColumn = true;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    const QRect a = rectOf(strip.relayout(params), QStringLiteral("a"));
    QCOMPARE(Ax::mainPos(a), (Ax::mainLen(params.workArea) - 400) / 2);

    // A second column ends the lone-column special case.
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(400), ColumnDisplay::Normal, params));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.viewOffset, 0);
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
    // Restored in the middle slot: b sits between a and c along the CROSS
    // axis, which is the direction the within-column stack divides.
    const ResolvedStrip r = strip.relayout(params);
    QVERIFY(Ax::crossPos(rectOf(r, QStringLiteral("a"))) < Ax::crossPos(rectOf(r, QStringLiteral("b"))));
    QVERIFY(Ax::crossPos(rectOf(r, QStringLiteral("b"))) < Ax::crossPos(rectOf(r, QStringLiteral("c"))));
}

void TestScrollStripCore::fullyMinimizedColumnCollapses()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));

    const int stripBefore = strip.relayout(params).stripExtent;
    QVERIFY(strip.setWindowMinimized(QStringLiteral("b"), true, params));
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(r.stripExtent, stripBefore - 300 - params.gap);
    // b's column contributes nothing; c closed up next to a.
    QCOMPARE(Ax::mainPos(rectOf(r, QStringLiteral("c"))),
             Ax::mainPos(rectOf(r, QStringLiteral("a"))) + 300 + params.gap);
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

void TestScrollStripCore::viewAnchorSurvivesLeadInsert()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("c"), kHalf, ColumnDisplay::Normal, params));

    // Restore-insert a column at index 0 (LEAD of everything): the focused
    // column c must not move on screen.
    const QRect cBefore = rectOf(strip.relayout(params), QStringLiteral("c"));
    QVERIFY(strip.insertWindowAt(0, QStringLiteral("z"), kHalf, ColumnDisplay::Normal, params));
    QCOMPARE(strip.activeWindowId(), QStringLiteral("c"));
    QCOMPARE(rectOf(strip.relayout(params), QStringLiteral("c")), cBefore);
}

// ── tab indicator ───────────────────────────────────────────────────────────
// The indicator's geometry is resolved by the relayout, not by the overlay, so
// these assert against the ResolvedColumn the strip hands back. The pixel
// numbers all derive from the shared 1200x800 / 10px-gap fixture, stated in
// ROLE terms because the fixture transposes: a half-width column takes 595 px
// ALONG the strip (1200 halved, minus half the gap) and the full 800 px
// ACROSS it. The assertions below stay in PHYSICAL vocabulary on purpose —
// TabIndicatorPosition is a screen-edge vocabulary by design, so a Left
// indicator means the left of the user's screen on either axis.

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

/// The build-really-happened companion for the NEGATIVE isNull rows: a failed
/// build hands back a default ResolvedColumn whose tabIndicatorRect is ALSO
/// null, so an isNull assertion alone is vacuous — pairing it with the
/// column's own rect validity is what makes "no indicator" mean "a real
/// column resolved without one".
bool builtWithoutIndicator(const ResolvedColumn& column)
{
    return column.rect.isValid() && column.tabIndicatorRect.isNull();
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
    QVERIFY(builtWithoutIndicator(tabbedColumn(off, 2, indicator)));
}

void TestScrollStripCore::tabIndicatorHidesForASingleTab()
{
    TabIndicatorParams indicator;
    indicator.enabled = true;
    indicator.hideWhenSingleTab = true;

    ScrollStrip single;
    QVERIFY(builtWithoutIndicator(tabbedColumn(single, 1, indicator)));

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
    // Derived from the resolved column's PHYSICAL width on purpose, not
    // pinned to a literal like the rest of this block: a Top indicator's
    // length runs along the screen's x, which is the column's 595px main
    // extent on the horizontal arm and its 800px cross extent on the
    // vertical one. A literal would pin one arm and fail the other, and the
    // property under test is the even trim, not the number.
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

void TestScrollStripCore::tabIndicatorRightAndBottomAnchorTheOppositeEdge()
{
    // Right and Bottom are the two branches whose arithmetic is most
    // off-by-one prone (`x + width - thickness + outward`), and Bottom is
    // additionally the fall-through-after-break path, so a mis-edit there is
    // silent. Every other indicator case drives Left or Top only.
    TabIndicatorParams indicator;
    indicator.placeWithinColumn = true;
    indicator.gap = 5;
    indicator.width = 10;
    const int reserved = 15; // width + gap

    indicator.position = TabIndicatorPosition::Right;
    ScrollStrip rightStrip;
    const ResolvedColumn right = tabbedColumn(rightStrip, 2, indicator);
    // Flush with the column's RIGHT edge, and the tiles keep the left edge.
    QCOMPARE(right.tabIndicatorRect.right(), right.rect.right());
    QCOMPARE(right.tabIndicatorRect.width(), 10);
    QCOMPARE(right.tiles.first().rect.x(), right.rect.x());
    QCOMPARE(right.tiles.first().rect.width(), right.rect.width() - reserved);

    indicator.position = TabIndicatorPosition::Bottom;
    ScrollStrip bottomStrip;
    const ResolvedColumn bottom = tabbedColumn(bottomStrip, 2, indicator);
    QCOMPARE(bottom.tabIndicatorRect.bottom(), bottom.rect.bottom());
    QCOMPARE(bottom.tabIndicatorRect.height(), 10);
    QCOMPARE(bottom.tiles.first().rect.y(), bottom.rect.y());
    QCOMPARE(bottom.tiles.first().rect.height(), bottom.rect.height() - reserved);

    // Outside the column, both push CLEAR of the far edge by the gap rather
    // than inward — the sign flip Left/Top would hide.
    indicator.placeWithinColumn = false;
    indicator.position = TabIndicatorPosition::Right;
    ScrollStrip outsideStrip;
    const ResolvedColumn outside = tabbedColumn(outsideStrip, 2, indicator);
    QCOMPARE(outside.tabIndicatorRect.x(), outside.rect.right() + 1 + indicator.gap);
    QCOMPARE(outside.tiles.first().rect, outside.rect);
}

void TestScrollStripCore::tabIndicatorNarrowerThanItsReservationKeepsTheColumn()
{
    // A column smaller than its own reservation must NOT resolve an inverted
    // tile rect. contentRectFor hands back the untouched column instead, and
    // the indicator simply overlaps it — the same thing a negative gap does.
    // Deleting that fallback leaves every other case green.
    TabIndicatorParams indicator;
    indicator.position = TabIndicatorPosition::Left;
    indicator.placeWithinColumn = true;
    indicator.gap = 0;
    indicator.width = 4000; // far wider than any column the fixture can make

    ScrollStrip strip;
    const ResolvedColumn column = tabbedColumn(strip, 2, indicator);
    QVERIFY(column.rect.isValid());
    for (const ResolvedTile& tile : column.tiles) {
        QVERIFY2(tile.rect.isValid(), "an over-large reservation must not invert the tile rect");
        QCOMPARE(tile.rect, column.rect);
    }
}

void TestScrollStripCore::tabReservationRaisesTheMinExtentFloorOnTheMainAxisOnly()
{
    // The respectMinimumSize floor in columnExtentPx grows by the indicator's
    // reservation ONLY when the reservation eats the strip's MAIN axis: a
    // Left/Right indicator is thick along x, which is the main axis on a
    // horizontal strip and the cross axis on a vertical one. The gate is an
    // EQUALITY between the indicator's orientation and the axis for exactly
    // that reason, and this case fails if it flips either way — the
    // horizontal arm expects the widened column, the vertical arm the bare
    // minimum.
    ScrollLayoutParams params = defaultParams();
    params.tabIndicator.enabled = true;
    params.tabIndicator.placeWithinColumn = true;
    params.tabIndicator.position = TabIndicatorPosition::Left;
    params.tabIndicator.width = 10;
    params.tabIndicator.gap = 6; // reservation = 16px

    // Two tabs whose client minimum (400px ALONG the strip, transposed as a
    // physical fact) is what sets the column width — the asked-for 0.1
    // proportion resolves far below it.
    const QSize minMain = Ax::t(QSize(400, 0));
    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("t0"), ColumnWidth::makeProportion(0.1), ColumnDisplay::Tabbed, params,
                               minMain.width(), minMain.height()));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("t1"), ColumnWidth::makeProportion(0.1), std::nullopt,
                                               params, minMain.width(), minMain.height()));

    const ResolvedStrip resolved = strip.relayout(params);
    QCOMPARE(resolved.columns.size(), 1);
    const ResolvedColumn& column = resolved.columns.first();
    QVERIFY(column.tabbed);
    if (ScrollTestUtils::Ax::vertical()) {
        // Left eats the CROSS extent here: no main-axis correction, so a
        // reservation added unconditionally overshoots this arm by 16px.
        QCOMPARE(Ax::mainLen(column.rect), 400);
    } else {
        // The column widens by the reservation so the TILE, after the
        // indicator takes its band, still honours the client's minimum.
        QCOMPARE(Ax::mainLen(column.rect), 416);
        QCOMPARE(rectOf(resolved, strip.activeWindowId()).width(), 400);
    }
}

void TestScrollStripCore::windowedFullscreenFlagsActiveTileOnly()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    const QRect bBefore = rectOf(strip.relayout(params), QStringLiteral("b"));

    // The insert made b the active tile, so the toggle lands on b alone.
    QVERIFY(strip.toggleActiveWindowedFullscreen());
    QVERIFY(strip.isWindowedFullscreen(QStringLiteral("b")));
    QVERIFY(!strip.isWindowedFullscreen(QStringLiteral("a")));

    // Layout-neutral: the resolved rect is byte-identical, and the flag rides
    // the resolved tile so the apply payload can carry it.
    const ResolvedStrip r = strip.relayout(params);
    QCOMPARE(rectOf(r, QStringLiteral("b")), bBefore);
    bool sawFlagged = false;
    for (const ResolvedColumn& col : r.columns) {
        for (const ResolvedTile& tile : col.tiles) {
            QCOMPARE(tile.windowedFullscreen, tile.windowId == QStringLiteral("b"));
            sawFlagged = sawFlagged || tile.windowedFullscreen;
        }
    }
    QVERIFY2(sawFlagged, "b's resolved tile must exist, or the per-tile compare above passed vacuously");

    // Toggle off restores; the direct write reports change-or-not honestly.
    QVERIFY(strip.toggleActiveWindowedFullscreen());
    QVERIFY(!strip.isWindowedFullscreen(QStringLiteral("b")));
    QVERIFY(strip.setWindowedFullscreen(QStringLiteral("b"), true));
    QVERIFY(!strip.setWindowedFullscreen(QStringLiteral("b"), true));
    QVERIFY(strip.setWindowedFullscreen(QStringLiteral("b"), false));
    QVERIFY(!strip.setWindowedFullscreen(QStringLiteral("missing"), true));
}

void TestScrollStripCore::windowedFullscreenTravelsWithConsume()
{
    ScrollStrip strip;
    const auto params = defaultParams();
    // Flag the tile that actually TRAVELS: consume pulls from the column
    // right of the active one, so b is the moved Tile. The earlier form
    // flagged the stationary a, which made both assertions trivially true —
    // deleting the whole-Tile copy in consumeWindowIntoColumn left it green.
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindow(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params)); // b is active
    QVERIFY(strip.toggleActiveWindowedFullscreen()); // flags b, the traveller
    QVERIFY(strip.focusWindow(QStringLiteral("a"), params)); // a's column consumes
    QVERIFY(strip.consumeWindowIntoColumn(params));
    QCOMPARE(strip.columnCount(), 1);
    QVERIFY(strip.isWindowedFullscreen(QStringLiteral("b"))); // the MOVED tile kept its flag
    QVERIFY(!strip.isWindowedFullscreen(QStringLiteral("a"))); // negative control
}

QTEST_APPLESS_MAIN(TestScrollStripCore)
#include "test_scrollstrip_core.moc"
