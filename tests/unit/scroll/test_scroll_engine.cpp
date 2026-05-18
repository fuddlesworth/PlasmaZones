// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollScreenState.h>

#include <PhosphorEngine/PlacementEngineBase.h>

#include <QSet>
#include <QVariantList>
#include <QVariantMap>
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

ScrollScreenState* scrollStateMut(ScrollEngine& engine, const QString& screenId)
{
    return dynamic_cast<ScrollScreenState*>(engine.stateForScreen(screenId));
}

} // namespace

class TestScrollEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void windowLifecycle();
    void defaultColumnWidth();
    void perScreenConfig();
    void focusAndMoveNavigation();
    void consumeAndExpel();
    void minimizeWindow();
    void windowDropped();
    void cyclePresetWidth();
    void cyclePresetHeight();
    void toggleColumnFullWidth();
    void adjustColumnWidth();
    void toggleCenterFocusedColumn();
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

void TestScrollEngine::defaultColumnWidth()
{
    ScrollEngine engine;

    // Until a default is pushed, a freshly opened column uses niri's middle
    // preset (one half).
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->columns().at(0).width().kind, ColumnWidth::Kind::Proportion);
    QVERIFY(qFuzzyCompare(state->columns().at(0).width().value, 0.5));

    // A pushed default applies to subsequently opened columns; the existing
    // column keeps its width.
    engine.setDefaultColumnWidth(0.4);
    QVERIFY(qFuzzyCompare(engine.defaultColumnWidth(), 0.4));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    QCOMPARE(state->columnCount(), 2);
    QVERIFY(qFuzzyCompare(state->columns().at(1).width().value, 0.4));
    QVERIFY(qFuzzyCompare(state->columns().at(0).width().value, 0.5));
}

void TestScrollEngine::perScreenConfig()
{
    ScrollEngine engine;
    engine.setDefaultColumnWidth(0.5);
    engine.setInnerGap(8);
    engine.setOuterGap(8);
    engine.setPresetColumnWidths({1.0 / 3.0, 0.5, 2.0 / 3.0});

    // No override → effective resolves to the global default.
    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S1")), 0.5));
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S1")), 8);
    QVERIFY(engine.effectiveViewportMode(QStringLiteral("S1")) == ScrollViewportMode::Fit);

    // A per-screen override map shadows the globals for that screen only.
    QVariantMap overrides;
    overrides.insert(QStringLiteral("DefaultColumnWidth"), 0.25);
    overrides.insert(QStringLiteral("InnerGap"), 20);
    overrides.insert(QStringLiteral("CenterFocusedColumn"), true);
    overrides.insert(QStringLiteral("PresetColumnWidths"), QVariantList{0.4, 0.8});
    engine.applyPerScreenConfig(QStringLiteral("S1"), overrides);

    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S1")), 0.25));
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S1")), 20);
    QVERIFY(engine.effectiveViewportMode(QStringLiteral("S1")) == ScrollViewportMode::Centered);
    QCOMPARE(engine.effectivePresetColumnWidths(QStringLiteral("S1")).size(), 2);
    QVERIFY(qFuzzyCompare(engine.effectivePresetColumnWidths(QStringLiteral("S1")).at(1), 0.8));

    // A screen with no override still resolves to the globals.
    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S2")), 0.5));
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S2")), 8);

    // A window opened on S1 takes that screen's overridden default width.
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const ScrollScreenState* s1 = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(s1);
    QVERIFY(qFuzzyCompare(s1->columns().at(0).width().value, 0.25));

    // clearPerScreenConfig reverts the screen to the globals.
    engine.clearPerScreenConfig(QStringLiteral("S1"));
    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S1")), 0.5));
    QVERIFY(engine.effectiveViewportMode(QStringLiteral("S1")) == ScrollViewportMode::Fit);

    // An empty override map clears the screen too (apply-empty == clear).
    engine.applyPerScreenConfig(QStringLiteral("S1"), overrides);
    engine.applyPerScreenConfig(QStringLiteral("S1"), QVariantMap{});
    QVERIFY(engine.perScreenOverrides(QStringLiteral("S1")).isEmpty());
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S1")), 8);
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

void TestScrollEngine::toggleColumnFullWidth()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const NavigationContext ctx = contextFor(QStringLiteral("S1"));
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state && state->activeColumn());
    QVERIFY(!state->activeColumn()->isFullWidth());
    QCOMPARE(state->activeColumn()->width().value, 0.5); // default proportion

    // Toggle on: the width intent fills the whole working area.
    engine.toggleColumnFullWidth(ctx);
    QVERIFY(state->activeColumn()->isFullWidth());
    QCOMPARE(state->activeColumn()->width().kind, ColumnWidth::Kind::Proportion);
    QCOMPARE(state->activeColumn()->width().value, 1.0);

    // Toggle off: the prior width is restored.
    engine.toggleColumnFullWidth(ctx);
    QVERIFY(!state->activeColumn()->isFullWidth());
    QCOMPARE(state->activeColumn()->width().value, 0.5);

    // A preset-width cycle while full-width leaves full-width mode — setWidth()
    // clears the flag — and supersedes the restore memory.
    engine.toggleColumnFullWidth(ctx);
    QVERIFY(state->activeColumn()->isFullWidth());
    engine.cyclePresetColumnWidth(ctx);
    QVERIFY(!state->activeColumn()->isFullWidth());
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 1.0 / 3.0)); // preset 0

    // A subsequent toggle remembers that preset width and restores it.
    engine.toggleColumnFullWidth(ctx);
    QCOMPARE(state->activeColumn()->width().value, 1.0);
    engine.toggleColumnFullWidth(ctx);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 1.0 / 3.0));
    QCOMPARE(state->activeColumn()->presetWidthIndex(), 0);

    // A context targeting a screen with no scroll state is a harmless no-op:
    // S1's column is untouched and no S99 state is created.
    const qreal widthBeforeS99 = state->activeColumn()->width().value;
    engine.toggleColumnFullWidth(contextFor(QStringLiteral("S99")));
    QVERIFY(scrollState(engine, QStringLiteral("S99")) == nullptr);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, widthBeforeS99));
    QVERIFY(!state->activeColumn()->isFullWidth());

    // Empty strip (state exists but has no columns): the no-active-column
    // guard — no crash, nothing to toggle.
    engine.windowClosed(QStringLiteral("a"));
    QVERIFY(state->activeColumn() == nullptr);
    engine.toggleColumnFullWidth(ctx);
    QVERIFY(state->activeColumn() == nullptr);
}

void TestScrollEngine::adjustColumnWidth()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const NavigationContext ctx = contextFor(QStringLiteral("S1"));
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state && state->activeColumn());
    QCOMPARE(state->activeColumn()->width().value, 0.5); // default proportion

    // Grow by 0.2 -> 0.7; the width detaches from the preset cycle.
    engine.adjustColumnWidth(0.2, ctx);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 0.7));
    QCOMPARE(state->activeColumn()->presetWidthIndex(), -1);

    // Shrink by 0.5 -> 0.2.
    engine.adjustColumnWidth(-0.5, ctx);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 0.2));

    // Shrinking past the floor clamps to the 0.1 minimum.
    engine.adjustColumnWidth(-0.5, ctx);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 0.1));

    // Growing past 1.0 clamps to full viewport width.
    engine.adjustColumnWidth(2.0, ctx);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 1.0));

    // Adjusting a full-width column leaves full-width mode.
    engine.toggleColumnFullWidth(ctx);
    QVERIFY(state->activeColumn()->isFullWidth());
    engine.adjustColumnWidth(-0.1, ctx);
    QVERIFY(!state->activeColumn()->isFullWidth());
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 0.9));

    // A no-op on a fixed-pixel column — the geometry-agnostic engine cannot
    // resolve a pixel width to a working-area fraction, so the width is left
    // untouched. No engine navigation op produces a Fixed width, so the test
    // installs one directly through the mutable screen state.
    ScrollScreenState* mutState = scrollStateMut(engine, QStringLiteral("S1"));
    QVERIFY(mutState);
    mutState->setActiveColumnWidth(ColumnWidth::fixed(400.0));
    QCOMPARE(state->activeColumn()->width().kind, ColumnWidth::Kind::Fixed);
    engine.adjustColumnWidth(0.1, ctx);
    QCOMPARE(state->activeColumn()->width().kind, ColumnWidth::Kind::Fixed);
    QCOMPARE(state->activeColumn()->width().value, 400.0);

    // A context targeting a screen with no scroll state is a harmless no-op —
    // no S99 state is created.
    engine.adjustColumnWidth(0.1, contextFor(QStringLiteral("S99")));
    QVERIFY(scrollState(engine, QStringLiteral("S99")) == nullptr);

    // Empty strip (state exists but has no columns): the no-active-column
    // guard — no crash, nothing to adjust.
    engine.windowClosed(QStringLiteral("a"));
    QVERIFY(state->activeColumn() == nullptr);
    engine.adjustColumnWidth(0.1, ctx);
    QVERIFY(state->activeColumn() == nullptr);
}

void TestScrollEngine::toggleCenterFocusedColumn()
{
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S2"));
    const NavigationContext ctx = contextFor(QStringLiteral("S1"));
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

    // The viewport mode is engine-global and starts at Fit. A toggle from one
    // screen flips it and re-resolves *every* scroll screen, not just the
    // focused one — so both S1 and S2 emit placementChanged on each toggle.
    QCOMPARE(engine.viewportMode(), ScrollViewportMode::Fit);
    engine.toggleCenterFocusedColumn(ctx);
    QCOMPARE(engine.viewportMode(), ScrollViewportMode::Centered);
    QCOMPARE(spy.count(), 2);

    // Both physical screens are re-resolved — not one screen twice.
    QSet<QString> notified;
    for (const auto& emission : spy) {
        notified.insert(emission.at(0).toString());
    }
    QCOMPARE(notified, QSet<QString>({QStringLiteral("S1"), QStringLiteral("S2")}));

    engine.toggleCenterFocusedColumn(ctx);
    QCOMPARE(engine.viewportMode(), ScrollViewportMode::Fit);
    QCOMPARE(spy.count(), 4);

    // The mode is engine-global, so a toggle still flips it when the focused
    // screen has no strip of its own (S99 was never opened) — and it still
    // re-resolves the scroll screens that do exist.
    engine.toggleCenterFocusedColumn(contextFor(QStringLiteral("S99")));
    QCOMPARE(engine.viewportMode(), ScrollViewportMode::Centered);
    QCOMPARE(spy.count(), 6);
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
