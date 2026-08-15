// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — rule-driven open/restore resolvers
//
// Per-window rule resolution for the open and restore paths: floated-position,
// zone, and size restore predicates, the open-float gate, placement-zone
// resolution, and screen/desktop open-routing.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "internal.h"

#include "dbus/zonedetectionadaptor.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/RuleStore.h>

#include <QJsonArray>

namespace PlasmaZones {

void WindowTrackingAdaptor::ensureRuleEvaluator()
{
    // Lazily construct against the store's live RuleSet. The evaluator binds a
    // const reference to RuleStore::m_ruleSet, which is a by-value member, so the
    // binding survives rule edits (the store mutates in place and bumps its own
    // revision, which is what the evaluator's cache is keyed on). Every caller
    // has already established m_ruleStore is non-null.
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
}

QString WindowTrackingAdaptor::resolveScreenForWindow(const QString& windowId) const
{
    // Mode-neutral: the service's accessor is snap-only (it reads the owning
    // SnapState), so on its own it returns empty for every autotile-tracked
    // window and for any window that has not been placed yet. Fall through to
    // each engine's own tracker, which is what lastActiveScreenName() already
    // does for the focused window — the difference is that this resolves the
    // screen of the window being ASKED about, never the focused one.
    //
    // Order is deliberate: the service first, so windows it already resolves
    // keep the canonicalizing lookup (issue #628 composite-id skew) and nothing
    // that resolves today changes. The engine fallbacks only fill cases that
    // previously came back empty.
    //
    // Note the asymmetry with lastActiveScreenName (queries.cpp), which skips the
    // service and starts at the snap engine's tracker, then falls back to a cached
    // screen id rather than to the service. The two answer different questions —
    // that one resolves the FOCUSED window and has a cache to fall back on, this
    // one resolves the window being asked about and has none — so they are not
    // trivially unifiable, and expressing one in terms of the other would change
    // behaviour rather than deduplicate it. The service lookup earns its place at
    // the front here because it is the accessor that canonicalizes the composite id
    // (WindowTrackingService::screenForWindow → snapForWindow → SnapState).
    if (m_service) {
        const QString fromService = m_service->screenForWindow(windowId);
        if (!fromService.isEmpty()) {
            return fromService;
        }
    }
    if (m_snapEngine) {
        const QString tracked = m_snapEngine->screenForTrackedWindow(windowId);
        if (!tracked.isEmpty()) {
            return tracked;
        }
    }
    if (m_autotileEngine) {
        const QString tracked = m_autotileEngine->screenForTrackedWindow(windowId);
        if (!tracked.isEmpty()) {
            return tracked;
        }
    }
    return QString();
}

std::optional<PhosphorRules::WindowQuery>
WindowTrackingAdaptor::buildContextualRuleQuery(const QString& windowId, const QString& screenIdHint) const
{
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return std::nullopt;
    }
    // Screen-derived context fields. WindowRegistry metadata carries no screen, so
    // ScreenId and ActiveLayout can only be stamped here: the open path knows the
    // screen the window is landing on (screenIdHint, which may be a route target
    // that differs from where the window currently sits), and every other path
    // reads the service's live screen-for-window.
    //
    // Stamping uniformly is load-bearing, not tidiness: the open-path consumers
    // share ONE RuleEvaluator::resolveCached entry keyed on (windowId, rule-set
    // revision), and resolveCached returns the cached actions WITHOUT consulting
    // the query on a hit — so whichever of them touches a window first seeds the
    // verdict the rest reuse for that window's lifetime. Three consumers stay off
    // that shared entry: shouldRestoreSizeOnUnsnap uses the uncached resolve(),
    // the exclusion provider feeds SnapEngine's own evaluator, and the predicates
    // wired into the AUTOTILE engine ask uncached (see below).
    //
    // Uniformity alone is NOT sufficient, because the hinted and unhinted paths
    // resolve different screens — the ORDER matters just as much. The
    // hint-bearing resolver must seed first, and does on both engines today:
    // SnapAdaptor::resolveWindowRestore calls applyOpenDesktopRouting
    // (snaprestore.cpp) before it enters the engine at all, seeding the entry with
    // the same screenId the engine's own placement / restore / float chain passes
    // afterwards, and AutotileAdaptor::dispatchWindowOpened runs
    // applyOpenRoutingForAutotile ahead of the tile engine's windowOpened.
    // Reordering either silently reverts ScreenId / ActiveLayout matching on the
    // open path with a green test suite.
    //
    // A sticky window under StickyWindowHandling::IgnoreAll makes
    // calculateSnapToPlacementRule early-return before it reaches the resolver,
    // which used to leave the unhinted restore predicate seeding first. On the
    // production path it no longer can: applyOpenDesktopRouting has already
    // seeded the entry with the hint before the engine is called. The gap is
    // confined to SnapEngine::windowOpened, a test-only entry point that reaches
    // the engine without the adaptor facade in front of it.
    const QString screenId = screenIdHint.isEmpty() ? resolveScreenForWindow(windowId) : screenIdHint;
    if (screenId.isEmpty()) {
        // No screen resolvable (window not placed yet and no hint) — leave both
        // fields as buildRuleQueryForWindow left them rather than stamping a
        // guess.
        //
        // Note what that does and does not buy: ScreenId and ActiveLayout are
        // non-optional context fields, so WindowQuery::valueForField returns them
        // ALWAYS ENGAGED (unlike the optional window fields, which report absent).
        // An unresolvable screen therefore compares an empty string, not "no
        // value" — `Equals <uuid>` correctly fails, but a NEGATED leaf
        // (`NotEquals` / `DoesNotContain`) matches. Rules cannot pair an empty
        // literal with Equals in the first place: MatchExpression::isValid rejects
        // an empty-Equals context-string leaf, which is what closes the
        // `Equals ""` trap. Genuine inertness would require making these optional
        // fields, a change spanning the effect, the daemon and the context cascade.
        return query;
    }
    query->screenId = screenId;
    if (!m_layoutManager) {
        return query;
    }
    // ActiveLayout is the layout active on that SCREEN right now — the id
    // assignmentIdForScreen resolves for the screen's current desktop and
    // activity, which is the same id the windowless context cascade stamps and
    // the same one the daemon publishes to the KWin effect. Keeping all three on
    // one definition is the point: `ActiveLayout Equals <uuid>` has to mean the
    // same thing whether it sits on a context rule, a daemon-resolved window
    // rule, or an effect-resolved appearance rule.
    //
    // Deliberately NOT the window's own desktop/activity. The effect resolves
    // this field from a per-SCREEN cache the daemon publishes for each screen's
    // current context and cannot do otherwise (it has no layout registry), so
    // stamping the window's context here would make the identical leaf match on
    // one side and not the other for any window sitting on a desktop other than
    // the one its screen is showing. The trade-off is explicit: a window on
    // desktop 2 whose screen currently shows desktop 1 matches desktop 1's
    // layout.
    //
    // The "screen's current desktop" and the activity come from the daemon's live
    // managers, not the registry's pushed mirrors. The mirrors are written from
    // one place each (the desktop-switch and activity-switch handlers), so between
    // a real switch and that handler running they lag, and this query would then
    // resolve the previous context's layout while diffActiveAssignments — which
    // reads the live managers — has already published the new one. `ActiveLayout`
    // only means one thing if every producer of it reads one source. Both
    // accessors fall back the same way the registry's did: VirtualDesktopManager
    // returns the global current desktop for a screen with no per-output value.
    const int desktop = currentDesktopForScreen(screenId);
    const QString activity = PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(m_activityManager);
    query->activeLayout = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);

    // ScreenOrientation was dead here for exactly the reason ActiveLayout was:
    // nothing stamped it on this side, and the effect resolves only APPEARANCE
    // actions, so it never sees an open-path rule at all. Float, Placement,
    // RouteToScreen, RouteToDesktop, RestorePosition, RestoreToZoneOnLogin and
    // RestoreSizeOnUnsnap are resolved here and nowhere else — a rule pairing one
    // of them with a ScreenOrientation leaf matched nothing, and (the field being
    // a non-optional context field) a negated leaf matched everything.
    m_layoutManager->stampScreenOrientation(*query, screenId);

    // Mode is deliberately left unstamped. WindowQuery documents it as the
    // window's live PLACEMENT state, empty for a window that is floating or not
    // placed at all (WindowQuery.h). These resolvers run on the open path, before
    // any engine has placed the window, so the honest answer here is "not known"
    // and the field stays as buildRuleQueryForWindow left it. The engine mode
    // assigned to the screen is a different question and is not what a `Mode`
    // leaf asks. The effect stamps this field from the live window for the
    // appearance rules it resolves.
    //
    // The trap that follows, and it is the same shape ScreenOrientation had before
    // it was stamped above: a rule pairing a `Mode` leaf with an OPEN-PATH action
    // (Float, SnapToZone, RouteToScreen, RouteToDesktop, RestorePosition,
    // RestoreToZoneOnLogin, RestoreSizeOnUnsnap) is resolved here and nowhere else,
    // so the leaf compares against a field nothing filled. Mode is a context field
    // and therefore always ENGAGED rather than absent, so it compares an empty
    // string: `Equals <mode>` never matches and the negated `NotEquals` form
    // matches every window. Only an APPEARANCE action makes a `Mode` leaf
    // meaningful, because only the effect stamps the field.
    return query;
}

bool WindowTrackingAdaptor::shouldRestoreFloatedPosition(const QString& windowId,
                                                         PhosphorZones::AssignmentEntry::Mode mode, bool useCache,
                                                         const QString& screenIdHint)
{
    // m_settings is a hard ctor dependency (qFatal on null), so it is non-null
    // here — deref unguarded like every other method in this class. The global
    // default is per-engine (snap-floated vs autotile-floated); the RestorePosition
    // rule override below is engine-neutral.
    const bool globalDefault = mode == PhosphorZones::AssignmentEntry::Mode::Autotile
        ? m_settings->autotileRestoreFloatedWindowsOnLogin()
        : m_settings->snappingRestoreFloatedWindowsOnLogin();

    // No rule store / metadata → the global setting is the whole policy.
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId, screenIdHint);
    if (!query) {
        return globalDefault;
    }

    ensureRuleEvaluator();
    // Shares m_ruleEvaluator with shouldFloatByRule; resolveCached is keyed on
    // (windowId, ruleSet revision) and ignores the query on a hit. Safe on the snap
    // engine, which consults this once per window lifetime on the open path, where
    // the effect has already pushed the window's full metadata. The autotile engine
    // also reaches it mid-session (insertWindow via backfillWindows), and passes
    // useCache=false so that resolve sees the current context — see shouldFloatByRule.
    // An uncached resolve builds its context from THIS query, so the autotile wiring
    // also hands over the insert screen as screenIdHint; without it the fresh query
    // would go out with ScreenId / ActiveLayout unstamped on the open path.
    const PhosphorRules::ResolvedActions resolved =
        useCache ? m_ruleEvaluator->resolveCached(windowId, *query) : m_ruleEvaluator->resolve(*query);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestorePosition))) {
        // A matched RestorePosition rule overrides the global setting.
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldRestoreToZoneOnLogin(const QString& windowId)
{
    // Mirror shouldRestoreFloatedPosition for the snapped-to-zone policy: a matched
    // SetRestoreToZoneOnLogin rule wins, otherwise the global setting decides.
    const bool globalDefault = m_settings->restoreWindowsToZonesOnLogin();
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId);
    if (!query) {
        return globalDefault;
    }
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestoreToZoneOnLogin))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldRestoreSizeOnUnsnap(const QString& windowId)
{
    // A matched SetRestoreSizeOnUnsnap rule wins, otherwise the global setting decides.
    const bool globalDefault = m_settings->restoreOriginalSizeOnUnsnap();
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId);
    if (!query) {
        return globalDefault;
    }
    ensureRuleEvaluator();
    // Unlike the open-path resolvers above, this fires MID-SESSION on every unsnap
    // (drag-out / drop / cursor-left-zones), long after the window opened. A fresh
    // uncached resolve is required: resolveCached is keyed on (windowId, ruleSet
    // revision) and returns the OPEN-TIME verdict on a hit, so a rule whose WHEN
    // references a property the registry refreshes mid-session (VirtualDesktop /
    // Activity, re-pushed on desktopsChanged / activitiesChanged) would resolve
    // stale. resolve() honours the freshly built query and does not pollute the
    // open-path cache. (Properties the effect does not re-push on a dedicated
    // maximize / geometry change — e.g. IsMaximized / width — are only as fresh as
    // the registry's last extended push, so resolve() reads that same value either
    // way: neutral, not stale, for those; a strict improvement for the refreshed
    // ones.)
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolve(*query);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestoreSizeOnUnsnap))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldFloatByRule(const QString& windowId, bool useCache, const QString& screenIdHint)
{
    // Float is purely rule-driven: there is no global "float on open" setting, so
    // absent a matching rule the answer is "do not float".
    if (!m_ruleStore) {
        return false;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId, screenIdHint);
    if (!query) {
        return false;
    }

    ensureRuleEvaluator();
    // resolveCached is keyed on (windowId, ruleSet revision); on a cache hit the
    // freshly built `query` is ignored. Window metadata staleness is not the risk:
    // windowId is lifetime-stable AND unique, so a reopened window gets a fresh
    // instanceId (new key → miss) and a mid-session appId rename changes the
    // composite key too. The screen-derived context is: the entry is not
    // invalidated on a layout, desktop or screen change, so a cached verdict
    // carries the ScreenId / ActiveLayout the window opened with.
    //
    // That is exactly right for the snap engine, which asks once on the open path.
    // The autotile engine asks again mid-session — insertShouldFloat runs from
    // insertWindow, which backfillWindows drives on an algorithm switch, a
    // maxWindows increase and an overflow-behaviour change, and the context seeder
    // calls it directly. Those callers pass useCache=false so the answer reflects
    // the layout assigned NOW rather than one that depends on whether some other
    // resolver happened to seed the entry first.
    //
    // They also pass the insert screen as screenIdHint. Skipping the cache means
    // the query built above is the one that gets resolved, and a hintless
    // resolveScreenForWindow consults the service and the snap engine BEFORE the
    // autotile engine — a window carrying stale snap state resolves to the SNAP
    // screen, not the screen it is landing on, and the consults reached from
    // context seeding can run before the engine has keyed the window at all. The
    // hint pins ScreenId / ActiveLayout to the landing screen in both cases.
    const PhosphorRules::ResolvedActions resolved =
        useCache ? m_ruleEvaluator->resolveCached(windowId, *query) : m_ruleEvaluator->resolve(*query);
    // The Float action carries free-form params (no Value key), so the verdict is
    // the PRESENCE of the filled slot, not a bool payload.
    return resolved.slot(QString(PhosphorRules::ActionSlot::Float)).has_value();
}

PhosphorSnapEngine::PlacementDirective WindowTrackingAdaptor::placementZonesByRule(const QString& windowId,
                                                                                   const QString& screenId)
{
    // Placement is purely rule-driven: absent a matching SnapToZone / RouteToScreen
    // rule there is nothing to snap or route, so the answer is empty.
    if (!m_ruleStore) {
        return {};
    }
    // Pin the query to the window's opening screen so a user-authored SnapToZone
    // rule carrying a `ScreenId` or `ActiveLayout` match resolves against the
    // screen the window is actually opening on, rather than the (not yet
    // assigned) screen the service would report for it. The settings
    // screen-picker stores the canonical id form the runtime reports, which is
    // what the open path hands us here.
    //
    // Two consequences of pinning the SPAWN screen, both inherent rather than
    // oversights. The verdict may itself carry RouteToScreen or RouteToDesktop, so
    // ActiveLayout and ScreenId here describe where the window came from, never the
    // route target — the route comes out of the same resolve, so the target cannot
    // be known before it. And because resolveCached is keyed on (windowId,
    // rule-set revision) and never invalidated on a layout, desktop or screen
    // change, this context is fixed for the window's lifetime.
    //
    // The contract that follows from that: the shared cached entry belongs to the
    // OPEN path, and every consumer of it is an open-path caller. A resolver that
    // also runs mid-session asks uncached instead, so its answer reads the current
    // context rather than one that depends on which caller seeded the entry first.
    // shouldRestoreSizeOnUnsnap does that, and so do the float and
    // restore-position predicates wired into the autotile engine, which
    // backfillWindows re-drives on algorithm and limit changes (see
    // enginewiring.cpp). Any new mid-session caller of these resolvers takes the
    // same route. The alternative, invalidating the evaluator on every assignment
    // change, would cost the open-path memoisation the whole open round trip
    // depends on.
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId, screenId);
    if (!query) {
        return {};
    }

    ensureRuleEvaluator();
    // Shares m_ruleEvaluator with shouldFloatByRule / shouldRestoreFloatedPosition;
    // resolveCached is keyed on (windowId, ruleSet revision) and returns every matched
    // slot, so reading the Placement slot off the same verdict is free. Same open-path
    // lifetime guarantee (resolved once per window lifetime) as the sibling predicates.
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    PhosphorSnapEngine::PlacementDirective directive;

    // RouteToScreen target (optional, independent of SnapToZone): pin the placement
    // to a specific monitor. A non-empty target moves the window there and resolves
    // its zone on that screen. The id is the canonical screen-id form the picker and
    // the runtime both use; the snap engine declines the route if the target is not
    // currently a snapping-mode screen, so an absent / autotile / disabled target is
    // safe here.
    if (const auto route = resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen))) {
        directive.targetScreenId = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    }

    // RouteToDesktop target (optional): when set, the zones resolve on this
    // desktop's layout and the snap commits in this desktop's context, so a
    // combined SnapToZone + RouteToDesktop rule lands the window in the right zone
    // of the destination desktop. The desktop MOVE itself is emitted separately by
    // applyOpenDesktopRouting (engine-neutral); this only steers the snap placement.
    if (const auto route = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
        const int desktop = route->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        if (desktop >= 1) {
            directive.targetDesktop = desktop;
        }
    }

    const std::optional<PhosphorRules::RuleAction> action =
        resolved.slot(QString(PhosphorRules::ActionSlot::Placement));
    if (!action) {
        // No SnapToZone: return the (possibly route-only) directive. With no ordinals
        // the snap engine treats it as "nothing to snap", so a RouteToScreen WITHOUT
        // an accompanying SnapToZone produces no snap here. The bare "move to monitor
        // X" is performed by applyOpenScreenRouting on the snap open-path facade (it
        // runs only when nothing snapped the window), not in this directive builder.
        return directive;
    }
    // The descriptor validator already guaranteed a non-empty array of in-range
    // 1-based ordinals at load; re-validate defensively against the SAME bound
    // (1..MaxZoneOrdinal) so a future loader change can never feed a bad ordinal
    // into zone resolution.
    const QJsonArray arr = action->params.value(QString(PhosphorRules::ActionParam::Zones)).toArray();
    directive.zoneOrdinals.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const int n = v.toInt(0);
        if (n >= 1 && n <= PhosphorRules::MaxZoneOrdinal) {
            directive.zoneOrdinals.append(n);
        }
    }
    return directive;
}

void WindowTrackingAdaptor::emitRouteToDesktopIfMatched(const PhosphorRules::ResolvedActions& resolved,
                                                        const QString& windowId)
{
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop));
    if (!route) {
        return;
    }
    // The descriptor validator already guaranteed a 1-based desktop in range; the
    // effect-side slot re-guards (rejects < 1, out-of-range, and sticky windows),
    // so moving to the desktop the window already occupies is a harmless no-op.
    const int desktop = route->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
    if (desktop >= 1) {
        qCInfo(lcDbusWindow) << "open-routing: routing" << windowId << "to virtual desktop" << desktop;
        Q_EMIT windowDesktopMoveRequested(windowId, desktop);
    }
}

void WindowTrackingAdaptor::applyOpenDesktopRouting(const QString& windowId, const QString& screenId)
{
    // Engine-neutral RouteToDesktop: when a matched rule pins the opening window
    // to a virtual desktop, ask the compositor to move it there. Independent of
    // snapping/tiling — the desktop move composes with the window's placement.
    // Called from the snap open-path facade (the autotile path uses
    // applyOpenRoutingForAutotile, which also handles the screen redirect).
    if (!m_ruleStore) {
        return;
    }
    // Pin the screen so a ScreenId / ActiveLayout-scoped rule resolves, mirroring
    // placementZonesByRule. resolveCached is keyed on windowId (+ rule-set revision), and
    // this is the SEEDER on the snap open path: SnapAdaptor::resolveWindowRestore calls
    // here (snaprestore.cpp) before it enters the engine, so placementZonesByRule reuses
    // this verdict rather than the other way round — no second evaluation.
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId, screenId);
    if (!query) {
        return;
    }
    ensureRuleEvaluator();
    emitRouteToDesktopIfMatched(m_ruleEvaluator->resolveCached(windowId, *query), windowId);
}

void WindowTrackingAdaptor::applyOpenScreenRouting(const QString& windowId, const QString& screenId)
{
    if (!m_ruleStore) {
        return;
    }
    // Pin the screen so a ScreenId / ActiveLayout-scoped rule resolves, mirroring
    // placementZonesByRule.
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId, screenId);
    if (!query) {
        return;
    }
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    // A SnapToZone action in the verdict does NOT disqualify the move, and used to.
    // The old guard returned whenever the Placement slot was filled, reasoning that
    // calculateSnapToPlacementRule resolves the zones ON the target screen and snaps
    // there, so this branch was unreachable for a route+snap rule and moving here
    // would double-place the window.
    //
    // Both halves of that were wrong. This function has exactly ONE caller,
    // SnapAdaptor::resolveWindowRestore, and it calls it only under
    // `if (!result.shouldSnap)` — so reaching here already means the snap did not
    // happen. calculateSnapToPlacementRule is pure calculation whose every decline
    // returns before any commit, so there is nothing placed to double-place. It
    // declines when the routed (screen, desktop) target is not in Snapping mode,
    // and when that target resolves no layout, no surviving ordinal, or a
    // degenerate union geometry.
    //
    // Nothing else picked the route up either. The most common decline is an
    // autotile-mode target, which the snap engine hands to "the autotile routing
    // hook" — but that hook runs from AutotileAdaptor::dispatchWindowOpened, and
    // the effect only calls windowOpened for windows already on an autotile screen
    // (autotilehandler.cpp: "Only notify autotile daemon for windows on autotile
    // screens"). So a window opening on a SNAPPING screen with a RouteToScreen onto
    // an autotile monitor reached neither path: it did not snap and it did not
    // move, while the same rule minus its SnapToZone action moved it fine.
    //
    // One case does then place twice, and is left alone deliberately. A window
    // spawning on an AUTOTILE screen with a SnapToZone + RouteToScreen rule aimed
    // at another autotile monitor takes both effect-side paths: window_lifecycle.cpp
    // runs callResolveWindowRestore first (this free move), then notifyWindowAdded
    // from its completion, which reaches applyOpenRoutingForAutotile and routes
    // plus tiles the window on the target. The free move is therefore transient,
    // and the window ends up correctly tiled either way.
    //
    // Suppressing the free move for that shape is NOT safe, because the autotile
    // leg is not guaranteed to follow. notifyWindowAdded declines for a window
    // isEligibleForAutotileNotify rejects and for one already in m_notifiedWindows
    // (a re-add), and applyOpenRoutingForAutotile itself declines when the target
    // is not in autotile mode on the ROUTED desktop rather than its current one.
    // Any of those with the move suppressed would strand the window on its spawn
    // screen — the exact failure the paragraph above describes. A transient extra
    // move is the cheaper side of that trade.
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return;
    }
    const QString target = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    if (target.isEmpty() || target == screenId) {
        return;
    }
    // m_service is non-null post-construction (class invariant); screenManager()
    // itself may still be null (e.g. an unconfigured test fixture), so guard that.
    PhosphorScreens::ScreenManager* screens = m_service->screenManager();
    if (!screens) {
        return;
    }
    const QRect dstAvail = screens->screenAvailableGeometry(target);
    if (!dstAvail.isValid()) {
        // Target monitor is not currently connected — leave the window on its spawn
        // screen (the rule fires again when that monitor returns).
        qCDebug(lcDbusWindow) << "applyOpenScreenRouting: route target" << target
                              << "is not currently connected — not moving" << windowId;
        return;
    }
    // The daemon's frame-geometry shadow is written only by the effect's DEBOUNCED
    // flush, while this runs inside the synchronous window-open round trip — so
    // for a genuinely new window the shadow is usually still empty here, and
    // returning on that would make a bare RouteToScreen rule silently no-op for
    // exactly the case it exists to handle. The registry metadata carries the
    // frame rect and is pushed by the setWindowMetadata call that precedes the
    // restore, so fall back to it before declining.
    QRect cur = frameGeometry(windowId);
    if (!cur.isValid() && !m_windowRegistry.isNull()) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
        if (const std::optional<PhosphorEngine::WindowMetadata> meta = m_windowRegistry->metadata(instanceId)) {
            if (meta->positionX && meta->positionY && meta->width && meta->height && *meta->width > 0
                && *meta->height > 0) {
                cur = QRect(*meta->positionX, *meta->positionY, *meta->width, *meta->height);
            }
        }
    }
    if (!cur.isValid()) {
        // Neither the shadow nor the metadata knows where the window is — nothing
        // to translate onto the target screen.
        qCDebug(lcDbusWindow) << "applyOpenScreenRouting: no frame geometry for" << windowId << "— not moving";
        return;
    }

    // Map the window's position relative to its current screen's available area onto
    // the target screen's, then clamp so the whole frame fits. Size is preserved
    // only where it fits: a route onto a SMALLER monitor shrinks the frame to the
    // target's available area rather than overflowing it. Preserves "the same spot
    // on the other monitor" across differing resolutions; an unknown / degenerate
    // source area falls back to the target's top-left.
    const QRect srcAvail = screens->screenAvailableGeometry(screenId);
    const int w = qMin(cur.width(), dstAvail.width());
    const int h = qMin(cur.height(), dstAvail.height());
    int x = dstAvail.x();
    int y = dstAvail.y();
    if (srcAvail.isValid() && srcAvail.width() > 0 && srcAvail.height() > 0) {
        const double relX = static_cast<double>(cur.x() - srcAvail.x()) / srcAvail.width();
        const double relY = static_cast<double>(cur.y() - srcAvail.y()) / srcAvail.height();
        x = dstAvail.x() + qRound(relX * dstAvail.width());
        y = dstAvail.y() + qRound(relY * dstAvail.height());
    }
    // Clamp the frame fully inside the target available area.
    x = qBound(dstAvail.left(), x, dstAvail.right() - w + 1);
    y = qBound(dstAvail.top(), y, dstAvail.bottom() - h + 1);

    qCInfo(lcDbusWindow) << "applyOpenScreenRouting: routing" << windowId << "to monitor" << target << "at"
                         << QRect(x, y, w, h);
    // Emit the marker first so the effect treats the resulting outputChanged as an
    // expected daemon-driven move (bookkeeping + decoration only, no reopen), then
    // the free placement (empty zone id ⇒ no snap chrome).
    Q_EMIT windowOutputMoveExpected(windowId, target);
    Q_EMIT applyGeometryRequested(windowId, x, y, w, h, QString(), target, false);
}

QString WindowTrackingAdaptor::applyOpenRoutingForAutotile(const QString& windowId, const QString& screenId)
{
    if (!m_ruleStore) {
        return QString();
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildContextualRuleQuery(windowId, screenId);
    if (!query) {
        return QString();
    }
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    // RouteToDesktop is engine-neutral — emit it for autotile windows too.
    emitRouteToDesktopIfMatched(resolved, windowId);

    // RouteToScreen: redirect the window onto a different AUTOTILE monitor. The
    // snap open path handles snap-mode targets itself (the placement directive),
    // so here we only honour a target that is currently in autotile mode — a snap
    // or disabled target is left to the window's spawn screen (cross-engine
    // routing is out of scope). Returning the target tells the caller to insert
    // the window into that screen's tiling state; the output-move marker stops the
    // effect from re-processing the resulting outputChanged as a fresh open.
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return QString();
    }
    const QString target = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    if (target.isEmpty() || target == screenId || !m_layoutManager) {
        return QString();
    }
    // When the same rule also pins a target desktop (RouteToDesktop), the window
    // lands on THAT desktop of the target screen, so gate the autotile-mode check
    // against the destination desktop — not the target's current desktop. Mirrors
    // the snap path (calculateSnapToPlacementRule), which gates modeForScreen on the
    // routed desktop. Absent / 0 ⇒ the target screen's current desktop.
    int destDesktop = currentDesktopForScreen(target);
    if (const auto desktopRoute = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
        const int d = desktopRoute->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        if (d >= 1) {
            destDesktop = d;
        }
    }
    // Live ActivityManager, not the registry's pushed mirror — the same source
    // buildContextualRuleQuery reads for ActiveLayout, and for the same reason: the
    // mirror is written from the activity-switch handler alone, so between a real
    // switch and that handler running it names the previous activity and this gate
    // would read the wrong screen's mode. Scope note: this file's two readers are
    // converted; other daemon consumers (ScreenModeRouter::modeFor, the
    // persistence restore path) still read the pushed mirrors, so no file-wide
    // coherence is claimed here.
    if (m_layoutManager->modeForScreen(target, destDesktop,
                                       PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(m_activityManager))
        != PhosphorZones::AssignmentEntry::Mode::Autotile) {
        qCDebug(lcDbusWindow) << "applyOpenRoutingForAutotile: RouteToScreen target" << target
                              << "is not in autotile mode — not redirecting" << windowId;
        return QString();
    }
    qCInfo(lcDbusWindow) << "applyOpenRoutingForAutotile: routing" << windowId << "to autotile screen" << target;
    Q_EMIT windowOutputMoveExpected(windowId, target);
    return target;
}

} // namespace PlasmaZones
