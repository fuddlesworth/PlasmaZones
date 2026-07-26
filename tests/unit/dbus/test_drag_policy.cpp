// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_drag_policy.cpp
 * @brief Truth-table guard for WindowDragAdaptor::computeDragPolicy
 *
 * The drag protocol refactor moves drag routing policy out of the
 * compositor plugin (where it lived in m_dragBypassedForAutotile and
 * m_cachedZoneSelectorEnabled caches that went stale after every settings
 * reload — see discussion #310) and into a daemon-side pure function.
 *
 * This test pins the function's behavior against the inputs that can
 * change routing:
 *
 *   (snap_enabled, screen_is_autotile, screen_is_scrolling,
 *    window_tracked_by_scroll, context_disabled, reorder_mode)
 *
 * reorder_mode is the RESOLVED bool computeDragPolicy takes; the per-screen
 * rule override that produces it (effectiveReorderMode) is a separate
 * function outside this file's scope.
 *     → PhosphorProtocol::DragPolicy.bypassReason + flags
 *
 * Precedence (first match wins, strongest disable first):
 *   context_disabled → autotile_screen → scrolling_screen →
 *   snapping_disabled → canonical_snap
 *
 * The precedence order is load-bearing — the log forensics on #310 showed
 * drags flip-flopping for tens of seconds after a settings reload because
 * the effect's cache disagreed with the daemon about whether a screen was
 * autotile-managed; pinning the daemon-side decision table removes the
 * only place that ambiguity can live.
 */

#include <QTest>
#include <QCoreApplication>
#include <QObject>

#include <PhosphorContext/IContextResolver.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include "helpers/AutotileTestHelpers.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"

#include "helpers/StubSettings.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

namespace {

/// Subclass of StubSettings that lets the test flip snappingEnabled and the
/// per-case autotile-drag behavior. The C1 cascade refactor routes the
/// per-monitor disable check through `IContextResolver::disabledReason`, so
/// the historical `m_monitorDisabled` flag + `isMonitorDisabled` override
/// is dead — `FakeContextResolver::m_disabled` is the live trigger.
class PolicyStubSettings : public StubSettings
{
public:
    bool m_snapEnabled = true;
    AutotileDragBehavior m_dragBehavior = AutotileDragBehavior::Float;

    bool snappingEnabled() const override
    {
        return m_snapEnabled;
    }

    AutotileDragBehavior autotileDragBehavior() const override
    {
        return m_dragBehavior;
    }
};

/// Minimal `IContextResolver` for the test — the only resolver methods
/// `computeDragPolicy` reads are `handleFor` + `isDisabled` (where
/// `isDisabled` is the non-virtual sugar over `disabledReason`), so every
/// other method returns a trivial default. `m_disabled` is the
/// test-controlled flag that drives the ContextDisabled branch. Replaces
/// the previous `PolicyStubSettings::m_monitorDisabled` trigger now that
/// `computeDragPolicy` consults the resolver, not ISettings, for the
/// disable cascade.
class FakeContextResolver : public PhosphorContext::IContextResolver
{
public:
    bool m_disabled = false;

    PhosphorContext::ContextHandle handleFor(const QString& screenId) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        return h;
    }
    PhosphorContext::ContextHandle globalHandle() const override
    {
        return {};
    }
    PhosphorContext::ContextHandle handleForMode(const QString& screenId,
                                                 PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.mode = mode;
        return h;
    }
    PhosphorContext::ContextHandle handleForPersisted(const QString& screenId, int virtualDesktop,
                                                      const QString& activity) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.virtualDesktop = virtualDesktop;
        h.activity = activity;
        return h;
    }
    int currentVirtualDesktop() const override
    {
        return 0;
    }
    QString currentActivity() const override
    {
        return QString();
    }
    PhosphorContext::DisabledReason disabledReason(const PhosphorContext::ContextHandle&) const override
    {
        return m_disabled ? PhosphorContext::DisabledReason::MonitorDisabled
                          : PhosphorContext::DisabledReason::NotDisabled;
    }
    bool isLocked(const PhosphorContext::ContextHandle&) const override
    {
        return false;
    }
};

/// Build a minimal AutotileEngine with one screen seeded (or empty) so
/// isAutotileScreen() returns the desired answer. The engine is constructed
/// with null dependencies — computeDragPolicy only reads isAutotileScreen
/// and isWindowTracked, neither of which touches layoutManager / tracker /
/// screenManager. A fresh engine reports `isWindowTracked = false` for every
/// windowId; tests that need the tracked branch seed a window through the
/// standard windowOpened lifecycle via seedTrackedWindow() below (the same
/// null-dep pattern test_autotile_drag_insert uses).
std::unique_ptr<AutotileEngine> makeEngine(bool screenIsAutotile, const QString& screenId)
{
    auto engine = std::make_unique<AutotileEngine>(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
    if (screenIsAutotile) {
        engine->setAutotileScreens(QSet<QString>{screenId});
    }
    return engine;
}

/// Register @p windowId through the standard windowOpened lifecycle so
/// isWindowTracked() returns true for it — the tracked branch that
/// computeDragPolicy's immediateFloatOnStart logic keys on. Mirrors the
/// null-dep seeding pattern in test_autotile_drag_insert (windowOpened +
/// processEvents populates m_states without needing a real LayoutManager).
void seedTrackedWindow(AutotileEngine& engine, const QString& windowId, const QString& screenId)
{
    engine.windowOpened(windowId, screenId);
    QCoreApplication::processEvents();
}

/// Real ScrollEngine with null deps (the headless pattern the router test
/// uses); claims @p screenId when @p screenIsScrolling.
std::unique_ptr<PhosphorScrollEngine::ScrollEngine> makeScrollEngine(bool screenIsScrolling, const QString& screenId)
{
    auto engine = std::make_unique<PhosphorScrollEngine::ScrollEngine>(nullptr, nullptr);
    if (screenIsScrolling) {
        engine->setActiveScreens(QSet<QString>{screenId});
    }
    return engine;
}

} // namespace

class TestDragPolicy : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─────────────────────────────────────────────────────────────────────
    // Canonical snap path — both screens normal, snap enabled, no disables.
    // Policy should stream, show overlay, grab keyboard, capture geometry.
    // bypassReason empty.
    // ─────────────────────────────────────────────────────────────────────
    void canonicalSnap_allFlagsSet()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        auto engine = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("DP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("DP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::None);
        QVERIFY(p.streamDragMoved);
        QVERIFY(p.showOverlay);
        QVERIFY(p.grabKeyboard);
        QVERIFY(p.captureGeometry);
        QVERIFY(!p.immediateFloatOnStart);
        QCOMPARE(p.screenId, QStringLiteral("DP-1"));
        // Producer/validator guard: computeDragPolicy must never return a
        // payload the effect-side validator would drop. Every branch below
        // mirrors this assertion.
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Autotile screen — engine owns placement, plugin bypasses the snap
    // path and (later in the drag flow) applies handleDragToFloat directly.
    // bypassReason = "autotile_screen". Policy captures geometry so the
    // free-floating size can be restored on unfloat.
    // ─────────────────────────────────────────────────────────────────────
    void autotileScreen_bypassAutotile()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        auto engine = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("HP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        QVERIFY(!p.streamDragMoved);
        QVERIFY(!p.showOverlay);
        QVERIFY(!p.grabKeyboard);
        QVERIFY(p.captureGeometry);
        // No windowOpened flowed through the engine, so isWindowTracked()
        // returns false and immediateFloatOnStart stays false. The
        // "tracked window → true" path is exercised indirectly by
        // integration tests that actually tile windows.
        QVERIFY(!p.immediateFloatOnStart);
        // AutotileScreen bypass requires non-empty screenId per the validator.
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Scrolling screen — the scrolling engine owns placement, so the drag
    // takes the same engine bypass as autotile (drag-to-float) instead of
    // falling through to the snap pipeline. The scroll branch ignores
    // reorderMode: the strip has no drag-insert preview, so a tracked
    // window always floats immediately. Uses the REAL ScrollEngine so the
    // branch is exercised against the production isActiveOnScreen /
    // isWindowTracked implementations, not a stand-in.
    // ─────────────────────────────────────────────────────────────────────
    void scrollingScreen_bypassesSnapPath()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        settings.m_dragBehavior = AutotileDragBehavior::Reorder;
        auto autotile = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("HP-1"));
        auto scroll = makeScrollEngine(/*screenIsScrolling=*/true, QStringLiteral("HP-1"));
        scroll->windowOpened(QStringLiteral("win-1"), QStringLiteral("HP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, autotile.get(), scroll.get(), QStringLiteral("win-1"), QStringLiteral("HP-1"), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        QVERIFY(!p.streamDragMoved);
        QVERIFY(!p.showOverlay);
        QVERIFY(!p.grabKeyboard);
        QVERIFY(p.captureGeometry);
        // Reorder mode does NOT clear the float for a scrolling screen — the
        // strip has no reorder preview.
        QVERIFY(p.immediateFloatOnStart);
        QVERIFY(p.validationError().isEmpty());
    }

    // Scroll branch, UNTRACKED window: still bypasses the snap path (the
    // screen belongs to the strip) but must not pre-float a window the
    // engine does not manage.
    void scrollingScreen_untrackedWindowDoesNotPreFloat()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        settings.m_dragBehavior = AutotileDragBehavior::Reorder;
        auto autotile = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("HP-1"));
        auto scroll = makeScrollEngine(/*screenIsScrolling=*/true, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, autotile.get(), scroll.get(), QStringLiteral("win-untracked"), QStringLiteral("HP-1"), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        QVERIFY(!p.immediateFloatOnStart);
        QVERIFY(p.validationError().isEmpty());
    }

    // The FALSE arm of the scroll branch's isActiveOnScreen guard: a
    // non-null scroll engine that does NOT claim the screen must leave the
    // canonical snap pipeline untouched — deleting the guard conjunct
    // would bypass snapping for every session with a scroll engine.
    void scrollEngineNotClaiming_keepsSnapPath()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        auto autotile = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("HP-1"));
        auto scroll = makeScrollEngine(/*screenIsScrolling=*/false, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, autotile.get(), scroll.get(), QStringLiteral("win-1"), QStringLiteral("HP-1"), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::None);
        QVERIFY(p.streamDragMoved);
        QVERIFY(p.showOverlay);
        QVERIFY(p.validationError().isEmpty());
    }

    // Empty screen id falls through to the snap check even while the
    // scroll engine claims a REAL screen.
    void scrollEngineClaiming_emptyScreenIdFallsThrough()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        auto autotile = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("HP-1"));
        auto scroll = makeScrollEngine(/*screenIsScrolling=*/true, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, autotile.get(), scroll.get(), QStringLiteral("win-1"), QString(), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::None);
        QVERIFY(p.validationError().isEmpty());
    }

    // The context-disable cascade beats the scroll branch just as it beats
    // the autotile one (contextDisabled_overridesAutotile is the twin).
    void contextDisabled_overridesScrolling()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        resolver.m_disabled = true;
        settings.m_snapEnabled = true;
        auto autotile = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("HP-1"));
        auto scroll = makeScrollEngine(/*screenIsScrolling=*/true, QStringLiteral("HP-1"));
        scroll->windowOpened(QStringLiteral("win-1"), QStringLiteral("HP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, autotile.get(), scroll.get(), QStringLiteral("win-1"), QStringLiteral("HP-1"), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::ContextDisabled);
        QVERIFY(!p.immediateFloatOnStart);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Both engines claim the screen (transition artefact): the autotile
    // branch is consulted first, so its reorder-mode behaviour wins — in
    // Reorder mode a tracked autotile drag keeps the window tiled for the
    // drag-insert preview (no immediate float), unlike the scroll branch.
    // ─────────────────────────────────────────────────────────────────────
    void bothEnginesClaim_autotileBranchWins()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        settings.m_dragBehavior = AutotileDragBehavior::Reorder;
        auto autotile = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-2"));
        auto scroll = makeScrollEngine(/*screenIsScrolling=*/true, QStringLiteral("HP-2"));
        seedTrackedWindow(*autotile, QStringLiteral("win-2"), QStringLiteral("HP-2"));
        scroll->windowOpened(QStringLiteral("win-2"), QStringLiteral("HP-2"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, autotile.get(), scroll.get(), QStringLiteral("win-2"), QStringLiteral("HP-2"), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        // The autotile Reorder branch keeps the window tiled for the
        // drag-insert preview; the scroll branch would have floated it.
        QVERIFY(!p.immediateFloatOnStart);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Snapping disabled on a normal screen. Dead drag — the user
    // configured snap mode off. bypassReason = "snapping_disabled".
    // Every flag false.
    // ─────────────────────────────────────────────────────────────────────
    void snapDisabled_onNormalScreen_bypass()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = false;
        auto engine = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("DP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("DP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::SnappingDisabled);
        QVERIFY(!p.streamDragMoved);
        QVERIFY(!p.showOverlay);
        QVERIFY(!p.grabKeyboard);
        QVERIFY(!p.captureGeometry);
        QVERIFY(!p.immediateFloatOnStart);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Snapping disabled BUT the drag is on an autotile screen. Autotile
    // takes precedence — the engine still owns placement regardless of the
    // snap-mode setting. bypassReason = "autotile_screen".
    //
    // This is the exact scenario from discussion #310: reporter had
    // snapping off and autotile on both monitors. Drags on autotile
    // screens should NOT be dead — they should float-on-drop.
    // ─────────────────────────────────────────────────────────────────────
    void autotileWithSnapDisabled_autotileWins()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = false;
        auto engine = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("HP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        QVERIFY(p.captureGeometry);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Monitor excluded in display settings. Dead drag regardless of snap
    // or autotile state — user told us to leave this monitor alone.
    // bypassReason = "context_disabled".
    // ─────────────────────────────────────────────────────────────────────
    void contextDisabled_overridesAutotile()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        resolver.m_disabled = true;
        settings.m_snapEnabled = true;
        // PolicyStubSettings no longer carries an isMonitorDisabled override
        // (removed in Pass 3 — the C1 cascade routes through the resolver).
        // A regression that reinstated `ISettings::isMonitorDisabled` as a
        // bypass source would have to add a new override here to be
        // exercised by the test; the test fails closed instead. The
        // bypass is unambiguously attributable to the resolver.
        auto engine = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("HP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::ContextDisabled);
        QVERIFY(!p.streamDragMoved);
        QVERIFY(!p.showOverlay);
        QVERIFY(!p.immediateFloatOnStart);
        QVERIFY(p.validationError().isEmpty());
    }

    void contextDisabled_overridesSnapDisabled()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        resolver.m_disabled = true;
        settings.m_snapEnabled = false;
        // See contextDisabled_overridesAutotile — PolicyStubSettings carries
        // no ISettings-side disable override (Pass 3 removed it), so the
        // resolver is the unambiguous source of the ContextDisabled bypass.
        // A regression that re-introduced an ISettings probe couldn't mask
        // itself behind a duplicated-flag here.
        auto engine = makeEngine(/*screenIsAutotile=*/false, QStringLiteral("DP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("DP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        // Context-disabled is checked before snapping_disabled — the reason
        // is stable at ContextDisabled even though either would produce
        // the same flag set.
        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::ContextDisabled);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Null dependencies — defensive path. When the daemon is mid-teardown
    // the adaptor may call in with nullptr settings / engine. Should
    // produce a canonical snap policy (no bypass reason) rather than
    // crashing, because there's no signal that a bypass applies.
    // ─────────────────────────────────────────────────────────────────────
    void nullDeps_returnsCanonicalSnap()
    {
        // Defensive shutdown path — every dep is null, including the
        // resolver. computeDragPolicy must still produce a canonical
        // snap policy without crashing.
        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            nullptr, nullptr, nullptr, QStringLiteral("win-1"), QStringLiteral("DP-1"), nullptr, false);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::None);
        QVERIFY(p.streamDragMoved);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Empty screenId — defensive. Should NOT match the autotile screen
    // (isAutotileScreen requires a valid id) and should NOT match
    // isContextDisabled. Falls through to snap-or-snap-disabled check.
    // ─────────────────────────────────────────────────────────────────────
    void emptyScreenId_fallsThroughToSnapCheck()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = false;
        auto engine = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-1"));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QString(), &resolver,
            settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        // Autotile check is skipped for empty screenId, so snapping_disabled wins.
        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::SnappingDisabled);
        // SnappingDisabled with empty screenId is explicitly tolerated by
        // the validator (see PhosphorProtocol::DragPolicy::validationError).
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // AutotileDragBehavior::Reorder on an autotile screen — the daemon owns
    // drag-insert preview for tile swapping, so the plugin must NOT float
    // the window on drag-start. Policy still uses bypassReason =
    // "autotile_screen" but immediateFloatOnStart must be cleared even
    // though the window is tracked, because the synchronous fast path and
    // the async reply handler both key on this flag to skip
    // handleDragToFloat.
    // ─────────────────────────────────────────────────────────────────────
    void reorderMode_clearsImmediateFloatOnStart()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        settings.m_dragBehavior = AutotileDragBehavior::Reorder;
        auto engine = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-1"));
        // Seed the dragged window as tracked so this test is not vacuous: in Float
        // mode a tracked window flips immediateFloatOnStart TRUE (see the paired
        // floatMode test), so asserting it stays FALSE here genuinely exercises the
        // Reorder-mode clear rather than the always-false untracked default.
        seedTrackedWindow(*engine, QStringLiteral("win-1"), QStringLiteral("HP-1"));
        QVERIFY(engine->isWindowTracked(QStringLiteral("win-1")));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("HP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        // The window IS tracked, yet Reorder mode must leave immediateFloatOnStart
        // cleared.
        QVERIFY(!p.immediateFloatOnStart);
        QVERIFY(p.captureGeometry);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Float mode (default) on an autotile screen — baseline that ensures
    // the reorder test above is actually catching a behavior difference
    // and not a default-null-engine artifact.
    // ─────────────────────────────────────────────────────────────────────
    void floatMode_allowsImmediateFloatOnStart()
    {
        PolicyStubSettings settings;
        FakeContextResolver resolver;
        settings.m_snapEnabled = true;
        settings.m_dragBehavior = AutotileDragBehavior::Float;
        auto engine = makeEngine(/*screenIsAutotile=*/true, QStringLiteral("HP-1"));
        // Same tracked seed as the reorder test — this is the positive baseline
        // proving the reorder test above catches a real difference: a tracked
        // window in Float mode DOES set immediateFloatOnStart.
        seedTrackedWindow(*engine, QStringLiteral("win-1"), QStringLiteral("HP-1"));
        QVERIFY(engine->isWindowTracked(QStringLiteral("win-1")));

        PhosphorProtocol::DragPolicy p = WindowDragAdaptor::computeDragPolicy(
            &settings, engine.get(), /*scrollEngine=*/nullptr, QStringLiteral("win-1"), QStringLiteral("HP-1"),
            &resolver, settings.m_dragBehavior == AutotileDragBehavior::Reorder);

        QCOMPARE(p.bypassReason, PhosphorProtocol::DragBypassReason::AutotileScreen);
        // Float mode + tracked window → immediateFloatOnStart set. This is the
        // behavior the paired reorder test proves is suppressed under Reorder.
        QVERIFY(p.immediateFloatOnStart);
        QVERIFY(p.captureGeometry);
        QVERIFY(p.validationError().isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Structural equality contract. PhosphorProtocol::DragPolicy::operator== (defaulted) is
    // the cross-VS flip trigger inside WindowDragAdaptor::updateDragCursor
    // — any field that differs between the in-force policy and a freshly
    // computed one must flip the comparator so dragPolicyChanged fires.
    //
    // This is the acceptance case the followup doc (removed alongside this
    // test) referenced: the comparator must not be blind to same-bypass-
    // reason transitions where another policy field differs.
    // ─────────────────────────────────────────────────────────────────────
    void operatorEquals_flipsOnAnyFieldChange()
    {
        PhosphorProtocol::DragPolicy base;
        base.streamDragMoved = true;
        base.showOverlay = true;
        base.grabKeyboard = true;
        base.captureGeometry = true;
        base.immediateFloatOnStart = false;
        base.screenId = QStringLiteral("DP-1");
        base.bypassReason = PhosphorProtocol::DragBypassReason::None;

        PhosphorProtocol::DragPolicy same = base;
        QVERIFY(same == base);

        // Cross-screen transition within the same bypass category — the
        // exact case the old bypassReason-only comparator missed. With
        // full-struct ==, screenId diff trips the flip.
        PhosphorProtocol::DragPolicy diffScreen = base;
        diffScreen.screenId = QStringLiteral("DP-2");
        QVERIFY(!(diffScreen == base));

        // Bypass-reason change still flips (regression guard).
        PhosphorProtocol::DragPolicy diffReason = base;
        diffReason.bypassReason = PhosphorProtocol::DragBypassReason::AutotileScreen;
        QVERIFY(!(diffReason == base));

        // Every routing flag participates — these cases exist so a future
        // refactor that replaces `= default` with a hand-written operator==
        // can't silently drop a field. If you add a field to PhosphorProtocol::DragPolicy, add
        // a case here too.
        PhosphorProtocol::DragPolicy diffOverlay = base;
        diffOverlay.showOverlay = false;
        QVERIFY(!(diffOverlay == base));

        PhosphorProtocol::DragPolicy diffStream = base;
        diffStream.streamDragMoved = false;
        QVERIFY(!(diffStream == base));

        PhosphorProtocol::DragPolicy diffKeyboard = base;
        diffKeyboard.grabKeyboard = false;
        QVERIFY(!(diffKeyboard == base));

        PhosphorProtocol::DragPolicy diffCapture = base;
        diffCapture.captureGeometry = false;
        QVERIFY(!(diffCapture == base));

        PhosphorProtocol::DragPolicy diffImmediateFloat = base;
        diffImmediateFloat.immediateFloatOnStart = true;
        QVERIFY(!(diffImmediateFloat == base));
    }
};

QTEST_MAIN(TestDragPolicy)
#include "test_drag_policy.moc"
