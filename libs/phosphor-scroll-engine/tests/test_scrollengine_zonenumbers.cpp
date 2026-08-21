// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The zone-number space and the verbs that address it: what the walk numbers
// (visibleTiles), what a digit press resolves to, what it actually MOVES (a
// stack-mate reorder inside the column, a whole-column travel across columns),
// what it refuses, and the cross-output crossings whose landing slots the swap
// contract promises. Split out of test_scrollengine_smoke.cpp, which is at its
// size ceiling; the engine fixture is the suite-wide one in
// scrollstriptestutils.h.
//
// Geometry throughout: the suite's shared work area, 1200 ALONG the strip and
// 800 ACROSS it on either axis, with no IScrollSettings (so the inner gap is 0)
// and a 600px default column, which makes exactly two columns fit. A third is
// parked off-screen and carries no number, which several of these tests lean
// on.

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

#include <limits>

using namespace PhosphorScrollEngine;

namespace {

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::makeProviderEngine;

/// Two outputs adjacent ALONG THE STRIP, so the crossing verbs under test are
/// strip-travel ones on either axis. The direction tokens come from the
/// harness rather than being spelled "right" and "left": on a vertical strip
/// the same topology is stacked, and a resolver still answering only to the
/// horizontal words would make every crossing test pass vacuously by never
/// finding a neighbour at all.
struct SideBySideResolver : PhosphorEngine::ICrossSurfaceResolver
{
    QString neighborOutputInDirection(const QString& screenId, const QString& direction) const override
    {
        if (direction == ScrollTestUtils::Ax::navTrail() && screenId == QLatin1String("S1")) {
            return QStringLiteral("S2");
        }
        if (direction == ScrollTestUtils::Ax::navLead() && screenId == QLatin1String("S2")) {
            return QStringLiteral("S1");
        }
        return QString();
    }
    int neighborDesktopInDirection(int, const QString&) const override
    {
        return 0;
    }
};

QString wid(const char* suffix)
{
    return QLatin1String("app|") + QLatin1String(suffix);
}

} // namespace

class TestScrollEngineZoneNumbers : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void zoneNumbersAreViewportRelativeVisibleSlots();
    void crossColumnDigitMovesTheWholeColumn();
    void intraColumnDigitReordersStackMate();
    void minimizedTileCarriesNoNumberAndIsSteppedOver();
    void outOfRangeDigitIsRejected();
    void digitVerbWithoutATargetReportsWhichKindOfNothing();
    void digitNamingTheOperatedWindowIsANoOp();
    void tabbedStackMateDigitRefocusesWithoutReordering();
    void hiddenTabLosesItsNumberAndGetsItBack();
    void visibleTileCarriesItsColumnsTabIndicator();
    void visibleTileTabLengthIsMeasuredOffTheResolvedRects();
    void visibleTileReportsTheResolvedIndicatorPosition();
    void columnDrawingNoIndicatorCarriesNoTabFields();
    void clippedEdgeTilesKeepTheirNumbers();
    void crossOutputMoveKeepsHeightAndAnnouncesOnDestination();
    void crossOutputSwapTradesSlotsWithStackedSource();
    void windowedFullscreenFeedbackPinsOnOffTokens();

private:
    /// One scrolling output on the suite's shared geometry (1200 along the
    /// strip, 800 across it), geometry-provider seam wired.
    static ScrollEngine* oneScreenEngine(QObject* parent)
    {
        return makeProviderEngine(parent, {QStringLiteral("S1")});
    }

    /// S1 and S2 as adjacent outputs on the suite's shared geometry, both
    /// scrolling. S2 sits one full output further ALONG THE STRIP, which is to
    /// the right of S1 on a horizontal strip and below it on a vertical one —
    /// the same relation SideBySideResolver answers with.
    static ScrollEngine* makeTwoScreenEngine(QObject* parent)
    {
        return makeProviderEngine(parent, {QStringLiteral("S1"), QStringLiteral("S2")}, [](const QString& screenId) {
            return screenId == QLatin1String("S2") ? Ax::t(QRect(1200, 0, 1200, 800)) : defaultScreenRect();
        });
    }

    /// The state for a screen, or nullptr. QVERIFY'd at the call site rather
    /// than dereferenced blind: a regression that drops the state would take
    /// the whole binary down and discard the other tests' results.
    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    // NOTE: the canonicalization step in visibleTileNumberForWindow /
    // moveFocusedToPosition is NOT exercised with a non-canonical id
    // anywhere here. It needs a WindowRegistry, and these fixtures pass none
    // — canonicalizeForLookup is then the identity, so every id in this file
    // is already canonical. The registry's own mapping has its own tests.
    static int numberOf(ScrollEngine* engine, const char* suffix)
    {
        return engine->visibleTileNumberForWindow(QStringLiteral("S1"), wid(suffix));
    }

    static PhosphorEngine::NavigationContext ctxFor(const char* suffix)
    {
        return PhosphorEngine::NavigationContext{wid(suffix), QStringLiteral("S1")};
    }
};

void TestScrollEngineZoneNumbers::zoneNumbersAreViewportRelativeVisibleSlots()
{
    // Zone numbers are VIEWPORT-relative visible TILE slots, not strip
    // indices: tiles are numbered sequentially in strip order (columns lead to
    // trail, tiles cross-lead to cross-trail within each), the leadmost
    // on-screen tile is 1 regardless of how many columns are parked off-screen
    // behind it, and a parked column's tiles have no number at all.
    // visibleTiles is the single source: the preview rect walk, the per-window
    // query and the Snap-to-Zone digit target all derive from it, so every
    // visible window carries its own distinct number and every consumer
    // addresses the same tile.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("c"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).size(), 3);

    // Opening focuses the newest column, so the view shows [b, c] and a is
    // the one parked off the LEAD end. Pinned PER WINDOW: a sorted multiset of
    // {-1, 1, 2} never said WHICH window was parked, so parking the focused
    // column instead passed.
    QCOMPARE(numberOf(engine, "a"), -1);
    QCOMPARE(numberOf(engine, "b"), 1);
    QCOMPARE(numberOf(engine, "c"), 2);

    // The stamped number, the walk position and the per-window query are one
    // number space. Asserting all three against each other is the point: the
    // stamp exists so a consumer cannot re-derive an ordinal that disagrees.
    const QVector<QRect> rects = engine->visibleTileRects(QStringLiteral("S1"));
    const QVector<ScrollEngine::VisibleTile> onScreen = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(rects.size(), 2);
    QCOMPARE(onScreen.size(), rects.size());
    for (int i = 0; i < rects.size(); ++i) {
        QCOMPARE(rects.at(i), onScreen.at(i).rect);
        QCOMPARE(onScreen.at(i).zoneNumber, i + 1);
        QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S1"), onScreen.at(i).windowId), i + 1);
    }

    // An unknown window has no slot, and neither does a window on a screen the
    // engine does not manage.
    QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S1"), QStringLiteral("app|nope")), -1);
    QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S9"), wid("a")), -1);

    // Stacking two windows into ONE column: every visible tile still gets
    // its own distinct number, in strip order. Three visible tiles across
    // two columns number 1..3 — the stacked pair does NOT collapse onto a
    // shared column ordinal (per-column numbering was the old model; it
    // rendered duplicate labels in every preview).
    // Focus pinned explicitly: consumeOrExpel acts on the ACTIVE column, and
    // the assertions below hold only because c is the one that MOVES — a
    // solo active window is taken out of its own column and appended into
    // the LEAD neighbour's (b's) stack; b's column is the survivor, c's
    // ceases to exist.
    engine->windowFocused(wid("c"), QStringLiteral("S1"));
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    const QVector<ScrollEngine::VisibleTile> stacked = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(stacked.size(), 3);
    for (int i = 0; i < stacked.size(); ++i) {
        QCOMPARE(stacked.at(i).zoneNumber, i + 1);
        QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S1"), stacked.at(i).windowId), i + 1);
    }
    // The stacked pair shares a column, and the walk is column-major: the
    // single column's tile comes first, then the pair back to back.
    QVERIFY(stacked.at(0).columnIndex != stacked.at(1).columnIndex);
    QCOMPARE(stacked.at(1).columnIndex, stacked.at(2).columnIndex);
    // Lead to trail: neither columnIndex nor the MAIN position ever decreases
    // along the walk, so a reversed iteration fails here. Read by role, not as
    // rect.x(): on a vertical strip every column shares an x, and a physical
    // read makes this vacuous for a strip of solo columns.
    for (int i = 1; i < stacked.size(); ++i) {
        QVERIFY(stacked.at(i).columnIndex >= stacked.at(i - 1).columnIndex);
        QVERIFY(Ax::mainPos(stacked.at(i).rect) >= Ax::mainPos(stacked.at(i - 1).rect));
    }

    // The normalized twin walks the same tiles, and every rect is inside
    // the unit square.
    const QVector<QRectF> relative = engine->visibleTileRectsRelative(QStringLiteral("S1"));
    QCOMPARE(relative.size(), stacked.size());
    for (const QRectF& r : relative) {
        QVERIFY(r.left() >= 0.0 && r.top() >= 0.0);
        QVERIFY(r.right() <= 1.0 + 1e-9 && r.bottom() <= 1.0 + 1e-9);
    }
}

void TestScrollEngineZoneNumbers::crossColumnDigitMovesTheWholeColumn()
{
    // THE documented address-vs-action divergence (ScrollEngine.h,
    // VisibleTile): a digit naming a tile in ANOTHER column moves the whole
    // active COLUMN to that column's strip position, stack-mates included.
    // The window the user operated therefore does NOT end up wearing the
    // number that was pressed — asserted here so the semantics cannot drift
    // silently in either direction.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // x joins a's column
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);

    // Two 600px columns fill the work area, so all three tiles are visible:
    // the stacked pair first (column-major), then b.
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "x"), 2);
    QCOMPARE(numberOf(engine, "b"), 3);

    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedToPosition(3, ctxFor("a"));

    // a's column travelled to b's slot, carrying x with it.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("b"), wid("a"), wid("x")}));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("a")), 1);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("x")), 1);
    // And the operated window now reads 2, not the 3 that was pressed. The
    // carried stack-mate is re-read in the number space too — the column
    // travelled as a unit, so x sits directly under a.
    QCOMPARE(numberOf(engine, "a"), 2);
    QCOMPARE(numberOf(engine, "b"), 1);
    QCOMPARE(numberOf(engine, "x"), 3);

    // The SUCCESS feedback, asserted nowhere else in the suite: the snap
    // action, an empty reason (the OSD renders its landing card from the
    // target, not from a direction arrow), and both window slots naming the
    // OPERATED window — passing the displaced tile as the target rendered
    // that other window's post-move number instead.
    QCOMPARE(feedback.count(), 1);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("snap"));
    QVERIFY(feedback.last().at(2).toString().isEmpty());
    QCOMPARE(feedback.last().at(3).toString(), wid("a"));
    QCOMPARE(feedback.last().at(4).toString(), wid("a"));
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S1"));
}

void TestScrollEngineZoneNumbers::intraColumnDigitReordersStackMate()
{
    // A digit naming a STACK-MATE reorders the operated window inside its
    // column, in both directions, and here the pressed number is exactly
    // what the window ends up wearing.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowOpened(wid("y"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("x"), wid("y")}));
    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 3);

    // Cross-trailward: the cross-lead tile onto the cross-trail slot.
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    engine->moveFocusedToPosition(3, ctxFor("a"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("x"), wid("y"), wid("a")}));
    QCOMPARE(numberOf(engine, "a"), 3);

    // Cross-leadward: and back to the head of the stack.
    engine->moveFocusedToPosition(1, ctxFor("a"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("x"), wid("y")}));
    QCOMPARE(numberOf(engine, "a"), 1);
}

void TestScrollEngineZoneNumbers::minimizedTileCarriesNoNumberAndIsSteppedOver()
{
    // A minimized tile keeps its slot in the strip but drops out of the
    // relayout, so it carries no number — and the digit path's step
    // arithmetic counts only the NON-minimized tiles between the two slots,
    // because that is the space moveActiveTile walks. Counting the minimized
    // one as a step overshoots the stack and the move is refused outright.
    //
    // Minimize is driven straight on the strip: the daemon models it as a
    // float, so no engine verb reaches the flag (see the seam note on
    // ScrollStrip::setWindowMinimized).
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowOpened(wid("y"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("x"), wid("y")}));

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Params built to match the ENGINE's own resolution (no IScrollSettings
    // attached, so gap 0) — defaultParams()'s gap 10 would re-clamp the view
    // anchor against geometry the engine never uses. The shared helper is
    // that exact struct AND carries the arm's axis; a local copy that omitted
    // it drove a portrait work area through a horizontal mapper on the
    // vertical arm.
    QVERIFY(state->strip().setWindowMinimized(wid("x"), true, ScrollTestUtils::engineParams()));
    engine->retile(QStringLiteral("S1"));

    // The minimized tile is still MANAGED and still in the order, but it has
    // no number, exactly like a hidden tab.
    QVERIFY(engine->isWindowTracked(wid("x")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).size(), 3);
    QCOMPARE(numberOf(engine, "x"), -1);
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "y"), 2);

    // Press 2 from the cross-lead tile: one non-minimized step separates them, so
    // the move lands a on the slot it named. Counting x as a step would ask
    // for two, run off the end of the stack, and refuse.
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    engine->moveFocusedToPosition(2, ctxFor("a"));
    QCOMPARE(numberOf(engine, "a"), 2);
    QCOMPARE(numberOf(engine, "y"), 1);
    QCOMPARE(numberOf(engine, "x"), -1);
}

void TestScrollEngineZoneNumbers::outOfRangeDigitIsRejected()
{
    // A digit the strip cannot honour is REFUSED, not clamped onto the
    // nearest tile (SnapEngine's convention): clamping moved a window the
    // user never named. 0 and INT_MIN pin the pre-subtraction bound —
    // position - 1 on an unvalidated int is one adaptor away from the wire,
    // and INT_MIN - 1 is the underflow the ordering exists to prevent.
    //
    // The bound is the VISIBLE count, not the managed one: three columns are
    // open here and only two fit, so 3 names a window that exists and is
    // tracked but carries no number. That distinction is the headline claim
    // of the whole number space.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("c"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).size(), 3);
    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 2);
    const QStringList before = engine->managedWindowOrder(QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    for (const int position : {0, 3, -1, std::numeric_limits<int>::min()}) {
        const int emitted = feedback.count();
        engine->moveFocusedToPosition(position, ctxFor("b"));
        QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), before);
        // Exactly one emission per rejection — a silent position would
        // otherwise re-read the previous iteration's feedback.
        QVERIFY2(feedback.count() == emitted + 1,
                 qPrintable(
                     QStringLiteral("position %1 emitted %2 feedbacks").arg(position).arg(feedback.count() - emitted)));
        QVERIFY2(!feedback.last().at(0).toBool(),
                 qPrintable(QStringLiteral("position %1 reported success").arg(position)));
        QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("snap"));
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("invalid_zone_number"));
    }

    // Positive control: the two numbers that ARE in range are honoured, so
    // the rejections above measure the bound and not a dead digit path.
    engine->moveFocusedToPosition(2, ctxFor("b"));
    QVERIFY(feedback.last().at(0).toBool());
    QVERIFY(engine->managedWindowOrder(QStringLiteral("S1")) != before);
}

void TestScrollEngineZoneNumbers::digitVerbWithoutATargetReportsWhichKindOfNothing()
{
    // The two empty cases are reported differently, because the OSD says
    // different things: an EMPTY STRIP is no_windows, while a strip that has
    // windows but nothing currently numbered is no_target.
    QObject owner;
    // S2's provider answers a null rect: its strip accepts windows, but no
    // relayout can resolve against it, so the walk comes back empty.
    ScrollEngine* engine =
        makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")}, [](const QString& screenId) {
            return screenId == QLatin1String("S2") ? QRect() : defaultScreenRect();
        });

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedToPosition(1, PhosphorEngine::NavigationContext{QString(), QStringLiteral("S1")});
    QCOMPARE(feedback.count(), 1);
    QVERIFY(!feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("snap"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_windows"));

    engine->windowOpened(wid("v"), QStringLiteral("S2"), 0, 0);
    QVERIFY(engine->isWindowTracked(wid("v")));
    QVERIFY(engine->visibleTiles(QStringLiteral("S2")).isEmpty());
    engine->moveFocusedToPosition(1, PhosphorEngine::NavigationContext{wid("v"), QStringLiteral("S2")});
    QCOMPARE(feedback.count(), 2);
    QVERIFY(!feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
}

void TestScrollEngineZoneNumbers::digitNamingTheOperatedWindowIsANoOp()
{
    // The digit already naming the operated window is a no-op reported with
    // NavigationController's reason token, so the OSD shows the dedicated
    // "already in that position" copy instead of a generic failure.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    const QStringList before = engine->managedWindowOrder(QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedToPosition(1, ctxFor("a"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), before);
    QCOMPARE(feedback.count(), 1);
    QVERIFY(!feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("snap"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("already_at_position"));

    // A screen-hinted press carrying NO window falls back to the strip's own
    // active window, so the same digit is the same no-op. This is the other
    // half of the operand rule: ctx names the operand when it can, the strip
    // focus stands in when it cannot.
    engine->moveFocusedToPosition(1, PhosphorEngine::NavigationContext{QString(), QStringLiteral("S1")});
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), before);
    QCOMPARE(feedback.count(), 2);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("already_at_position"));
    QCOMPARE(feedback.last().at(3).toString(), wid("a"));
}

void TestScrollEngineZoneNumbers::tabbedStackMateDigitRefocusesWithoutReordering()
{
    // Three things at once, because one press produces all of them.
    //
    // The ctx window is the OPERAND, not the strip's focus: ctx names the
    // HIDDEN tab here, so the press refocuses that tab (it becomes the shown
    // one) instead of acting on the tab that was visible.
    //
    // The reorder is then REFUSED, because the target column is Tabbed. Only
    // its shown tab carries a number, so a stack-mate digit inside a tabbed
    // column can only ever name a tile the user cannot see; shuffling the
    // stack behind the tab bar is not what the digit asked for.
    //
    // And the refocus-only outcome still reaches applyLayout and
    // placementChanged: focus is persisted state, so an arm that mutates the
    // focus and returns early would leave the model and the saved strip out
    // of step.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // x joins a's column
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "x"), -1);
    const QStringList before = engine->managedWindowOrder(QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
    engine->moveFocusedToPosition(1, ctxFor("x"));

    // Nothing was reordered.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), before);
    QCOMPARE(feedback.count(), 1);
    QVERIFY(!feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    // But the operand DID take the focus, so the tab swapped: x now wears the
    // column's only number and a has lost it.
    QCOMPARE(numberOf(engine, "x"), 1);
    QCOMPARE(numberOf(engine, "a"), -1);
    QVERIFY2(placement.count() >= 1, "a refocus is persisted state and must announce a placement change");
}

void TestScrollEngineZoneNumbers::hiddenTabLosesItsNumberAndGetsItBack()
{
    // A tabbed column shows one tile; its other tabs are hidden, drop out of
    // the walk, and report no number at all — so no digit can target them.
    // Untabbing puts the number back: the tab state is presentation, and a
    // permanent loss of the address would be a strip regression the tabbed
    // assertion alone cannot see.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 2);

    engine->toggleColumnTabbed(QStringLiteral("S1"));

    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 1);
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "x"), -1);
    // The window is still MANAGED — it lost its number, not its tile.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).size(), 2);
    QVERIFY(engine->isWindowTracked(wid("x")));

    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 2);
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "x"), 2);
}

void TestScrollEngineZoneNumbers::visibleTileCarriesItsColumnsTabIndicator()
{
    // The walk drops a column's hidden tabs, so the tile it DOES emit is the
    // only place a consumer can learn that the tile stands for a stack. These
    // are the fields the strip previews draw their tab pills from.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // x joins a's column
    engine->windowFocused(wid("a"), QStringLiteral("S1"));

    // A normal column carries no indicator, whatever it holds: two stacked
    // tiles are two tiles, not two tabs. Size-checked first, or a walk that
    // returned nothing at all would satisfy the loop by never entering it.
    const QVector<ScrollEngine::VisibleTile> plain = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(plain.size(), 2);
    for (const ScrollEngine::VisibleTile& tile : plain) {
        QCOMPARE(tile.tabCount, 0);
        QCOMPARE(tile.activeTabIndex, -1);
        QCOMPARE(tile.tabLengthProportion, 0.0);
    }

    engine->toggleColumnTabbed(QStringLiteral("S1"));

    const QVector<ScrollEngine::VisibleTile> tabbed = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(tabbed.size(), 1);
    // Both tabs are counted, hidden one included — those are the pills the
    // indicator shows — and the shown tab names its own slot among them.
    QCOMPARE(tabbed.first().windowId, wid("a"));
    QCOMPARE(tabbed.first().tabCount, 2);
    QCOMPARE(tabbed.first().activeTabIndex, 0);
    // The proportion is pinned numerically in its own case below; here it is
    // only asserted to exist, since a bare 0 would silently give every preview
    // an empty band. (The <= 1.0 companion this once carried was structurally
    // guaranteed by the qBound in the walk and could never fail.)
    QVERIFY(tabbed.first().tabLengthProportion > 0.0);

    // Switching tabs moves the index with the shown tile. Nothing about the
    // strip's GEOMETRY changes here, which is exactly why a preview cannot
    // decide it is up to date from the rects alone.
    engine->windowFocused(wid("x"), QStringLiteral("S1"));
    const QVector<ScrollEngine::VisibleTile> switched = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(switched.size(), 1);
    QCOMPARE(switched.first().windowId, wid("x"));
    QCOMPARE(switched.first().tabCount, 2);
    QCOMPARE(switched.first().activeTabIndex, 1);
}

namespace {

/// A screen carrying one tabbed column of two windows, with @p overrides
/// applied before the tabbing so they are in force when the strip resolves.
ScrollEngine* tabbedColumnEngine(QObject* owner, const QVariantMap& overrides = {})
{
    ScrollEngine* engine = ScrollTestUtils::makeProviderEngine(owner, {QStringLiteral("S1")});
    if (!overrides.isEmpty()) {
        engine->applyPerScreenConfig(QStringLiteral("S1"), overrides);
    }
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    return engine;
}

} // namespace

void TestScrollEngineZoneNumbers::visibleTileTabLengthIsMeasuredOffTheResolvedRects()
{
    // The proportion is the whole reason the payload carries a length at all:
    // the resolve rounds the configured fraction and floors it at one pixel,
    // so a preview that re-applied the raw setting would draw a length the
    // screen does not have. Pinned NUMERICALLY against the rect the walk
    // reports beside it — asserting only "> 0" left the entire computation
    // replaceable by a constant.
    QObject owner;
    ScrollEngine* engine = tabbedColumnEngine(&owner);

    const QVector<ScrollEngine::VisibleTile> tiles = engine->visibleTiles(QStringLiteral("S1"));
    const QVector<QRect> rects = engine->visibleTileRects(QStringLiteral("S1"));
    QCOMPARE(tiles.size(), 1);
    QCOMPARE(rects.size(), 1);

    // Default position is Left, so the indicator runs along the column's
    // HEIGHT. Measuring the other axis would divide the indicator's thickness
    // by the column's width and land nowhere near this.
    const int axisExtent = rects.first().height();
    QVERIFY(axisExtent > 0);
    const qreal configured = engine->tabIndicatorParamsForScreen(QStringLiteral("S1")).lengthProportion;
    const qreal expected = static_cast<qreal>(qBound(1, qRound(axisExtent * configured), axisExtent)) / axisExtent;
    QVERIFY(qAbs(tiles.first().tabLengthProportion - expected) < 0.0001);
}

void TestScrollEngineZoneNumbers::visibleTileReportsTheResolvedIndicatorPosition()
{
    // Position is stamped from the resolved column, and Left is BOTH the
    // struct default and the configured default — so only a non-default value
    // can tell a real stamp from a field nobody wrote. Bottom also transposes
    // the length basis onto the column's width, which pins the axis choice.
    QObject owner;
    QVariantMap overrides;
    overrides.insert(ScrollPerScreenKeys::tabIndicatorPosition(), static_cast<int>(TabIndicatorPosition::Bottom));
    ScrollEngine* engine = tabbedColumnEngine(&owner, overrides);
    QCOMPARE(engine->tabIndicatorParamsForScreen(QStringLiteral("S1")).position, TabIndicatorPosition::Bottom);

    const QVector<ScrollEngine::VisibleTile> tiles = engine->visibleTiles(QStringLiteral("S1"));
    const QVector<QRect> rects = engine->visibleTileRects(QStringLiteral("S1"));
    QCOMPARE(tiles.size(), 1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(tiles.first().tabIndicatorPosition, TabIndicatorPosition::Bottom);

    const int axisExtent = rects.first().width();
    QVERIFY(axisExtent > 0);
    const qreal configured = engine->tabIndicatorParamsForScreen(QStringLiteral("S1")).lengthProportion;
    const qreal expected = static_cast<qreal>(qBound(1, qRound(axisExtent * configured), axisExtent)) / axisExtent;
    QVERIFY(qAbs(tiles.first().tabLengthProportion - expected) < 0.0001);
}

void TestScrollEngineZoneNumbers::columnDrawingNoIndicatorCarriesNoTabFields()
{
    // The gate is the RESOLVED indicator rect, not the column's tabbed flag,
    // and the master switch is one of the things that rect folds in. Without
    // this leg every negative case in the suite was an untabbed column, so
    // dropping the rect half of the gate changed nothing anywhere: a tabbed
    // column with the indicator switched off would have reported a tab count
    // and sprouted a pill in every preview.
    QObject owner;
    QVariantMap overrides;
    overrides.insert(ScrollPerScreenKeys::tabIndicatorEnabled(), false);
    ScrollEngine* engine = tabbedColumnEngine(&owner, overrides);
    QCOMPARE(engine->tabIndicatorParamsForScreen(QStringLiteral("S1")).enabled, false);

    const QVector<ScrollEngine::VisibleTile> tiles = engine->visibleTiles(QStringLiteral("S1"));
    // The column is still tabbed — one visible tile standing for two windows.
    QCOMPARE(tiles.size(), 1);
    QCOMPARE(tiles.first().tabCount, 0);
    QCOMPARE(tiles.first().activeTabIndex, -1);
    QCOMPARE(tiles.first().tabLengthProportion, 0.0);
}

void TestScrollEngineZoneNumbers::clippedEdgeTilesKeepTheirNumbers()
{
    // "Clipped, not dropped": centering the middle of three 600px columns
    // pushes its neighbours halfway off BOTH edges. They keep their numbers,
    // and the rects the walk reports are the CLIPPED ones — which, IN THIS
    // FIXTURE (screen rect == work area, crop mode off, remainders above the
    // peek floor), also equal the APPLIED rects. The equality is not general:
    // with a panel the walk clips to the work area while the apply clamps to
    // the screen, a remainder below the peek floor parks while keeping its
    // number, and crop mode applies the true rect.
    //
    // Work area runs 1200 ALONG the strip with 600px columns: centering the
    // middle one puts it at main 300..900, leaving the outer two at -300..300
    // and 900..1500.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(wid("b"), QStringLiteral("S1"));
    engine->centerColumn(QStringLiteral("S1"));

    const QVector<ScrollEngine::VisibleTile> tiles = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(tiles.size(), 3);
    QCOMPARE(tiles.at(0).windowId, wid("a"));
    QCOMPARE(tiles.at(0).rect, Ax::t(QRect(0, 0, 300, 800)));
    QCOMPARE(tiles.at(1).windowId, wid("b"));
    QCOMPARE(tiles.at(1).rect, Ax::t(QRect(300, 0, 600, 800)));
    QCOMPARE(tiles.at(2).windowId, wid("c"));
    QCOMPARE(tiles.at(2).rect, Ax::t(QRect(900, 0, 300, 800)));
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "b"), 2);
    QCOMPARE(numberOf(engine, "c"), 3);

    // The APPLIED geometry is CLAMPED at the screen edge on BOTH sides, and
    // symmetrically — this reverses the old keep-the-true-rect contract.
    // Render-side cropping of a committed overhang proved unenforceable (the
    // compositor may present an idle surface on a hardware plane, bypassing
    // the effect chain), so the rect itself stops at the boundary; and the
    // clamp applies on edges WITHOUT an adjacent output too, so the two
    // peeks of a centred column look the same on every topology.
    QCOMPARE(engine->lastManagedRect(wid("b")), Ax::t(QRect(300, 0, 600, 800)));
    QCOMPARE(engine->lastManagedRect(wid("a")), Ax::t(QRect(0, 0, 300, 800)));
    QCOMPARE(engine->lastManagedRect(wid("c")), Ax::t(QRect(900, 0, 300, 800)));
}

void TestScrollEngineZoneNumbers::crossOutputMoveKeepsHeightAndAnnouncesOnDestination()
{
    // Moving off the strip's TRAIL edge migrates the window to the adjacent
    // scrolling output: it enters at the facing edge (column 0 for a trailward
    // crossing), keeps the user's height intent, and the success feedback
    // names the DESTINATION screen — the source no longer holds the window
    // the OSD is about.
    //
    // Resolver BEFORE owner: the engine (owned by `owner`) must be destroyed
    // before the resolver it borrows, and a failing QCOMPARE returns before
    // the trailing setCrossSurfaceResolver(nullptr) runs.
    SideBySideResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeTwoScreenEngine(&owner);
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("p"), QStringLiteral("S2"), 0, 0);

    engine->windowFocused(wid("b"), QStringLiteral("S1"));
    engine->cycleWindowPresetHeight(1, QStringLiteral("S1"));
    const int height = Ax::crossLen(engine->lastManagedRect(wid("b")));
    QVERIFY2(height > 0 && height < 800, qPrintable(QStringLiteral("expected a preset height, got %1").arg(height)));
    // Windowed fullscreen is per-tile state the crossing must carry too
    // (niri keeps it across move-column-to-monitor); asserted on the target
    // beside the height-intent carry below.
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    // ctx names a DIFFERENT window than the strip's focus, deliberately: the
    // verb acts on the strip's active window and the feedback must name that
    // one. With ctx and the focus coinciding, a boundary arm that reported
    // ctx.windowId would be indistinguishable from the correct one.
    engine->moveFocusedInDirection(Ax::navTrail(), PhosphorEngine::NavigationContext{wid("a"), QStringLiteral("S1")});

    QCOMPARE(engine->screenForTrackedWindow(wid("b")), QStringLiteral("S2"));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S2"), wid("b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S2"), wid("p")), 1);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a")}));
    // The height intent crossed with it (insertWindowAt builds a default
    // Auto tile, so this only holds because the crossing re-applies it).
    QCOMPARE(Ax::crossLen(engine->lastManagedRect(wid("b"))), height);
    // And so did windowed fullscreen, for the same reason.
    auto* targetState = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S2")));
    QVERIFY(targetState);
    QVERIFY(targetState->strip().isWindowedFullscreen(wid("b")));

    QCOMPARE(feedback.count(), 1);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("move"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::navTrail());
    QCOMPARE(feedback.last().at(3).toString(), wid("b"));
    // A crossing names no landing TARGET: the OSD renders the direction, not
    // a zone card, so filling this slot would make it render the wrong one.
    QVERIFY(feedback.last().at(4).toString().isEmpty());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));

    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineZoneNumbers::crossOutputSwapTradesSlotsWithStackedSource()
{
    // The swap TRADES slots. When the crossing window came out of a STACK,
    // its vacated slot is a tile inside a surviving column, so the partner
    // must re-enter that column — landing it in a fresh column beside the
    // stack is the shape this pins against. The partner's height intent
    // crosses too, and the feedback names the destination screen.
    //
    // Resolver before owner: see the move test above.
    SideBySideResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeTwoScreenEngine(&owner);
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // b joins a's column
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("b")), 0);
    engine->windowOpened(wid("p"), QStringLiteral("S2"), 0, 0);

    engine->windowFocused(wid("p"), QStringLiteral("S2"));
    engine->cycleWindowPresetHeight(1, QStringLiteral("S2"));
    const int partnerHeight = Ax::crossLen(engine->lastManagedRect(wid("p")));
    QVERIFY2(partnerHeight > 0 && partnerHeight < 800,
             qPrintable(QStringLiteral("expected a preset height, got %1").arg(partnerHeight)));

    // The PARTNER's windowed-fullscreen flag crosses with the trade too —
    // the swap's partner leg is a separate capture/re-apply pair from the
    // mover's, and this is its only coverage (the mover leg is pinned by the
    // move twin above). p is already focused from the height setup above.
    engine->toggleWindowedFullscreen(QStringLiteral("S2"));
    {
        auto* s2State = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S2")));
        QVERIFY(s2State);
        QVERIFY(s2State->strip().isWindowedFullscreen(wid("p")));
    }

    engine->windowFocused(wid("b"), QStringLiteral("S1"));
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->swapFocusedInDirection(Ax::navTrail(), PhosphorEngine::NavigationContext{wid("b"), QStringLiteral("S1")});

    QCOMPARE(engine->screenForTrackedWindow(wid("b")), QStringLiteral("S2"));
    QCOMPARE(engine->screenForTrackedWindow(wid("p")), QStringLiteral("S1"));
    auto* s1State = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
    QVERIFY(s1State);
    QVERIFY(s1State->strip().isWindowedFullscreen(wid("p"))); // the partner's flag survived the trade
    auto* s2StateAfter = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S2")));
    QVERIFY(s2StateAfter);
    QVERIFY(!s2StateAfter->strip().isWindowedFullscreen(wid("b")));
    // The trade: p took b's TILE slot beside a. Both windows in column 0 is
    // the whole assertion — a fresh column for p would push a to index 1.
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("p")), 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("p")}));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S2"), wid("b")), 0);
    // A Preset height resolves against the column's CROSS extent, which is the
    // work area's cross extent on both outputs, so the surviving intent reads
    // identically. Compared through Ax::crossLen for that reason: on a vertical
    // strip the column's cross extent is its physical WIDTH.
    QCOMPARE(Ax::crossLen(engine->lastManagedRect(wid("p"))), partnerHeight);

    QCOMPARE(feedback.count(), 1);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("swap"));
    // Same reason token as the move twin — the OSD reads the direction out of
    // it, and a swap that reported a bare direction would render as a move.
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::navTrail());
    QCOMPARE(feedback.last().at(3).toString(), wid("b"));
    QVERIFY(feedback.last().at(4).toString().isEmpty());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));

    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineZoneNumbers::windowedFullscreenFeedbackPinsOnOffTokens()
{
    // The OSD's success arm discriminates on the LITERAL "on"/"off" reason
    // tokens (an unpinned rename would silently announce "on" for an off
    // toggle), so pin the full tuple for both directions of the toggle.
    QObject owner;
    ScrollEngine* engine = oneScreenEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 1);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("fullscreen"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("on"));

    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 2);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("fullscreen"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("off"));
}

// GUILESS, matching the sibling engine suites: the coalesced retile and the
// prunes' deleteLater need a real event dispatcher.
QTEST_GUILESS_MAIN(TestScrollEngineZoneNumbers)

#include "test_scrollengine_zonenumbers.moc"
