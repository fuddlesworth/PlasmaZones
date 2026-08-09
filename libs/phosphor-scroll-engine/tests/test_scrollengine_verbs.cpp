// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The niri-parity verb vocabulary at the ENGINE layer: plain vs wrapping
// column focus, first/last tile focus, absolute width/height intents, the
// explicit float/tile moves and the floating/tiling focus switch. The strip
// math those verbs sit on is covered in test_scrollstrip_ops; what this file
// owns is the feedback contract, the activation/echo bookkeeping and the
// focus-side float memory.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

class TestScrollEngineVerbs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void plainColumnFocusStopsAtTheEdge();
    void wrapColumnFocusWrapsToTheFarEnd();
    void focusWindowTopBottomWalkTheColumn();
    void absoluteWidthAndHeightIntents();
    void moveToFloatingAndBackAnswersEveryPress();
    void switchFocusRoundTripsBetweenLayers();

private:
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

    /// a | b | c as three columns on S1, focus on c (arrival order).
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
    ScrollEngine* engine = threeWindows(&owner);
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

    // An interior step still works.
    engine->focusColumnPlain(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
    QCOMPARE(feedback.last().at(0).toBool(), true);
}

void TestScrollEngineVerbs::wrapColumnFocusWrapsToTheFarEnd()
{
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    // Right from the last column wraps to the first…
    engine->focusColumnWrap(1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
    // …and left from the first wraps to the last.
    engine->focusColumnWrap(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));
    // An interior step is a plain adjacent move, no wrap.
    engine->focusColumnWrap(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
}

void TestScrollEngineVerbs::focusWindowTopBottomWalkTheColumn()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Stack b and c onto a's column: focus b, consume left twice builds one
    // three-tile column the same way capturePlacement's smoke fixture does.
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
}

void TestScrollEngineVerbs::absoluteWidthAndHeightIntents()
{
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));
    QCOMPARE(feedback.last().at(0).toBool(), true);
    const Column& active = state->strip().columns().at(state->strip().activeColumnIndex());
    QCOMPARE(active.width.kind, ColumnWidth::Kind::Proportion);
    QCOMPARE(active.width.proportion, 0.25);

    // Same intent again: refused (the return value gates relayout + OSD).
    engine->setColumnWidth(ColumnWidth::makeProportion(0.25), QStringLiteral("S1"));
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

    engine->setWindowHeight(WindowHeight::makeFixed(300), QStringLiteral("S1"));
    QCOMPARE(feedback.last().at(0).toBool(), true);
    const Column& after = state->strip().columns().at(state->strip().activeColumnIndex());
    QCOMPARE(after.tiles.at(after.activeTileIdx).height.kind, WindowHeight::Kind::Fixed);
    QCOMPARE(after.tiles.at(after.activeTileIdx).height.fixedPx, 300);
}

void TestScrollEngineVerbs::moveToFloatingAndBackAnswersEveryPress()
{
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));

    // Explicit move-to-floating floats the focused tile, and the focus-side
    // memory flips with it (the window keeps compositor focus; no report
    // will arrive).
    engine->moveFocusedToFloating(QStringLiteral("S1"));
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
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

    // No float at all: the switch has nowhere to go.
    engine->switchFocusBetweenFloatingAndTiling(QStringLiteral("S1"));
    QCOMPARE(activate.count(), 0);
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
    engine->windowFocused(tiled, QStringLiteral("S1")); // the echo
    QVERIFY(!state->floatingHasFocus());

    // Switch → floating: activates the remembered float, NOT echo-queued —
    // the genuine report lands and re-arms the float side.
    engine->switchFocusBetweenFloatingAndTiling(QStringLiteral("S1"));
    QCOMPARE(activate.count(), 2);
    QCOMPARE(activate.last().at(0).toString(), QStringLiteral("app|b"));
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    QVERIFY(state->floatingHasFocus());
    QCOMPARE(state->lastFloatingFocus(), QStringLiteral("app|b"));
}

QTEST_GUILESS_MAIN(TestScrollEngineVerbs)
#include "test_scrollengine_verbs.moc"
