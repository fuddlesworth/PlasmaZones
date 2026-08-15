// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The niri-parity verb vocabulary at the ENGINE layer: plain vs wrapping
// column focus, first/last tile focus, absolute width/height intents, the
// explicit float/tile moves and the floating/tiling focus switch, plus the
// cross-output and cross-mode focus/move crossings (horizontal, vertical,
// and from an empty screen). The strip math those verbs sit on is covered
// in test_scrollstrip_ops; what this file owns is the feedback contract,
// the activation/echo bookkeeping and the focus-side float memory.

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::RightNeighbourResolver;

namespace {

/// S2 sits one step along the CROSS axis — the topology the cross-axis output
/// crossings need: a focus or move that exhausts its COLUMN continues onto
/// that output (and the no-neighbour directions still refuse). Cross rather
/// than main is the whole point of these tests, so the token comes from
/// crossTrail: on a vertical strip the exhausted-column press is "right", not
/// "down", and "down" would walk the strip instead.
struct DownNeighbourResolver : PhosphorEngine::ICrossSurfaceResolver
{
    QString neighborOutputInDirection(const QString&, const QString& direction) const override
    {
        return direction == Ax::crossTrail() ? QStringLiteral("S2") : QString();
    }
    int neighborDesktopInDirection(int, const QString&) const override
    {
        return 0;
    }
};

} // namespace

class TestScrollEngineVerbs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, then skips while the
    /// engine is horizontal-only.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void plainColumnFocusStopsAtTheEdge();
    void wrapColumnFocusWrapsToTheFarEnd();
    void wrapOnASingleColumnRefusesBothWays();
    void focusWindowTopBottomWalkTheColumn();
    void absoluteWidthAndHeightIntents();
    void centerVisibleColumnsCentersOnceThenRefuses();
    void everyVerbAnswersNoWindowsOnAnEmptyScreen();
    void moveToFloatingAndBackAnswersEveryPress();
    void switchFocusRoundTripsBetweenLayers();
    void focusCrossesToTheScrollNeighboursEntryWindow();
    void verticalFocusCrossesToTheOutputBelow();
    void verticalMoveCrossesToTheOutputBelow();
    void verticalSwapCrossesToTheOutputBelow();
    void emptyScreenFocusCrossesInsteadOfDeadEnding();
    void focusEdgeWithNoNeighbourReportsNoTarget();
    void focusOntoAForeignModeNeighbourDefersToTheDaemon();

private:
    /// Bare engine: NO geometry providers and NO auto-echo. Only for tests
    /// that hand-drive the activation/echo protocol themselves
    /// (switchFocusRoundTripsBetweenLayers) or need no rects at all — the
    /// apply path resolves no real geometry on this fixture, so verbs whose
    /// strip op reads layout params run under degenerate geometry here.
    /// Everything else uses providerThreeWindows below.
    static ScrollEngine* makeEngine(QObject* parent)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        engine->setActiveScreens({QStringLiteral("S1")});
        return engine;
    }

    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    /// a | b | c as three columns on S1 with REAL geometry providers and the
    /// auto-echo, focus on c (arrival order).
    static ScrollEngine* providerThreeWindows(QObject* parent)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {QStringLiteral("S1")});
        engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
        return engine;
    }

    /// Same strip on the bare fixture — for the echo-hand-driving tests.
    static ScrollEngine* threeWindows(QObject* parent)
    {
        ScrollEngine* engine = makeEngine(parent);
        engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
        return engine;
    }
};

void TestScrollEngineVerbs::plainColumnFocusStopsAtTheEdge()
{
    QObject owner;
    ScrollEngine* engine = providerThreeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    // Right from the last column: the plain verb stops dead, with feedback.
    engine->focusColumnPlain(1, QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    // An interior step still works, and success carries the pressed
    // direction as the reason (the OSD derives its arrow from it).
    engine->focusColumnPlain(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), Ax::navLead());

    // An out-of-contract delta is rejected outright, without feedback.
    const int countBefore = feedback.count();
    engine->focusColumnPlain(0, QStringLiteral("S1"));
    engine->focusColumnPlain(2, QStringLiteral("S1"));
    QCOMPARE(feedback.count(), countBefore);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
}

void TestScrollEngineVerbs::wrapColumnFocusWrapsToTheFarEnd()
{
    QObject owner;
    ScrollEngine* engine = providerThreeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    // Right from the last column wraps to the first… and the success reason
    // carries the PRESSED direction, not the jump's travel direction: the
    // verb continued the user's motion past the edge, so the OSD arrow
    // points the way they pressed. Pinned — a wrap that reported the travel
    // direction would draw the opposite arrow with the whole suite green.
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->focusColumnWrap(1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), Ax::navTrail());
    // …and left from the first wraps to the last.
    engine->focusColumnWrap(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));
    // An interior step is a plain adjacent move, no wrap.
    engine->focusColumnWrap(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));

    // A zero delta must NOT short-circuit into the wrap fallback and
    // teleport focus to the strip head — it is rejected outright.
    engine->focusColumnWrap(0, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
}

void TestScrollEngineVerbs::wrapOnASingleColumnRefusesBothWays()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    // The discriminating case for the wrap's short-circuit: with one column
    // both the adjacent step AND the far-end fallback refuse (the far end IS
    // the focused column), so the verb reports no_target instead of a
    // phantom success.
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->focusColumnWrap(1, QStringLiteral("S1"));
    engine->focusColumnWrap(-1, QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 2);
    for (int i = 0; i < 2; ++i) {
        QCOMPARE(feedback.at(i).at(0).toBool(), false);
        QCOMPARE(feedback.at(i).at(2).toString(), QStringLiteral("no_target"));
    }
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
}

void TestScrollEngineVerbs::focusWindowTopBottomWalkTheColumn()
{
    QObject owner;
    ScrollEngine* engine = providerThreeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Stack b and c onto a's column: focus b then c, consuming left after
    // each, which builds one three-tile column the same way
    // capturePlacement's smoke fixture does.
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|c"), QStringLiteral("S1"));
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    engine->focusWindowTop(QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
    engine->focusWindowBottom(QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    // Already at the bottom: refused, with feedback.
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->focusWindowBottom(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

    // And the mirrored refusal at the top — focusWindowTop carries its own
    // edge guard, so the bottom arm alone cannot pin it.
    engine->focusWindowTop(QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
    const int beforeTop = feedback.count();
    engine->focusWindowTop(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), beforeTop + 1);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
}

void TestScrollEngineVerbs::absoluteWidthAndHeightIntents()
{
    QObject owner;
    ScrollEngine* engine = providerThreeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    const Column& active = state->strip().columns().at(state->strip().activeColumnIndex());
    QCOMPARE(active.width.kind, ColumnWidth::Kind::Proportion);
    QCOMPARE(active.width.proportion, 0.25);

    // Same intent again: refused (the return value gates relayout + OSD).
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

    // An out-of-range Proportion is clamped at this exported boundary (the
    // library's own envelope; the D-Bus layer rejects before calling).
    engine->setColumnWidth(ColumnWidth::makeProportion(4.0), QStringLiteral("S1"));
    QCOMPARE(state->strip().columns().at(state->strip().activeColumnIndex()).width.proportion, 1.0);

    engine->setWindowHeight(WindowHeight::makeFixed(300), QStringLiteral("S1"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    const Column& after = state->strip().columns().at(state->strip().activeColumnIndex());
    QCOMPARE(after.tiles.at(after.activeTileIdx).height.kind, WindowHeight::Kind::Fixed);
    QCOMPARE(after.tiles.at(after.activeTileIdx).height.fixedPx, 300);

    // The height twin's idempotent refusal — its equality check is as
    // load-bearing as the width's.
    engine->setWindowHeight(WindowHeight::makeFixed(300), QStringLiteral("S1"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
}

void TestScrollEngineVerbs::centerVisibleColumnsCentersOnceThenRefuses()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Narrow both columns so the fully-visible span is smaller than the
    // viewport and centering genuinely moves the view.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->centerVisibleColumns(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("center"));

    // The repeat press finds the span already centered and refuses — the
    // no-op verdict the strip op's return value carries.
    engine->centerVisibleColumns(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 2);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
}

void TestScrollEngineVerbs::everyVerbAnswersNoWindowsOnAnEmptyScreen()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

    // Every new verb on a screen with no strip answers the distinct
    // no_windows reason (CLAUDE.md's empty-zones edge case) rather than
    // silence or no_target.
    engine->focusColumnPlain(1, QStringLiteral("S1"));
    engine->focusColumnWrap(1, QStringLiteral("S1"));
    engine->focusWindowTop(QStringLiteral("S1"));
    engine->focusWindowBottom(QStringLiteral("S1"));
    engine->centerVisibleColumns(QStringLiteral("S1"));
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));
    engine->setWindowHeight(WindowHeight::makeFixed(300), QStringLiteral("S1"));
    engine->moveFocusedToFloating(QStringLiteral("S1"));
    engine->moveFocusedToTiling(QStringLiteral("S1"));
    engine->switchFocusBetweenFloatingAndTiling(QStringLiteral("S1"));

    QCOMPARE(feedback.count(), 10);
    for (int i = 0; i < feedback.count(); ++i) {
        QCOMPARE(feedback.at(i).at(0).toBool(), false);
        QCOMPARE(feedback.at(i).at(2).toString(), QStringLiteral("no_windows"));
    }
}

void TestScrollEngineVerbs::moveToFloatingAndBackAnswersEveryPress()
{
    QObject owner;
    ScrollEngine* engine = providerThreeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));

    // Explicit move-to-floating floats the focused tile, and the focus-side
    // memory flips with it (the window keeps compositor focus; no report
    // will arrive). A SUCCESSFUL press emits no navigation feedback — the
    // float-verb convention delegates to setWindowFloat, whose visible
    // window move is the feedback; only refusals speak. Pinned here so the
    // asymmetry stays deliberate rather than drifting.
    QSignalSpy successFeedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedToFloating(QStringLiteral("S1"));
    QCOMPARE(successFeedback.count(), 0);
    QVERIFY(state->isFloating(QStringLiteral("app|b")));
    QVERIFY(state->floatingHasFocus());
    QCOMPARE(state->lastFloatingFocus(), QStringLiteral("app|b"));

    // A second press has nothing to float: the focused window IS the float.
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedToFloating(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    QVERIFY(state->isFloating(QStringLiteral("app|b")));

    // move-to-tiling re-tiles the focused float and hands focus back.
    engine->moveFocusedToTiling(QStringLiteral("S1"));
    QVERIFY(!state->isFloating(QStringLiteral("app|b")));
    QVERIFY(!state->floatingHasFocus());
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));

    // And its second press is refused the same way: the focus is on a tile.
    feedback.clear();
    engine->moveFocusedToTiling(QStringLiteral("S1"));
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
}

void TestScrollEngineVerbs::switchFocusRoundTripsBetweenLayers()
{
    // Deliberately the BARE fixture (no auto-echo): this test hand-drives
    // the echo protocol below, and makeProviderEngine's automatic echo
    // would deliver each activation's report before the assertions between
    // the press and the echo could observe the intermediate state.
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

    // No float at all: the switch has nowhere to go.
    engine->switchFocusBetweenFloatingAndTiling(QStringLiteral("S1"));
    QCOMPARE(activate.count(), 0);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

    // Float the focused tile; the float layer now holds focus.
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    QVERIFY(state->floatingHasFocus());

    // Switch → tiling: activates the strip's active window and queues the
    // echo, so the compositor's answering report must NOT re-arm the float
    // side.
    activate.clear();
    engine->switchFocusBetweenFloatingAndTiling(QStringLiteral("S1"));
    QCOMPARE(activate.count(), 1);
    const QString tiled = activate.last().at(0).toString();
    QVERIFY(tiled != QStringLiteral("app|b"));
    QVERIFY(!state->floatingHasFocus());
    // Success speaks the layer copy: action "float" with the "tiled"
    // reason (the OSD's float arm renders it without a direction arrow).
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("float"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("tiled"));
    engine->windowFocused(tiled, QStringLiteral("S1")); // the echo
    QVERIFY(!state->floatingHasFocus());

    // Switch → floating: activates the remembered float, NOT echo-queued —
    // the genuine report lands and re-arms the float side.
    engine->switchFocusBetweenFloatingAndTiling(QStringLiteral("S1"));
    QCOMPARE(activate.count(), 2);
    QCOMPARE(activate.last().at(0).toString(), QStringLiteral("app|b"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("float"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("floating"));
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    QVERIFY(state->floatingHasFocus());
    QCOMPARE(state->lastFloatingFocus(), QStringLiteral("app|b"));
}

void TestScrollEngineVerbs::focusCrossesToTheScrollNeighboursEntryWindow()
{
    RightNeighbourResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S2"), 0, 0);

    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    // a is S1's only column: the strip refuses, and the crossing enters S2
    // by its LEFT edge — the entry window is b, the leftmost visible tile.
    engine->focusInDirection(Ax::navTrail(), ctx);
    QCOMPARE(activate.count(), 1);
    QCOMPARE(activate.last().at(0).toString(), QStringLiteral("app|b"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::navTrail());
    // Announced on the DESTINATION screen, the move arm's convention.
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));
    // The DESTINATION strip's own focus moved to the entry window and the
    // source strip's did not — without these, a regression that activates
    // the right window but leaves S2's active column stale passes, and the
    // next relative navigation on S2 starts from the wrong column.
    QCOMPARE(stateFor(engine, QStringLiteral("S2"))->strip().activeWindowId(), QStringLiteral("app|b"));
    QCOMPARE(stateFor(engine, QStringLiteral("S1"))->strip().activeWindowId(), QStringLiteral("app|a"));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineVerbs::verticalFocusCrossesToTheOutputBelow()
{
    DownNeighbourResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);

    // A vertical press that exhausts the column crosses onto the output
    // below, riding the same boundary machinery as the horizontal walk:
    // entryWindowForCrossing's vertical arm stands the target's own focused
    // window in for the strip edge a vertical press does not have.
    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    engine->focusInDirection(Ax::crossTrail(), ctx);
    QCOMPARE(activate.count(), 1);
    QCOMPARE(activate.last().at(0).toString(), QStringLiteral("app|b"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::crossTrail());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));
    QCOMPARE(stateFor(engine, QStringLiteral("S2"))->strip().activeWindowId(), QStringLiteral("app|b"));

    // With no neighbour in the pressed direction (this resolver only offers
    // "down"), the exhausted walk still answers no_target.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->focusInDirection(Ax::crossLead(), ctx);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineVerbs::verticalMoveCrossesToTheOutputBelow()
{
    DownNeighbourResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);

    // A vertical move that exhausts the column carries the window onto the
    // output below, entering as an APPENDED column (a vertical crossing has
    // no facing strip edge — the handoffReceive convention).
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    engine->moveFocusedInDirection(Ax::crossTrail(), ctx);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::crossTrail());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));
    QVERIFY(!stateFor(engine, QStringLiteral("S1"))->strip().containsWindow(QStringLiteral("app|a")));
    QCOMPARE(stateFor(engine, QStringLiteral("S2"))->strip().windowsInOrder(),
             (QStringList{QStringLiteral("app|b"), QStringLiteral("app|a")}));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineVerbs::verticalSwapCrossesToTheOutputBelow()
{
    DownNeighbourResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);

    // A vertical SWAP that exhausts the column trades places with the output
    // below: the mover joins S2 and the partner — S2's entry window, its
    // focused tile standing in for the strip edge a vertical crossing does
    // not have — comes back to S1. This is the leg that exercises
    // entryWindowForCrossing's vertical arm feeding the partner-landing
    // machinery, which the move twin never reaches. With one window per
    // screen the assertions pin the two-way TRADE (a degraded move would
    // leave S1 empty); the partner's exact landing slot inside a populated
    // source strip is left to the horizontal swap coverage.
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    engine->swapFocusedInDirection(Ax::crossTrail(), ctx);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("swap"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::crossTrail());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));
    // BOTH windows changed strips, each holding the other's old ground.
    QCOMPARE(stateFor(engine, QStringLiteral("S1"))->strip().windowsInOrder(), (QStringList{QStringLiteral("app|b")}));
    QCOMPARE(stateFor(engine, QStringLiteral("S2"))->strip().windowsInOrder(), (QStringList{QStringLiteral("app|a")}));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineVerbs::emptyScreenFocusCrossesInsteadOfDeadEnding()
{
    RightNeighbourResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->setCrossSurfaceResolver(&resolver);
    // S1 stays EMPTY; only the neighbour holds a window.
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);

    // niri parity: a directional focus press on an empty monitor walks onto
    // the neighbour instead of dead-ending with no_windows — the boundary
    // is the whole verb when there is nothing local to prefer.
    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    engine->focusInDirection(Ax::navTrail(), ctx);
    QCOMPARE(activate.count(), 1);
    QCOMPARE(activate.last().at(0).toString(), QStringLiteral("app|b"));
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::navTrail());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));

    // No neighbour that way and nothing local: the empty screen still
    // answers no_windows (this resolver offers only "right").
    engine->focusInDirection(Ax::navLead(), ctx);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_windows"));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineVerbs::focusEdgeWithNoNeighbourReportsNoTarget()
{
    RightNeighbourResolver resolver; // no LEFT neighbour
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    engine->focusInDirection(Ax::navLead(), ctx);
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineVerbs::focusOntoAForeignModeNeighbourDefersToTheDaemon()
{
    RightNeighbourResolver resolver;
    QObject owner;
    // S2 exists as a neighbour but is NOT a scrolling screen — a
    // different-mode context this engine holds no state for.
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);

    QSignalSpy crossFocus(engine, &PhosphorEngine::PlacementEngineBase::crossModeFocusRequested);
    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    PhosphorEngine::NavigationContext ctx;
    ctx.screenId = QStringLiteral("S1");
    // NO handler connected: the daemon-side verdict (the bool* out-param)
    // stays false, so the engine must report no_target on the SOURCE rather
    // than announcing a crossing nothing performed — the empty-neighbour
    // case this contract exists for.
    engine->focusInDirection(Ax::navTrail(), ctx);
    QCOMPARE(crossFocus.count(), 1);
    QCOMPARE(crossFocus.last().at(0).toString(), QStringLiteral("S2"));
    QCOMPARE(crossFocus.last().at(1).toString(), Ax::navTrail());
    QCOMPARE(activate.count(), 0);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S1"));

    // With a handler that reports the activation happened (the daemon's
    // DirectConnection shape), the same press announces success on the
    // DESTINATION.
    connect(
        engine, &PhosphorEngine::PlacementEngineBase::crossModeFocusRequested, engine,
        [](const QString&, const QString&, bool* handled) {
            if (handled) {
                *handled = true;
            }
        },
        Qt::DirectConnection);
    engine->focusInDirection(Ax::navTrail(), ctx);
    QCOMPARE(crossFocus.count(), 2);
    QVERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(0).toBool(), true);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:") + Ax::navTrail());
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));
    engine->setCrossSurfaceResolver(nullptr);
}

QTEST_GUILESS_MAIN(TestScrollEngineVerbs)
#include "test_scrollengine_verbs.moc"
