// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollScreenState.h>

#include <PhosphorEngine/PlacementEngineBase.h>

#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;
using PhosphorEngine::NavigationContext;

namespace {

/// Minimal in-test IScrollSettings the engine pulls global geometry config
/// through (the daemon supplies a Settings instance in production). Public
/// settable fields seeded with niri defaults; the engine qobject_casts to the
/// interface, so it must declare Q_INTERFACES.
class FakeScrollSettings : public QObject, public PhosphorEngine::IScrollSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngine::IScrollSettings)

public:
    int innerGap = 8;
    int outerGap = 8;
    double defaultColumnWidth = 0.5;
    bool centerFocusedColumn = false;
    QVariantList presetColumnWidths{1.0 / 3.0, 0.5, 2.0 / 3.0};
    QVariantList presetWindowHeights{1.0 / 3.0, 0.5, 2.0 / 3.0};

    int scrollInnerGap() const override
    {
        return innerGap;
    }
    int scrollOuterGap() const override
    {
        return outerGap;
    }
    qreal scrollDefaultColumnWidth() const override
    {
        return defaultColumnWidth;
    }
    bool scrollCenterFocusedColumn() const override
    {
        return centerFocusedColumn;
    }
    QVariantList scrollPresetColumnWidths() const override
    {
        return presetColumnWidths;
    }
    QVariantList scrollPresetWindowHeights() const override
    {
        return presetWindowHeights;
    }
};

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
    void floatToggle();
    void perDesktopState();
    void serializeRoundTrip();
    void restoreReconciliation();
    void cyclePresetWidth_stalePresetIndex();
    void cyclePresetWidth_emptyPresetListFallsBackToNiri();
    void serialize_dropsDuplicateWindowIds();
    void windowOpened_migratesAcrossScreens();
    void pruneStatesForScreen_dropsContext();
    void unsupportedOps_emitNegativeFeedback();
    void pruneStatesForActivities_dropsByActivity();
    void hasPersistableState_rejectsEmptyStates();
    void deserializeEngineState_clearsAllSessionState();
    void deserializeEngineState_normalisesNonPositiveDesktop();
    void reconcileRestoredWindows_coalescesPlacementChangedPerScreen();
    void cyclePresetWidth_skipsEmitOnSameValue();
    void reapplyLayout_doesNotEmitForUnknownScreen();
    void windowOpened_migrationDropsEmptyOldState();
    void fromJson_clampsOutOfRangeWidth();
    void windowFocused_skipsEmitOnAlreadyFocusedWindow();
    void setWindowFloat_skipsEmitOnTiledWindowAskedToUnFloat();
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
    FakeScrollSettings settings;
    engine.setEngineSettings(&settings);

    // With the settings default at niri's middle preset (one half), a freshly
    // opened column is created at that width.
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->columns().at(0).width().kind, ColumnWidth::Kind::Proportion);
    QVERIFY(qFuzzyCompare(state->columns().at(0).width().value, 0.5));

    // A changed settings default applies to subsequently opened columns; the
    // existing column keeps its width.
    settings.defaultColumnWidth = 0.4;
    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S1")), 0.4));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    QCOMPARE(state->columnCount(), 2);
    QVERIFY(qFuzzyCompare(state->columns().at(1).width().value, 0.4));
    QVERIFY(qFuzzyCompare(state->columns().at(0).width().value, 0.5));
}

void TestScrollEngine::perScreenConfig()
{
    ScrollEngine engine;
    // The fake's defaults are the globals the effective*() resolvers fall back
    // to: default column width 0.5, inner/outer gap 8, niri preset widths.
    FakeScrollSettings settings;
    engine.setEngineSettings(&settings);

    // No override → effective resolves to the global default.
    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S1")), 0.5));
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S1")), 8);
    QVERIFY(engine.effectiveViewportMode(QStringLiteral("S1")) == ScrollViewportMode::Fit);

    // A per-screen override map shadows the globals for that screen only.
    QVariantMap overrides;
    overrides.insert(QStringLiteral("DefaultColumnWidth"), 0.25);
    overrides.insert(QStringLiteral("InnerGap"), 20);
    overrides.insert(QStringLiteral("OuterGap"), 12);
    overrides.insert(QStringLiteral("CenterFocusedColumn"), true);
    overrides.insert(QStringLiteral("PresetColumnWidths"), QVariantList{0.4, 0.8});
    overrides.insert(QStringLiteral("PresetWindowHeights"), QVariantList{0.3, 0.6, 0.9});
    engine.applyPerScreenConfig(QStringLiteral("S1"), overrides);

    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S1")), 0.25));
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S1")), 20);
    QCOMPARE(engine.effectiveOuterGap(QStringLiteral("S1")), 12);
    QVERIFY(engine.effectiveViewportMode(QStringLiteral("S1")) == ScrollViewportMode::Centered);
    QCOMPARE(engine.effectivePresetColumnWidths(QStringLiteral("S1")).size(), 2);
    QVERIFY(qFuzzyCompare(engine.effectivePresetColumnWidths(QStringLiteral("S1")).at(1), 0.8));
    QCOMPARE(engine.effectivePresetWindowHeights(QStringLiteral("S1")).size(), 3);
    QVERIFY(qFuzzyCompare(engine.effectivePresetWindowHeights(QStringLiteral("S1")).at(2), 0.9));

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

    // Out-of-range per-screen override values are clamped on read by the
    // effective*() resolvers — defence-in-depth over the Settings boundary.
    QVariantMap outOfRange;
    outOfRange.insert(QStringLiteral("DefaultColumnWidth"), 5.0); // above max
    outOfRange.insert(QStringLiteral("InnerGap"), -10); // below min
    outOfRange.insert(QStringLiteral("OuterGap"), 9999); // above max
    outOfRange.insert(QStringLiteral("PresetColumnWidths"), QVariantList{0.0, 99.0});
    outOfRange.insert(QStringLiteral("PresetWindowHeights"), QVariantList{-1.0, 0.5});
    engine.applyPerScreenConfig(QStringLiteral("S3"), outOfRange);
    QVERIFY(qFuzzyCompare(engine.effectiveDefaultColumnWidth(QStringLiteral("S3")), 1.0));
    QCOMPARE(engine.effectiveInnerGap(QStringLiteral("S3")), 0);
    QCOMPARE(engine.effectiveOuterGap(QStringLiteral("S3")), 50); // clamped to kMaxStripGap
    const QVector<qreal> clampedPresets = engine.effectivePresetColumnWidths(QStringLiteral("S3"));
    QCOMPARE(clampedPresets.size(), 2);
    QVERIFY(qFuzzyCompare(clampedPresets.at(0), 0.1));
    QVERIFY(qFuzzyCompare(clampedPresets.at(1), 1.0));
    const QVector<qreal> clampedHeights = engine.effectivePresetWindowHeights(QStringLiteral("S3"));
    QCOMPARE(clampedHeights.size(), 2);
    QVERIFY(qFuzzyCompare(clampedHeights.at(0), 0.1));
    QVERIFY(qFuzzyCompare(clampedHeights.at(1), 0.5));
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

    // A complete positional no-op — dropping "a" before "b" again, where it
    // already sits AND it is already the focused window — must NOT emit:
    // moveColumnNextTo detects the no-op (target == from AND focusWindow
    // reports no change) and returns false, so the daemon is spared a
    // redundant placementChanged round-trip.
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(state->columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state->columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state->columns().at(2).windowIds().first(), QStringLiteral("c"));

    // ...but a "positional no-op" that DOES move focus is a real mutation.
    // Focus a non-dragged window, drop "a" before "b" (already its slot):
    // target == from but focus changes "c" → "a", so moveColumnNextTo returns
    // true and the daemon re-resolves once.
    engine.windowFocused(QStringLiteral("c"), QStringLiteral("S1"));
    spy.clear();
    engine.windowDropped(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(state->focusedWindowId(), QStringLiteral("a"));
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

void TestScrollEngine::restoreReconciliation()
{
    // Build a three-column strip and persist it.
    ScrollEngine source;
    source.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    source.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    source.windowOpened(QStringLiteral("c"), QStringLiteral("S1"));
    const QJsonObject saved = source.serializeEngineState();

    // Restore it, then reconcile: the effect's first batch reports only a and
    // c live — b was closed while the daemon was down. b's phantom column must
    // be pruned; a and c keep their restored columns.
    ScrollEngine restored;
    restored.deserializeEngineState(saved);
    // The screen state pointer is stable across reconciliation (windowClosed
    // prunes columns but never erases the state), so bind and guard it once.
    const ScrollScreenState* restoredState = scrollState(restored, QStringLiteral("S1"));
    QVERIFY(restoredState);
    QCOMPARE(restoredState->columnCount(), 3);
    restored.reconcileRestoredWindows(QSet<QString>{QStringLiteral("a"), QStringLiteral("c")});
    QVERIFY(restored.isWindowTracked(QStringLiteral("a")));
    QVERIFY(!restored.isWindowTracked(QStringLiteral("b")));
    QVERIFY(restored.isWindowTracked(QStringLiteral("c")));
    QCOMPARE(restoredState->columnCount(), 2);

    // Reconciliation is one-shot: a later batch (e.g. a screen entering scroll
    // mode at runtime) must not prune the now-authoritative live strip.
    restored.reconcileRestoredWindows(QSet<QString>{QStringLiteral("a")});
    QVERIFY(restored.isWindowTracked(QStringLiteral("c")));
    QCOMPARE(restoredState->columnCount(), 2);

    // Zero live windows after a restart — the effect sends an empty batch and
    // every restored column is reconciled away. The screen state survives as
    // an empty strip (windowClosed drops emptied columns, not the state).
    ScrollEngine emptied;
    emptied.deserializeEngineState(saved);
    emptied.reconcileRestoredWindows(QSet<QString>{});
    QVERIFY(!emptied.isWindowTracked(QStringLiteral("a")));
    const ScrollScreenState* emptiedState = scrollState(emptied, QStringLiteral("S1"));
    QVERIFY(emptiedState);
    QCOMPARE(emptiedState->columnCount(), 0);

    // An engine with no pending restore ignores reconciliation entirely — a
    // live-opened window is never pruned by a stray batch.
    ScrollEngine fresh;
    fresh.windowOpened(QStringLiteral("x"), QStringLiteral("S1"));
    fresh.reconcileRestoredWindows(QSet<QString>{});
    QVERIFY(fresh.isWindowTracked(QStringLiteral("x")));
}

void TestScrollEngine::cyclePresetWidth_stalePresetIndex()
{
    // Reproduces the "preset list shrank since the column was tagged" branch in
    // cyclePresetColumnWidth. With a 2-element preset list and a column tagged
    // presetWidthIndex=2 (out of range), the cycle must fall through to the
    // -1 normalisation and land at index 0 — NOT wrap from a phantom index 2
    // into something past-the-end.
    ScrollEngine engine;
    FakeScrollSettings settings;
    settings.presetColumnWidths = QVariantList{0.4, 0.8};
    engine.setEngineSettings(&settings);
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));

    ScrollScreenState* mutState = scrollStateMut(engine, QStringLiteral("S1"));
    QVERIFY(mutState);
    // Stamp a stale index by hand — no engine op produces an out-of-range
    // index, so this is the test's only path to the branch.
    mutState->setActiveColumnWidth(ColumnWidth::proportion(0.5), /*presetIndex=*/2);
    QCOMPARE(mutState->activeColumn()->presetWidthIndex(), 2);

    engine.cyclePresetColumnWidth(contextFor(QStringLiteral("S1")));
    QCOMPARE(mutState->activeColumn()->presetWidthIndex(), 0);
    QVERIFY(qFuzzyCompare(mutState->activeColumn()->width().value, 0.4));
}

void TestScrollEngine::cyclePresetWidth_emptyPresetListFallsBackToNiri()
{
    // A user that clears every preset would previously turn the cycle-width
    // shortcut into a silent no-op. After the empty-list fallback the cycle
    // resolves to the niri defaults (1/3, 1/2, 2/3) so the chord still works.
    ScrollEngine engine;
    FakeScrollSettings settings;
    settings.presetColumnWidths = QVariantList{}; // empty
    settings.presetWindowHeights = QVariantList{};
    engine.setEngineSettings(&settings);
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));

    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
    engine.cyclePresetColumnWidth(contextFor(QStringLiteral("S1")));
    QCOMPARE(spy.count(), 1);
    const ScrollScreenState* state = scrollState(engine, QStringLiteral("S1"));
    QVERIFY(state && state->activeColumn());
    QCOMPARE(state->activeColumn()->presetWidthIndex(), 0);
    QVERIFY(qFuzzyCompare(state->activeColumn()->width().value, 1.0 / 3.0));

    engine.cyclePresetWindowHeight(contextFor(QStringLiteral("S1")));
    QCOMPARE(state->activeColumn()->activeTile()->height.kind, WindowHeight::Kind::Preset);
    QCOMPARE(state->activeColumn()->activeTile()->height.presetIndex, 0);
}

void TestScrollEngine::serialize_dropsDuplicateWindowIds()
{
    // Build a malformed JSON blob with a window appearing twice across two
    // columns plus once in the floating set. fromJson must keep only the first
    // occurrence in strip order; later duplicates (and the floating mention)
    // are dropped. Without dedup, locateWindow / removeWindow / windowCount
    // would all give wrong answers downstream.
    QJsonObject tileA;
    tileA.insert(QLatin1String("windowId"), QStringLiteral("dup"));
    QJsonObject heightAuto;
    heightAuto.insert(QLatin1String("kind"), QStringLiteral("auto"));
    heightAuto.insert(QLatin1String("weight"), 1.0);
    tileA.insert(QLatin1String("height"), heightAuto);

    QJsonObject column1;
    column1.insert(QLatin1String("tiles"), QJsonArray{tileA});
    column1.insert(QLatin1String("activeTileIndex"), 0);
    QJsonObject column2;
    column2.insert(QLatin1String("tiles"), QJsonArray{tileA}); // same windowId again
    column2.insert(QLatin1String("activeTileIndex"), 0);

    QJsonObject blob;
    blob.insert(QLatin1String("screenId"), QStringLiteral("S1"));
    blob.insert(QLatin1String("columns"), QJsonArray{column1, column2});
    blob.insert(QLatin1String("activeColumnIndex"), 0);
    blob.insert(QLatin1String("scrollX"), 0.0);
    blob.insert(QLatin1String("floating"), QJsonArray{QStringLiteral("dup")}); // also floating

    const ScrollScreenState state = ScrollScreenState::fromJson(blob);
    QCOMPARE(state.columnCount(), 1); // duplicate column was dropped after its only tile
    QVERIFY(!state.isFloating(QStringLiteral("dup")));
    QCOMPARE(state.windowCount(), 1);
}

void TestScrollEngine::windowOpened_migratesAcrossScreens()
{
    // Cross-restart screen migration: a window persisted under screen S1 but
    // reported alive on S2 by the post-restore batch must move to S2's strip.
    // Without this the geometry resolve would run against S1's working area.
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("a")), QStringLiteral("S1"));
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

    // Re-open under S2: the engine should drop "a" from S1, add it to S2, and
    // emit one signal for each side so a daemon listener re-resolves both
    // screens (S1 must clear its now-empty rect, S2 must place the window).
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S2"));
    QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("a")), QStringLiteral("S2"));
    QVERIFY(spy.count() >= 2);

    const ScrollScreenState* s1 = scrollState(engine, QStringLiteral("S1"));
    if (s1) {
        QVERIFY(!s1->containsWindow(QStringLiteral("a")));
    }
    const ScrollScreenState* s2 = scrollState(engine, QStringLiteral("S2"));
    QVERIFY(s2);
    QVERIFY(s2->containsWindow(QStringLiteral("a")));

    // A second open under the SAME screen is a no-op (already-tracked + same
    // key) — must NOT duplicate the column or emit a redundant signal.
    const int before = spy.count();
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S2"));
    QCOMPARE(spy.count(), before);
    QCOMPARE(s2->columnCount(), 1);
}

void TestScrollEngine::pruneStatesForScreen_dropsContext()
{
    // Monitor unplug: pruneStatesForScreen drops every state, window mapping,
    // active-screen entry, and per-screen-config override tied to that
    // screenId — across all desktops/activities. Without this, long sessions
    // with frequent hot-plug accumulate dead entries.
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.setCurrentDesktop(2);
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1")); // different desktop, same screen
    engine.setCurrentDesktop(1);
    engine.windowOpened(QStringLiteral("c"), QStringLiteral("S2")); // different screen
    engine.applyPerScreenConfig(QStringLiteral("S1"), QVariantMap{{QStringLiteral("InnerGap"), 20}});
    engine.setActiveScreens(QSet<QString>{QStringLiteral("S1"), QStringLiteral("S2")});

    engine.pruneStatesForScreen(QStringLiteral("S1"));

    QVERIFY(!engine.isWindowTracked(QStringLiteral("a")));
    QVERIFY(!engine.isWindowTracked(QStringLiteral("b")));
    QVERIFY(engine.isWindowTracked(QStringLiteral("c"))); // S2 untouched
    QVERIFY(engine.perScreenOverrides(QStringLiteral("S1")).isEmpty());
    QVERIFY(!engine.activeScreens().contains(QStringLiteral("S1")));
    QVERIFY(engine.activeScreens().contains(QStringLiteral("S2")));
}

void TestScrollEngine::unsupportedOps_emitNegativeFeedback()
{
    // The four ops that have no scrollable equivalent (rotate, snap-all,
    // push-to-empty-zone, restore) must surface a negative navigationFeedback
    // signal so the OSD machinery renders "not supported" instead of silently
    // absorbing the chord.
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    QSignalSpy feedback(&engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

    const NavigationContext ctx = contextFor(QStringLiteral("S1"));
    engine.rotateWindows(true, ctx);
    engine.snapAllWindows(ctx);
    engine.pushToEmptyZone(ctx);
    engine.restoreFocusedWindow(ctx);

    QCOMPARE(feedback.count(), 4);
    for (int i = 0; i < feedback.count(); ++i) {
        QCOMPARE(feedback.at(i).at(0).toBool(), false); // success=false
        QCOMPARE(feedback.at(i).at(2).toString(), QStringLiteral("no_target")); // reason
    }
}

void TestScrollEngine::pruneStatesForActivities_dropsByActivity()
{
    // Symmetric to the existing pruneStatesForDesktop_dropsContext: open windows
    // under different activity contexts, prune one activity, verify the other
    // survives.
    ScrollEngine engine;
    engine.setCurrentActivity(QStringLiteral("activity-A"));
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.setCurrentActivity(QStringLiteral("activity-B"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));

    QCOMPARE(engine.isWindowTracked(QStringLiteral("a")), true);
    QCOMPARE(engine.isWindowTracked(QStringLiteral("b")), true);

    // Only activity-B is "valid" — A's windows should be dropped.
    engine.pruneStatesForActivities(QStringList{QStringLiteral("activity-B")});

    QVERIFY(!engine.isWindowTracked(QStringLiteral("a")));
    QVERIFY(engine.isWindowTracked(QStringLiteral("b")));
}

void TestScrollEngine::hasPersistableState_rejectsEmptyStates()
{
    // After every window is closed on a screen, the screen state may linger in
    // m_states (windowClosed prunes columns but not the state itself). Such a
    // state holds no information and must NOT cause hasPersistableState() to
    // report true — otherwise saveScrollState writes a non-empty
    // scroll-session.json containing only empty-strip stubs.
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    QVERIFY(engine.hasPersistableState());

    engine.windowClosed(QStringLiteral("a"));
    QVERIFY(!engine.hasPersistableState());

    // Round-trip the now-empty engine state and confirm the persisted blob
    // contains zero state entries.
    const QJsonObject blob = engine.serializeEngineState();
    QCOMPARE(blob.value(QLatin1String("states")).toArray().size(), 0);
}

void TestScrollEngine::deserializeEngineState_clearsAllSessionState()
{
    // Deserialise resets ONLY the persisted-shape containers (m_states /
    // m_windowToKey). The runtime-context fields (current desktop, current
    // activity, active screens, per-screen overrides) are owned by the daemon
    // — it has typically already pushed them via setCurrentDesktop /
    // setActiveScreens / applyPerScreenConfig before handing the persisted
    // JSON in — so resetting them here would silently overwrite the daemon's
    // authoritative live values. Build an engine with the full session shape,
    // deserialise an unrelated blob, and verify the runtime context survives
    // while the strip-state containers are cleared.
    ScrollEngine engine;
    engine.setCurrentDesktop(5);
    engine.setCurrentActivity(QStringLiteral("activity-Z"));
    engine.setActiveScreens(QSet<QString>{QStringLiteral("S1")});
    engine.applyPerScreenConfig(QStringLiteral("S1"), QVariantMap{{QStringLiteral("InnerGap"), 20}});
    // Open a window so m_states and m_windowToKey have something to clear.
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    QVERIFY(engine.isWindowTracked(QStringLiteral("a")));

    QJsonObject blob;
    blob.insert(QLatin1String("states"), QJsonArray{});
    engine.deserializeEngineState(blob);

    // Strip-state containers cleared.
    QVERIFY(!engine.isWindowTracked(QStringLiteral("a")));
    QVERIFY(engine.desktopsWithActiveState().isEmpty());

    // Runtime-context fields survive — the daemon keeps owning them.
    QCOMPARE(engine.activeScreens(), QSet<QString>{QStringLiteral("S1")});
    QCOMPARE(engine.perScreenOverrides(QStringLiteral("S1")).value(QStringLiteral("InnerGap")).toInt(), 20);
    // Opening a fresh window honours the surviving current desktop (5), not
    // the engine's construction-time default (kDefaultDesktopId == 1).
    engine.windowOpened(QStringLiteral("x"), QStringLiteral("S1"));
    QCOMPARE(engine.desktopsWithActiveState(), QSet<int>{5});
}

void TestScrollEngine::deserializeEngineState_normalisesNonPositiveDesktop()
{
    // A corrupt or hand-edited scroll-session.json with desktop=0 (or negative)
    // must not produce an unreachable state-key. The deserialise path normalises
    // non-positive values to 1 so the strip surfaces somewhere instead of
    // disappearing.
    QJsonObject column;
    column.insert(QLatin1String("tiles"), QJsonArray{});
    QJsonObject tile;
    tile.insert(QLatin1String("windowId"), QStringLiteral("w"));
    QJsonObject heightAuto;
    heightAuto.insert(QLatin1String("kind"), QStringLiteral("auto"));
    column.insert(QLatin1String("tiles"), QJsonArray{tile});
    column.insert(QLatin1String("activeTileIndex"), 0);

    QJsonObject state;
    state.insert(QLatin1String("screenId"), QStringLiteral("S1"));
    state.insert(QLatin1String("columns"), QJsonArray{column});
    state.insert(QLatin1String("activeColumnIndex"), 0);
    state.insert(QLatin1String("scrollX"), 0.0);
    state.insert(QLatin1String("floating"), QJsonArray{});
    state.insert(QLatin1String("desktop"), 0); // invalid
    state.insert(QLatin1String("activity"), QStringLiteral(""));

    QJsonObject blob;
    blob.insert(QLatin1String("states"), QJsonArray{state});

    ScrollEngine engine;
    engine.deserializeEngineState(blob);
    // Desktop=0 should have been clamped to 1; check the resulting active set.
    QCOMPARE(engine.desktopsWithActiveState(), QSet<int>{1});
}

void TestScrollEngine::reconcileRestoredWindows_coalescesPlacementChangedPerScreen()
{
    // Build a strip with three stale windows on one screen, persist + restore.
    // Then reconcile against an empty live set: every restored window is stale
    // and must be pruned, but the dispatch of placementChanged must coalesce
    // into ONE emit per screen (not three).
    ScrollEngine source;
    source.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    source.windowOpened(QStringLiteral("b"), QStringLiteral("S1"));
    source.windowOpened(QStringLiteral("c"), QStringLiteral("S1"));
    const QJsonObject saved = source.serializeEngineState();

    ScrollEngine restored;
    restored.deserializeEngineState(saved);
    QSignalSpy spy(&restored, &PhosphorEngine::PlacementEngineBase::placementChanged);
    restored.reconcileRestoredWindows(QSet<QString>{}); // every window stale
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("S1"));
    QVERIFY(!restored.isWindowTracked(QStringLiteral("a")));
    QVERIFY(!restored.isWindowTracked(QStringLiteral("b")));
    QVERIFY(!restored.isWindowTracked(QStringLiteral("c")));
}

void TestScrollEngine::cyclePresetWidth_skipsEmitOnSameValue()
{
    // Single-element preset list: the column's width is set to that preset, and
    // a subsequent cycle press normalises to (0+1)%1==0 == current. With both
    // index AND value matching, the engine must skip the emit. Without this
    // guard the user sees flicker / a redundant daemon resolve every press.
    ScrollEngine engine;
    FakeScrollSettings settings;
    settings.presetColumnWidths = QVariantList{0.5}; // one preset
    engine.setEngineSettings(&settings);
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));

    // First press lands at preset 0 (value 0.5).
    engine.cyclePresetColumnWidth(contextFor(QStringLiteral("S1")));
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
    engine.cyclePresetColumnWidth(contextFor(QStringLiteral("S1")));
    QCOMPARE(spy.count(), 0); // same index, same value — no-op
}

void TestScrollEngine::reapplyLayout_doesNotEmitForUnknownScreen()
{
    // reapplyLayout used to emit placementChanged unconditionally; the daemon
    // would then dispatch a layout pass for a screen the engine doesn't manage.
    // The fix gates the emit on a non-null state — verify by sending a context
    // for a screen where no window has ever been opened.
    ScrollEngine engine;
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
    engine.reapplyLayout(contextFor(QStringLiteral("S99")));
    QCOMPARE(spy.count(), 0);
}

void TestScrollEngine::windowOpened_migrationDropsEmptyOldState()
{
    // Cross-restart screen migration must not leave the old empty state
    // hanging in m_states. Open one window on S1, migrate it to S2, verify
    // the S1 state is gone (so it doesn't get serialised on shutdown).
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S2"));

    // S1's state should be erased — no serialised blob entry for it.
    const QJsonObject blob = engine.serializeEngineState();
    const QJsonArray states = blob.value(QLatin1String("states")).toArray();
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.first().toObject().value(QLatin1String("screenId")).toString(), QStringLiteral("S2"));
}

void TestScrollEngine::fromJson_clampsOutOfRangeWidth()
{
    // Defence-in-depth: a corrupt scroll-session.json with a 100.0 proportion
    // would otherwise produce a column 100x the screen width on restore.
    // Column::fromJson clamps Proportion widths into [kMinSizeFraction,
    // kMaxSizeFraction] at the persistence boundary.
    QJsonObject widthBlob;
    widthBlob.insert(QLatin1String("kind"), QStringLiteral("proportion"));
    widthBlob.insert(QLatin1String("value"), 100.0);

    QJsonObject tileObj;
    tileObj.insert(QLatin1String("windowId"), QStringLiteral("w"));
    QJsonObject heightAuto;
    heightAuto.insert(QLatin1String("kind"), QStringLiteral("auto"));
    heightAuto.insert(QLatin1String("weight"), 1.0);
    tileObj.insert(QLatin1String("height"), heightAuto);

    QJsonObject columnObj;
    columnObj.insert(QLatin1String("tiles"), QJsonArray{tileObj});
    columnObj.insert(QLatin1String("activeTileIndex"), 0);
    columnObj.insert(QLatin1String("width"), widthBlob);

    Column restored = Column::fromJson(columnObj);
    QCOMPARE(restored.width().kind, ColumnWidth::Kind::Proportion);
    QVERIFY(restored.width().value <= 1.0);
    QVERIFY(restored.width().value >= 0.1);
}

void TestScrollEngine::windowFocused_skipsEmitOnAlreadyFocusedWindow()
{
    // A duplicate focus event on the already-focused window — Qt-side
    // wl_keyboard/wl_pointer can deliver paired events, the effect can
    // re-announce after a reattach — must not fan out a redundant
    // placementChanged. The engine routes through ScrollScreenState::focusWindow,
    // which now early-returns false when {column, tile} already match.
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1"));
    engine.windowOpened(QStringLiteral("b"), QStringLiteral("S1")); // focus = b
    QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

    // Refocus the already-focused window: no signal.
    engine.windowFocused(QStringLiteral("b"), QStringLiteral("S1"));
    QCOMPARE(spy.count(), 0);

    // A real focus change still emits.
    engine.windowFocused(QStringLiteral("a"), QStringLiteral("S1"));
    QCOMPARE(spy.count(), 1);

    // ...and re-announcing the new focus is a no-op too.
    engine.windowFocused(QStringLiteral("a"), QStringLiteral("S1"));
    QCOMPARE(spy.count(), 1);
}

void TestScrollEngine::setWindowFloat_skipsEmitOnTiledWindowAskedToUnFloat()
{
    // A daemon path that calls setWindowFloat(false) on a window that is
    // already tiled (never floated) must not emit windowFloatingChanged or
    // placementChanged — there is no transition. Symmetric with the
    // floating→float-already case.
    ScrollEngine engine;
    engine.windowOpened(QStringLiteral("a"), QStringLiteral("S1")); // tiled
    QSignalSpy placementSpy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
    QSignalSpy floatingSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

    // Already tiled, asked to un-float: no transition.
    engine.setWindowFloat(QStringLiteral("a"), false);
    QCOMPARE(placementSpy.count(), 0);
    QCOMPARE(floatingSpy.count(), 0);
    QVERIFY(engine.isWindowTiled(QStringLiteral("a")));

    // Float the window — that's a real transition, both signals fire once.
    engine.setWindowFloat(QStringLiteral("a"), true);
    QCOMPARE(placementSpy.count(), 1);
    QCOMPARE(floatingSpy.count(), 1);

    // Already floating, asked to float: no transition.
    engine.setWindowFloat(QStringLiteral("a"), true);
    QCOMPARE(placementSpy.count(), 1);
    QCOMPARE(floatingSpy.count(), 1);

    // Un-float — real transition, signals fire again.
    engine.setWindowFloat(QStringLiteral("a"), false);
    QCOMPARE(placementSpy.count(), 2);
    QCOMPARE(floatingSpy.count(), 2);
}

QTEST_GUILESS_MAIN(TestScrollEngine)
#include "test_scroll_engine.moc"
