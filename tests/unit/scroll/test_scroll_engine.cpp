// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollScreenState.h>

#include <PhosphorEngine/PlacementEngineBase.h>

#include <QtTest>

using namespace PhosphorScrollEngine;
using PhosphorEngine::NavigationContext;

namespace {

NavigationContext contextFor(const QString& screenId)
{
    NavigationContext ctx;
    ctx.screenId = screenId;
    return ctx;
}

const ScrollScreenState* scrollState(const ScrollEngine& engine, const QString& screenId)
{
    return dynamic_cast<const ScrollScreenState*>(engine.stateForScreen(screenId));
}

} // namespace

class TestScrollEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void windowLifecycle();
    void focusAndMoveNavigation();
    void consumeAndExpel();
    void minimizeWindow();
    void windowDropped();
    void cyclePresetWidth();
    void cyclePresetHeight();
    void floatToggle();
    void perDesktopState();
    void serializeRoundTrip();
};

void TestScrollEngine::windowLifecycle()
{
    ScrollEngine engine;
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("c"), QStringLiteral("S1"));

    QVERIFY(engine.isWindowTracked(QStringLiteral("b")));
    QVERIFY(engine.isWindowTiled(QStringLiteral("b")));
    QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("b")), QStringLiteral("S1"));
    QCOMPARE(spy.count(), 3);

    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->columnCount(), 3);

    engine.windowClosed(QStringLiteral("b"));
    QVERIFY(!engine.isWindowTracked(QStringLiteral("b")));
    QCOMPARE(state->columnCount(), 2);
}

void TestScrollEngine::focusAndMoveNavigation()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("c"), QStringLiteral("S1"));

    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->focusedWindowId(), QStringLiteral("c")); // last opened is focused

    const NavigationContext ctx = contextFor(QStringLiteral("S1"));
    engine.focusInDirection(QStringLiteral("left"), ctx);
    QCOMPARE(state->focusedWindowId(), QStringLiteral("b"));

    engine.moveFocusedInDirection(QStringLiteral("left"), ctx);
    QCOMPARE(state->columns().at(0).windowIds(), QStringList{QStringLiteral("b")});
    QCOMPARE(state->focusedWindowId(), QStringLiteral("b")); // focus follows the moved column

    engine.windowFocused(QStringLiteral("c"), QStringLiteral("S1"));
    QCOMPARE(state->focusedWindowId(), QStringLiteral("c"));
}

void TestScrollEngine::consumeAndExpel()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));

    const NavigationContext ctx = contextFor(QStringLiteral("S1"));
    engine.focusInDirection(QStringLiteral("left"), ctx); // focus column "a"
    engine.consumeWindowIntoColumn(ctx);

    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->columnCount(), 1);
    QCOMPARE(state->activeColumn()->tileCount(), 2);

    engine.expelWindowFromColumn(ctx);
    QCOMPARE(state->columnCount(), 2);
}

void TestScrollEngine::minimizeWindow()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

    engine.windowMinimizedChanged(QStringLiteral("b"), true);
    QCOMPARE(spy.count(), 1);
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->isWindowMinimized(QStringLiteral("b")));
    QCOMPARE(state->columnCount(), 2); // the slot is kept
    QVERIFY(engine.isWindowTracked(QStringLiteral("b"))); // still tracked while minimized
    // "b" was the focused window (opened last); minimizing it hands focus to
    // the nearest still-visible window so the viewport never anchors on a
    // hidden window.
    QCOMPARE(state->focusedWindowId(), QStringLiteral("a"));

    engine.windowMinimizedChanged(QStringLiteral("b"), true); // no change — no signal
    QCOMPARE(spy.count(), 1);

    engine.windowMinimizedChanged(QStringLiteral("b"), false); // restore
    QCOMPARE(spy.count(), 2);
    QVERIFY(!state->isWindowMinimized(QStringLiteral("b")));

    engine.windowMinimizedChanged(QStringLiteral("missing"), true); // untracked — no-op
    QCOMPARE(spy.count(), 2);

    // Every window minimized: focus has nowhere visible to go, so it is left
    // on its window rather than cleared. Minimizing focused "a" hands focus to
    // "b"; minimizing "b" then finds nothing visible and leaves focus on "b".
    engine.windowMinimizedChanged(QStringLiteral("a"), true);
    engine.windowMinimizedChanged(QStringLiteral("b"), true);
    QVERIFY(state->isWindowMinimized(QStringLiteral("a")));
    QVERIFY(state->isWindowMinimized(QStringLiteral("b")));
    QCOMPARE(state->focusedWindowId(), QStringLiteral("b")); // retained, not cleared
    QCOMPARE(state->columnCount(), 2); // both slots kept
}

void TestScrollEngine::windowDropped()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("c"), QStringLiteral("S1")); // [a][b][c]
    engine.windowOpened(QStringLiteral("z"), QStringLiteral("S2")); // a separate strip
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

    // Drag-to-reorder: drop "a" after "c" -> [b][c][a], focus follows "a".
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("c"), /*placeAfter=*/true);
    QCOMPARE(spy.count(), 1);
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->columns().at(2).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state->focusedWindowId(), QStringLiteral("a"));

    // Each of these drops hits a distinct rejection path and must emit no
    // signal — asserted per call so a regression on one path cannot hide
    // behind another: the dragged window's own column, an unknown dragged
    // window, and an anchor on a different screen's strip.
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("a"), true);
    QCOMPARE(spy.count(), 1);
    engine.windowDropped(QStringLiteral("missing"), QStringLiteral("b"), true);
    QCOMPARE(spy.count(), 1);
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("z"), true);
    QCOMPARE(spy.count(), 1);
    // ...and the column order is genuinely untouched, not merely signal-free.
    QCOMPARE(state->columns().at(0).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state->columns().at(1).windowIds().first(), QStringLiteral("c"));
    QCOMPARE(state->columns().at(2).windowIds().first(), QStringLiteral("a"));

    // placeAfter=false: drop "a" before "b" -> [a][b][c].
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(state->columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state->columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state->columns().at(2).windowIds().first(), QStringLiteral("c"));

    // A positional no-op — dropping "a" before "b" again, where it already
    // sits — emits without reordering: moveColumnNextTo skips the column move
    // but still returns true, so the daemon re-resolves and snaps the dragged
    // window back into its unchanged slot.
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(state->columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state->columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state->columns().at(2).windowIds().first(), QStringLiteral("c"));
}

void TestScrollEngine::cyclePresetWidth()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const NavigationContext ctx = contextFor(QStringLiteral("S1"));

    engine.cyclePresetColumnWidth(ctx);
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state && state->activeColumn());
    QCOMPARE(state->activeColumn()->presetWidthIndex(), 0);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 1.0 / 3.0));

    engine.cyclePresetColumnWidth(ctx);
    QCOMPARE(state->activeColumn()->presetWidthIndex(), 1);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 0.5));
}

void TestScrollEngine::cyclePresetHeight()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const NavigationContext ctx = contextFor(QStringLiteral("S1"));

    engine.cyclePresetWindowHeight(ctx);
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state && state->activeColumn() && state->activeColumn()->activeTile());
    QCOMPARE(state->activeColumn()->activeTile()->height.kind, WindowHeight::Kind::Preset);
    QCOMPARE(state->activeColumn()->activeTile()->height.presetIndex, 0);

    engine.cyclePresetWindowHeight(ctx);
    QCOMPARE(state->activeColumn()->activeTile()->height.presetIndex, 1);
}

void TestScrollEngine::floatToggle()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));

    engine.toggleWindowFloat(QStringLiteral("b"), QStringLiteral("S1"));
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->isFloating(QStringLiteral("b")));
    QVERIFY(!engine.isWindowTiled(QStringLiteral("b")));
    QVERIFY(engine.isWindowTracked(QStringLiteral("b"))); // floating windows stay tracked

    engine.toggleWindowFloat(QStringLiteral("b"), QStringLiteral("S1"));
    QVERIFY(!state->isFloating(QStringLiteral("b")));
    QVERIFY(engine.isWindowTiled(QStringLiteral("b"))); // back in the strip
}

void TestScrollEngine::perDesktopState()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1")); // desktop 1 (default)
    engine.setCurrentDesktop(2);
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1")); // desktop 2

    QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 2}));

    const ScrollScreenState* desktop2 = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(desktop2);
    QCOMPARE(desktop2->columnCount(), 1);
    QVERIFY(desktop2->containsWindow(QStringLiteral("b")));
    QVERIFY(!desktop2->containsWindow(QStringLiteral("a")));

    engine.pruneStatesForDesktop(1);
    QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{2}));
    QVERIFY(!engine.isWindowTracked(QStringLiteral("a")));
    QVERIFY(engine.isWindowTracked(QStringLiteral("b")));
}

void TestScrollEngine::serializeRoundTrip()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));

    ScrollEngine restored;
    restored.deserializeEngineState(engine.serializeEngineState());

    QVERIFY(restored.isWindowTracked(QStringLiteral("a")));
    QVERIFY(restored.isWindowTracked(QStringLiteral("b")));
    const ScrollScreenState* state = scrollState(restored, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->columnCount(), 2);
}

QTEST_GUILESS_MAIN(TestScrollEngine)
#include "test_scroll_engine.moc"
