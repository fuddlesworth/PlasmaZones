// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file navigation_actions.cpp
 * @brief Snap-mode navigation entry points moved out of WindowTrackingAdaptor.
 *
 * Before the Phase 5 cleanup these methods lived on WindowTrackingAdaptor,
 * which had grown into a partial engine: target resolution, feedback
 * emission, bookkeeping, and zone-routing logic all sat in the D-Bus
 * facade class. That violated "the adaptor is a thin facade" and made
 * the daemon branch on mode at every shortcut handler.
 *
 * Navigation is now SnapEngine's concern. The entry points take a
 * NavigationContext {windowId, screenId} from the daemon's shortcut handler,
 * except switchFocusBetweenFloatingAndTiling(screenId) and
 * rotateWindowsInLayout(clockwise, screenId). The compositor-layer fallbacks
 * (the last-active window, the last-cursor then last-active screen, the frame
 * shadow) come through the typed INavigationStateProvider interface.
 *
 * Signals emitted by these methods are SnapEngine signals. The feedback/state
 * signals are relayed by SnapAdaptor to WindowTrackingAdaptor for D-Bus;
 * crossModeMoveRequested and crossModeSwapRequested go to
 * WindowTrackingAdaptor's handlers, and windowDesktopMoveRequested is relayed
 * unchanged to the effect over WindowTracking.
 */

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/LayerFocusSwitch.h>
#include <PhosphorEngine/WindowPlacementStore.h>

#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "snapenginelogging.h"
#include <PhosphorSnapEngine/snapnavigationtargets.h>

#include <algorithm>
#include <limits>

namespace PhosphorSnapEngine {

using PhosphorEngine::NavigationContext;
using PhosphorEngine::SnapIntent;
using PhosphorEngine::ZoneAssignmentEntry;

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Resolve the screen to use for a navigation operation on @p windowId.
///
/// Prefers (in order):
///   1. The stored screen assignment for the window (if currently snapped).
///      This keeps the operation on the monitor the user originally chose,
///      rather than the one KWin happens to think the window is on.
///   2. An explicit @p preferredScreen from the NavigationContext.
///   3. The INavigationStateProvider cursor / last-active screen values.
QString resolveNavScreen(INavigationStateProvider* navState, const QString& windowId,
                         PhosphorEngine::IWindowTrackingService* service, const QString& preferredScreen = QString())
{
    if (service && !windowId.isEmpty()) {
        const QString zoneId = service->zoneForWindow(windowId);
        if (!zoneId.isEmpty()) {
            // Shared validation rule — see isStoredScreenValid in
            // snapnavigationtargets.h for the virtual-vs-physical semantics.
            const QString storedScreen = service->screenForWindow(windowId);
            if (isStoredScreenValid(service->screenManager(), storedScreen)) {
                return storedScreen;
            }
        }
    }
    if (!preferredScreen.isEmpty()) {
        return preferredScreen;
    }
    if (!navState) {
        return QString();
    }
    QString screen = navState->lastCursorScreenName();
    if (screen.isEmpty()) {
        screen = navState->lastActiveScreenName();
    }
    return screen;
}

} // namespace

QString SnapEngine::navigationScreenFor(const QString& windowId, const QString& preferredScreen) const
{
    return resolveNavScreen(m_navState, windowId, m_windowTracker, preferredScreen);
}

namespace {

/// Pick the effective window id: the explicit one from NavigationContext
/// if set, otherwise the last-active window from INavigationStateProvider.
/// Returns empty when neither is available; callers emit their own no-window feedback.
QString effectiveWindowId(const NavigationContext& ctx, INavigationStateProvider* navState)
{
    if (!ctx.windowId.isEmpty()) {
        return ctx.windowId;
    }
    return navState ? navState->lastActiveWindowId() : QString();
}

/// Pick the effective screen id: the explicit one from NavigationContext
/// if set, otherwise the last-active screen from INavigationStateProvider.
QString effectiveScreenId(const NavigationContext& ctx, INavigationStateProvider* navState)
{
    if (!ctx.screenId.isEmpty()) {
        return ctx.screenId;
    }
    return navState ? navState->lastActiveScreenName() : QString();
}

} // namespace

void SnapEngine::setExcludeRuleSet(const PhosphorRules::RuleSet* ruleSet)
{
    if (m_excludeRuleSet == ruleSet) {
        return;
    }
    m_excludeRuleSet = ruleSet;
    // The cached evaluator binds a reference to the previously-pointed-at
    // rule set. Dropping it forces the next isAppIdExcluded call to rebind
    // against the new pointer — a held evaluator with a stale binding would
    // resolve against the WRONG store. The evaluator's per-revision
    // internal cache key off the bound rule set's revision counter, so an
    // in-place edit to the SAME pointer needs no reset here — only the
    // pointer-changed case does.
    m_excludeEvaluator.reset();
}

bool SnapEngine::evaluateExcludeRules(const PhosphorRules::WindowQuery& query) const
{
    // No-wiring fast path: early-init can run before the daemon hands the rule
    // store over; an empty set short-circuits with no evaluator allocation.
    if (!m_excludeRuleSet || m_excludeRuleSet->isEmpty()) {
        return false;
    }
    if (!m_excludeEvaluator) {
        m_excludeEvaluator.emplace(*m_excludeRuleSet);
    }
    // A rule on a field the query left unstamped is skipped (F881): a negated
    // leaf on it would otherwise exclude every window.
    return m_excludeEvaluator
        ->resolveFiltered(query,
                          m_exclusionAdmission ? m_exclusionAdmission(query)
                                               : std::function<bool(const PhosphorRules::Rule&)>{})
        .isExcluded();
}

bool SnapEngine::isAppIdExcluded(const QString& appId) const
{
    PhosphorRules::WindowQuery query;
    query.appId = appId;
    return evaluateExcludeRules(query);
}

bool SnapEngine::isWindowExcluded(const QString& windowId, const QString& screenHint) const
{
    return isWindowExcludedAt(windowId, screenHint, 0);
}

bool SnapEngine::isWindowExcludedAt(const QString& windowId, const QString& screenHint, int desktop) const
{
    // Build the richest query available: the daemon-supplied full attributes
    // (window class / title / frame size / flags) when the provider is wired,
    // else the appId-only query — the historical fallback unit tests rely on.
    std::optional<PhosphorRules::WindowQuery> query;
    if (m_exclusionQueryProvider) {
        query = m_exclusionQueryProvider(windowId, screenHint);
    }
    if (!query) {
        PhosphorRules::WindowQuery q;
        q.appId = m_windowTracker ? m_windowTracker->currentAppIdFor(windowId) : QString();
        query = std::move(q);
    }
    // The desktop a move lands on, asked before the window is there.
    if (desktop >= 1) {
        query->virtualDesktop = desktop;
    }

    // Minimum-window-size exclusion — only meaningful when the query carries the
    // frame size (full-query path). Mirrors the autotile eligibility filter, so
    // a sub-threshold utility window is excluded from auto-snap in both modes.
    if (auto* s = snapSettings()) {
        const int minW = s->minimumWindowWidth();
        const int minH = s->minimumWindowHeight();
        if ((minW > 0 && query->width && *query->width < minW)
            || (minH > 0 && query->height && *query->height < minH)) {
            return true;
        }
    }

    // Rule-based exclusion against the full query (no rules → nothing to match).
    return evaluateExcludeRules(*query);
}

bool SnapEngine::isWindowExcludedForAction(const QString& windowId, const QString& action, const QString& screenId)
{
    if (!m_windowTracker) {
        return false;
    }
    // @p screenId is the screen the verb acts on: the stored screen for a
    // window in a zone, the context screen otherwise, which is also where an
    // untracked window is. The landing context is asked separately, by the
    // resolver (cross-output) and tryCrossDesktopMove (cross-desktop).
    if (isWindowExcluded(windowId, screenId)) {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << action << ":" << windowId << "excluded by rule, appId:" << appId;
        // The appId stays in the log only: the fourth argument is the
        // source-zone-id slot, and smuggling a non-zone token through it
        // invites consumers to misparse it as a zone.
        Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), QString(), QString(), screenId);
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation entry points
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::focusInDirection:" << direction;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("focus"));
    if (!resolver) {
        return; // ensureTargetResolver emitted feedback
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::FocusTargetResult result = resolver->getFocusTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        // With no reachable entry zone on a neighbour output, the resolver
        // deferred the decision to us: a tiling neighbour's engine, then the
        // adjacent desktop.
        if (result.reason == QLatin1String("no_adjacent_zone")) {
            if (tryCrossModeFocus(direction, screenId) || tryCrossDesktopFocus(windowId, direction, screenId)) {
                return;
            }
            Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"), QString(),
                                      QString(), screenId);
        }
        return;
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

bool SnapEngine::tryCrossDesktopFocus(const QString& focusedWindowId, const QString& direction, const QString& screenId)
{
    // Only the late-bound cross-surface resolver is optional here.
    if (!m_crossSurfaceResolver) {
        return false;
    }
    const int targetDesktop =
        m_crossSurfaceResolver->neighborDesktopInDirection(currentVirtualDesktopForScreen(screenId), direction);
    if (targetDesktop <= 0) {
        return false;
    }
    // That desktop's own store in the current activity, on a snapping desktop
    // only: a tiling desktop's windows are another engine's (F225).
    const QString activity = currentActivity();
    if (m_layoutManager
        && m_layoutManager->modeForScreen(screenId, targetDesktop, activity)
            != PhosphorZones::AssignmentEntry::Snapping) {
        return false;
    }
    const SnapState* target =
        m_states.stateForKey(PhosphorEngine::PlacementStateKey{screenId, targetDesktop, activity});
    if (!target) {
        return false;
    }
    const QString self = canonicalWindowId(focusedWindowId); // F340
    const int shown = currentVirtualDesktopForScreen(screenId);
    // Already visible here: sticky, or also on the desktop in view (F146).
    const auto alsoOnDesktop = [this, shown](const QString& windowId) {
        const auto context = m_windowRegistry ? m_windowRegistry->desktopContext(windowId) : std::nullopt;
        if (!context) {
            return false;
        }
        if (context->sticky.value_or(false)) {
            return true;
        }
        const std::optional<QSet<int>> set = context->desktopSet();
        return set && set->contains(shown);
    };
    QHash<QString, int> numberOf; // zone id -> zone number in that desktop's layout
    if (auto* layout =
            m_layoutManager ? m_layoutManager->layoutForScreen(screenId, targetDesktop, activity) : nullptr) {
        for (const PhosphorZones::Zone* zone : layout->zones()) {
            numberOf.insert(zone->id().toString(), zone->zoneNumber());
        }
    }
    QList<std::pair<int, QString>> snapped;
    QStringList floats;
    for (const QString& windowId : target->windowsOnScreenAndDesktop(screenId, targetDesktop)) {
        if (windowId == self || !holdsWindowInState(windowId, target)) {
            continue;
        }
        if ((m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false))
            || alsoOnDesktop(windowId)) {
            continue;
        }
        if (target->isWindowSnapped(windowId) && !target->isFloating(windowId)) {
            snapped.append(
                {numberOf.value(target->zoneForWindow(windowId), std::numeric_limits<int>::max()), windowId});
        } else {
            floats.append(windowId);
        }
    }
    std::sort(snapped.begin(), snapped.end());
    floats.sort();
    QStringList pool;
    for (const auto& entry : std::as_const(snapped)) {
        pool.append(entry.second);
    }
    if (pool.isEmpty()) {
        pool = floats; // floats only when no snapped window is there (F387)
    }
    if (pool.isEmpty()) {
        return false;
    }
    // Enter at the first zone of that desktop's layout stepping forward and the
    // last stepping back, like autotile's first and last tile. Activating a
    // window on another desktop switches KWin to it.
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));
    Q_EMIT activateWindowRequested(forward ? pool.first() : pool.last());
    Q_EMIT navigationFeedback(true, QStringLiteral("focus"), QStringLiteral("desktop:") + direction, QString(),
                              QString(), screenId);
    return true;
}

void SnapEngine::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::moveFocusedInDirection:" << direction;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("move"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    // The screen the verb acts on: the exclusion test and the boundary feedback
    // both use it; geometry and landing feedback name the destination.
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    if (isWindowExcludedForAction(windowId, QStringLiteral("move"), screenId)) {
        return;
    }
    PhosphorProtocol::MoveTargetResult result = resolver->getMoveTargetForWindow(windowId, direction, screenId);
    // A neighbour where snapping is off takes the window unsnapped (F208).
    if (result.reason == QLatin1String("landing_disabled")) {
        moveUnsnapped(windowId, screenId, result.screenName, 0, direction);
        return;
    }
    if (!result.success) {
        // With no reachable entry zone on a neighbour output, the resolver
        // deferred the decision to us — try crossing to the adjacent desktop.
        if (result.reason == QLatin1String("no_adjacent_zone")) {
            // A neighbour OUTPUT in a tiling mode → hand the window to that engine.
            if (tryCrossModeOutput(windowId, direction, screenId, /*swap=*/false)) {
                return;
            }
            if (tryCrossDesktopMove(windowId, direction, screenId)) {
                return;
            }
            // No neighbour desktop either — emit the boundary feedback the
            // resolver left to us.
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"), QString(),
                                      QString(), screenId);
        }
        return;
    }
    commitUserSnap(windowId, {result.zoneId}, result.screenName, result.toRect(), screenId);
}

void SnapEngine::spanFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::spanFocusedInDirection:" << direction;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("span"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("span"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("span"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("span"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    // The screen the verb acts on: the exclusion test and the boundary feedback
    // both use it; geometry and landing feedback name the destination.
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    if (isWindowExcludedForAction(windowId, QStringLiteral("span"), screenId)) {
        return;
    }
    const SpanTargetResult result = resolver->getSpanTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        // Unlike move, a span boundary is a hard stop — a span is a set of
        // zones on ONE screen's layout, so there's no cross-output or
        // cross-desktop continuation. The resolver already emitted feedback.
        return;
    }
    commitUserSnap(windowId, result.zoneIds, result.screenName, result.geometry, screenId);
}

bool SnapEngine::tryCrossDesktopMove(const QString& windowId, const QString& direction, const QString& screenId)
{
    // As in tryCrossDesktopFocus, only the resolver is late-bound and optional.
    if (!m_crossSurfaceResolver) {
        return false;
    }
    const int targetDesktop =
        m_crossSurfaceResolver->neighborDesktopInDirection(currentVirtualDesktopForScreen(screenId), direction);
    if (targetDesktop <= 0) {
        return false;
    }
    // A window on every desktop has no next desktop, and the compositor refuses
    // to move it anyway (F289).
    if (m_windowRegistry) {
        if (const auto context = m_windowRegistry->desktopContext(windowId);
            context && context->sticky.value_or(false)) {
            return false;
        }
    }
    // Only snapped windows cross-desktop: this path is reached via the
    // no_adjacent_zone boundary, which requires a current zone. An unsnapped
    // window has nothing to carry — report no crossing so the caller emits the
    // boundary feedback instead of a phantom "moved" signal.
    const QString currentZoneId = zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        return false;
    }

    // If the target desktop on this screen is a DIFFERENT mode (autotile or
    // scrolling), snap has no zone to land in — hand the window to that engine
    // via the daemon cross-mode handoff, which inserts it into the target
    // desktop's stack or strip.
    //
    // The question is "is the target NOT snapping", not "is it autotile": for a
    // scrolling target, layoutForScreen answers defaultLayout() for any
    // non-Snapping entry, so the fall-through below WOULD resolve a zone id and
    // geometry and snap the window over a scroll-owned desktop, clobbering the
    // scroll engine's record via the placement store's mutual-exclusivity
    // invariant. This mirrors the neighbour-tiling gate installed in
    // SnapEngine::ensureTargetResolver, where testing for Autotile alone was
    // itself the shipped bug.
    if (m_layoutManager
        && m_layoutManager->modeForScreen(screenId, targetDesktop, currentActivity())
            != PhosphorZones::AssignmentEntry::Snapping) {
        // Deliberately do NOT touch SnapState / the placement store here: the
        // daemon's handleCrossModeMove resolves this engine as the source and
        // calls handoffRelease(windowId) on it, vacating the snap zone before the
        // autotile target receives the window. Re-stamping the desktop locally
        // would leave the window double-tracked (snapped here, tiled there) until
        // that release lands. The release is the source-of-truth uncommit.
        Q_EMIT crossModeMoveRequested(windowId, screenId, targetDesktop, direction);
        // Same success OSD as the snap-zone branches below — the daemon's
        // cross-mode handler relocates the window; it emits no feedback itself.
        Q_EMIT navigationFeedback(true, QStringLiteral("move"), QStringLiteral("desktop:") + direction, QString(),
                                  QString(), screenId);
        return true;
    }

    // The desktop it lands on is asked (F159, F208): a rule excluding the
    // window there refuses the move, and snapping switched off there takes the
    // window there unsnapped.
    if (isWindowExcludedAt(windowId, screenId, targetDesktop)) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("excluded"), QString(), QString(),
                                  screenId);
        return true;
    }
    if (!snapsInContext({screenId, targetDesktop, currentActivity()})) {
        moveUnsnapped(windowId, screenId, screenId, targetDesktop, direction);
        return true;
    }

    const QString activity = currentActivity();
    const PhosphorEngine::PlacementStateKey sourceKey = currentKeyForScreen(screenId);
    const PhosphorEngine::PlacementStateKey targetKey{screenId, targetDesktop, activity};
    const QString canonical = canonicalWindowId(windowId);
    const SnapState* const source = m_states.stateForKey(sourceKey);
    const SnapState* const there = m_states.stateForKey(targetKey);
    const bool member = there && holdsWindowInState(canonical, there);

    // Where it lands: a window already on that desktop keeps the zones it holds
    // there (F611), or its float there. Otherwise its slot is carried over, the
    // whole span on a shared layout and the first zone's position on another
    // (F355).
    const QStringList keptZones = member ? there->zonesForWindow(canonical) : QStringList{};
    const bool keepsFloat = member && keptZones.isEmpty() && there->isFloating(canonical);
    QStringList landing = keptZones;
    QRect rect;
    if (landing.isEmpty() && !keepsFloat) {
        const QStringList sourceZones = source ? source->zonesForWindow(canonical) : QStringList{currentZoneId};
        if (m_layoutManager && !sourceZones.isEmpty()
            && m_layoutManager->layoutForScreen(screenId, sourceKey.desktop, activity)
                == m_layoutManager->layoutForScreen(screenId, targetDesktop, activity)) {
            landing = sourceZones;
        } else if (const auto [zoneId, geometry] = resolveCrossDesktopZone(currentZoneId, screenId, targetDesktop);
                   !zoneId.isEmpty()) {
            landing = {zoneId};
            rect = geometry;
        }
    }
    if (!landing.isEmpty() && !rect.isValid() && m_windowTracker) {
        rect = m_windowTracker->resolveZoneGeometry(landing, screenId);
    }
    if (!keepsFloat && (landing.isEmpty() || !rect.isValid())) {
        // No slot there: only the compositor moves it. The membership pass then
        // decides its arrival as for KWin's own move, sending it back to its
        // pre-snap geometry or releasing it. Re-stamping the store here made
        // the zone it visibly holds read empty (F289).
        Q_EMIT windowDesktopMoveRequested(windowId, targetDesktop);
        Q_EMIT navigationFeedback(true, QStringLiteral("move"), QStringLiteral("desktop:") + direction, QString(),
                                  QString(), screenId);
        return true;
    }

    // A kept float goes back to its float-back now, while the window is still
    // visible: a free apply has no zone for the arrival to re-apply, and a
    // suspended client on a hidden desktop may not ack it.
    if (keepsFloat && m_windowTracker) {
        if (const auto back = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
            back && back->isValid()) {
            Q_EMIT applyGeometryRequested(windowId, back->x(), back->y(), back->width(), back->height(), QString(),
                                          screenId, false);
        }
    }
    // Its snap where it leaves goes silently, before the commit, so the record
    // and the effect's zone mirror name only the zones it lands in (F534, F458).
    {
        QStringList removed;
        bool lastUsedCleared = releaseMembership(windowId, sourceKey, removed);
        lastUsedCleared |= clearGlobalLastUsedIfRemoved(removed);
        if (lastUsedCleared && m_windowTracker) {
            m_windowTracker->markLastUsedZoneDirty();
        }
    }
    if (keepsFloat) {
        Q_EMIT windowSnapStateChanged(windowId,
                                      PhosphorProtocol::WindowStateEntry{windowId, QString(), QString(), false,
                                                                         QStringLiteral("unsnapped"), QStringList{},
                                                                         false});
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
    } else {
        // The commit states it snapped there and tells the tracking service
        // (F106, F534); a kept set is a re-statement.
        if (landing.size() > 1) {
            commitMultiZoneSnap(windowId, landing, screenId, PhosphorEngine::SnapIntent::UserInitiated, targetDesktop);
        } else {
            commitSnap(windowId, landing.first(), screenId, PhosphorEngine::SnapIntent::UserInitiated, targetDesktop);
        }
        if (m_windowTracker) {
            m_windowTracker->recordSnapIntent(windowId, true);
        }
    }
    if (m_windowTracker) {
        if (auto placement = capturePlacementAtDesktop(windowId, targetDesktop)) {
            placement->virtualDesktop = targetDesktop;
            m_windowTracker->placementStore().record(std::move(*placement));
        } else {
            qCDebug(PhosphorSnapEngine::lcSnapEngine) << "tryCrossDesktopMove: capturePlacement miss for" << windowId
                                                      << "— placement-store desktop not updated to" << targetDesktop;
        }
    }
    Q_EMIT windowDesktopMoveRequested(windowId, targetDesktop);
    // The target desktop is not in view, so its suspended client may not ack
    // the apply; the effect parks it and re-applies the zone when the desktop
    // is shown (F408, F491).
    if (!keepsFloat) {
        Q_EMIT applyGeometryRequested(windowId, rect.x(), rect.y(), rect.width(), rect.height(), landing.first(),
                                      screenId, false);
    }
    Q_EMIT navigationFeedback(true, QStringLiteral("move"), QStringLiteral("desktop:") + direction, QString(),
                              QString(), screenId);
    return true;
}

void SnapEngine::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::swapFocusedInDirection:" << direction;
    // The global-scalar holder is created by SnapEngine's ctor as a Qt-child, so
    // it (and the per-screen store map) is always live for a constructed engine.
    // Asserted unconditionally on entry (mirrors toggleFocusedFloat) so the
    // invariant fires regardless of which early-return path runs below, with
    // the release-build pair every other m_globals assert carries.
    Q_ASSERT(m_globals);
    if (!m_globals) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("swap"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    // The screen the verb acts on: the exclusion test and the boundary feedback
    // both use it; geometry and landing feedback name the destination.
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    if (isWindowExcludedForAction(windowId, QStringLiteral("swap"), screenId)) {
        return;
    }
    PhosphorProtocol::SwapTargetResult result = resolver->getSwapTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        // With no reachable entry zone on a neighbour output, the resolver deferred
        // to us. A cross-MONITOR swap onto a tiling neighbour is a two-way
        // exchange (both surfaces are visible). Swap is NOT extended across
        // virtual desktops — exchanging with a window on a desktop you can't see
        // is meaningless; use move to send a window to another desktop. So a
        // desktop-boundary swap simply reports the boundary.
        if (result.reason == QLatin1String("no_adjacent_zone")) {
            if (tryCrossModeOutput(windowId, direction, screenId, /*swap=*/true)) {
                return;
            }
            Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"), QString(),
                                      QString(), screenId);
        }
        return;
    }
    // A swap exchanges the two windows' whole zone sets (decision Q1): each
    // takes the other's zones where the other stood, read before any commit.
    // Into an empty zone it is a move, one zone. A store with no context for
    // the screen answers nothing, and the resolver's single zones stand.
    const auto zonesInView = [this](const QString& windowId, const QString& screen) {
        const SnapState* state = m_states.stateForKey(currentKeyForScreen(screen));
        return state ? state->zonesForWindow(canonicalWindowId(windowId)) : QStringList{};
    };
    QString partner = result.windowId2;
    if (!partner.isEmpty() && canonicalWindowId(partner) == canonicalWindowId(result.windowId1)) {
        partner.clear(); // never a swap with itself (F242)
    }
    const QStringList zones1Before = zonesInView(result.windowId1, screenId);
    const QStringList partnerZones = partner.isEmpty() ? QStringList{} : zonesInView(partner, result.screenName);
    const QStringList landing1 = partnerZones.isEmpty() ? QStringList{result.zoneId1} : partnerZones;
    const QRect rect1 = landing1.size() > 1 ? m_windowTracker->resolveZoneGeometry(landing1, result.screenName)
                                            : QRect(result.x1, result.y1, result.w1, result.h1);
    // No capture: the window is in a zone.
    commitUserSnap(result.windowId1, landing1, result.screenName, rect1, QString());

    if (!partner.isEmpty()) {
        // A cross-output swap sends window2 to the SOURCE output (screenName2),
        // not where it currently lives — its stored assignment is the neighbour
        // it's leaving. For an in-surface swap screenName2 is empty, so fall back
        // to its current assignment (then window1's screen) as before.
        QString screen2 = result.screenName2;
        if (screen2.isEmpty()) {
            // stateForWindow never returns null (untracked windows resolve
            // to the global holder, whose lookup yields an empty screen and
            // falls through to the screenName fallback below).
            screen2 = stateForWindow(partner)->screenForWindow(partner);
        }
        if (screen2.isEmpty()) {
            screen2 = result.screenName;
        }
        const QStringList landing2 = zones1Before.isEmpty() ? QStringList{result.zoneId2} : zones1Before;
        const QRect rect2 = landing2.size() > 1 ? m_windowTracker->resolveZoneGeometry(landing2, screen2)
                                                : QRect(result.x2, result.y2, result.w2, result.h2);
        if (landing2.size() > 1) {
            commitMultiZoneSnap(partner, landing2, screen2);
        } else {
            commitSnap(partner, landing2.first(), screen2);
        }
        m_windowTracker->recordSnapIntent(partner, true);
        // The partner is not the subject of the swap: a re-statement.
        Q_EMIT restatementGeometryRequested(partner, rect2.x(), rect2.y(), rect2.width(), rect2.height(),
                                            landing2.first(), screen2);
    }
}

void SnapEngine::moveFocusedToPosition(int zoneNumber, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::moveFocusedToPosition: zone" << zoneNumber << "screen" << ctx.screenId;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::moveFocusedToPosition: invalid zone number" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("snap"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    // The screen the verb acts on: the exclusion test and the boundary feedback
    // both use it; geometry and landing feedback name the destination.
    const QString effectiveScreen = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    if (isWindowExcludedForAction(windowId, QStringLiteral("snap"), effectiveScreen)) {
        return;
    }
    PhosphorProtocol::MoveTargetResult result =
        resolver->getSnapToZoneByNumberTarget(windowId, zoneNumber, effectiveScreen);
    if (!result.success) {
        return;
    }
    commitUserSnap(windowId, {result.zoneId}, effectiveScreen, result.toRect(), effectiveScreen);
}

void SnapEngine::pushFocusedToEmptyZone(const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::pushFocusedToEmptyZone: screen" << ctx.screenId;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("push"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    // The screen the verb acts on: the exclusion test and the boundary feedback
    // both use it; geometry and landing feedback name the destination.
    const QString effectiveScreen = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    if (isWindowExcludedForAction(windowId, QStringLiteral("push"), effectiveScreen)) {
        return;
    }
    PhosphorProtocol::MoveTargetResult result = resolver->getPushTargetForWindow(windowId, effectiveScreen);
    if (!result.success) {
        return;
    }
    commitUserSnap(windowId, {result.zoneId}, effectiveScreen, result.toRect(), effectiveScreen);
}

void SnapEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::restoreFocusedWindow";
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("restore"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    // Restore takes a window out of its zone; a floating or free window has
    // none to leave, and moving it to a float-back is not a restore (F190).
    if (zoneForWindow(windowId).isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return;
    }
    PhosphorProtocol::RestoreTargetResult result = resolver->getRestoreForWindow(windowId, screenId);
    if (!result.success) {
        return;
    }
    uncommitSnap(windowId);
    if (m_windowTracker) {
        // Screen-scoped: this restore consumes exactly one screen's
        // float-back, and the all-screens form destroyed the position
        // remembered for every OTHER monitor (the distinct-monitor memory
        // the store's collapse deliberately preserves).
        m_windowTracker->clearFreeGeometry(windowId, screenId);
    }
    // Snapping has only TWO states, so a window that just lost its snap must be
    // marked floating. Leaving it in neither made isWindowTracked answer false,
    // which made capturePlacement return nullopt, which left the STALE SNAPPED
    // record intact under the capture orchestrator's no-engine contract — the
    // window re-snapped to the zone it was just restored out of on next login.
    // The capture repeats here: uncommitSnap's ran while it was in neither (F353).
    stateForWindowOnScreen(windowId, screenId)
        ->setFloatingOnScreen(windowId, screenId, currentVirtualDesktopForScreen(screenId));
    if (auto placement = capturePlacement(windowId)) {
        m_windowTracker->placementStore().record(std::move(*placement));
    }
    Q_EMIT windowFloatingChanged(windowId, true, screenId);
    Q_EMIT applyGeometryRequested(windowId, result.x, result.y, result.width, result.height, QString(), screenId,
                                  false);
}

void SnapEngine::toggleFocusedFloat(const NavigationContext& ctx)
{
    Q_ASSERT(m_globals);
    if (!m_globals) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::toggleFocusedFloat";
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    const QString screenId = effectiveScreenId(ctx, m_navState);
    if (windowId.isEmpty() || screenId.isEmpty()) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "toggleFocusedFloat: no active window in context";
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_active_window"), QString(),
                                  QString(), screenId);
        return;
    }
    // An unfloat snaps the window back into a zone, which an exclusion rule
    // refuses like every other snapping verb (F243).
    if (isFloating(windowId) && isWindowExcludedForAction(windowId, QStringLiteral("float"), screenId)) {
        return;
    }

    // Dispatch to the IPlacementEngine toggle path (SnapEngine::toggleWindowFloat
    // lives in src/float.cpp). No need to route through WTA —
    // the router already ensured this screen is snap-mode.
    toggleWindowFloat(windowId, screenId);
}

void SnapEngine::switchFocusBetweenFloatingAndTiling(const QString& screenId)
{
    // Action "float": the OSD's float success arm renders layer copy from
    // the reason token ("Snapped" / "Floating"); the directional success
    // arm would render an arrow for a verb that has no direction.
    const QString action = QStringLiteral("float");
    QString screen = screenId;
    if (screen.isEmpty() && m_navState) {
        screen = m_navState->lastActiveScreenName();
    }
    if (screen.isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }
    // ensureStateForKey never returns null for a non-empty screen (a miss
    // lazily creates an empty store, bounded by screens × desktops ×
    // activities), so this refusal is belt-and-braces: a virgin screen
    // resolves an empty store and refuses below with no_target instead.
    SnapState* state = ensureStateForKey(currentKeyForScreen(screen));
    if (!state) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }

    // Whether the float layer holds focus is DERIVED from the live focus
    // (the navigation state provider's last-active window) rather than
    // stored: snap keeps no focus slot, and the provider is the same
    // compositor shadow every other snap verb trusts. Residence-only
    // windows are on neither layer, so a focused free window takes the
    // tiling→float leg — "give me a float" is the sensible answer there.
    const QString currentFocus = m_navState ? canonicalWindowId(m_navState->lastActiveWindowId()) : QString();
    const bool floatingHasFocus = !currentFocus.isEmpty() && state->isFloating(currentFocus);

    // Minimized windows are filtered on BOTH sides: the daemon models
    // minimize as a float, and the layer memories can name one.
    const auto isHidden = [this](const QString& id) {
        return m_windowRegistry && m_windowRegistry->minimizedState(id).value_or(false);
    };
    // Both pools come from the per-(screen,desktop,activity) store, but a
    // cross-desktop directional move re-stamps the window's desktop while
    // leaving it in its source store (reassignDesktop), so the raw pools can
    // name windows living on OTHER desktops. Activating one would yank the
    // user's desktop out from under the press — filter both sides to the
    // screen's current desktop. 0 means "on all desktops" per the KWin
    // convention buildOccupiedZoneSet documents, and an unstamped plain
    // float reads as 0 too.
    const int currentDesktop = currentVirtualDesktopForScreen(screen);
    const auto onCurrentDesktop = [state, currentDesktop](const QString& id) {
        if (currentDesktop == 0) {
            // No virtual-desktop authority resolves. In production the VDM
            // never answers 0 (an unknown screen falls back to the global
            // current desktop), so this branch serves reduced wirings with
            // no VDM at all (tests) — fail open like the minimize filter:
            // a focus verb must not refuse windows because the desktop is
            // unknowable.
            return true;
        }
        const int desktop = state->desktopForWindow(id);
        return desktop == 0 || desktop == currentDesktop;
    };
    PhosphorEngine::LayerSwitchSide snappedSide;
    snappedSide.candidate = state->lastSnappedFocus();
    snappedSide.fallbacks = state->snappedWindows();
    // snappedWindows() walks a hash — sort so the fallback pick is
    // deterministic, matching the sorted floating pool.
    std::sort(snappedSide.fallbacks.begin(), snappedSide.fallbacks.end());
    // The !isFloating term is defence in depth (every traced producer clears
    // the zone before setting the float bit), mirroring the autotile twin and
    // reapplyManagedWindowAppearance's guard.
    snappedSide.isEligible = [state, isHidden, onCurrentDesktop](const QString& id) {
        return state->isWindowSnapped(id) && !state->isFloating(id) && !isHidden(id) && onCurrentDesktop(id);
    };
    PhosphorEngine::LayerSwitchSide floatingSide;
    floatingSide.candidate = state->lastFloatingFocus();
    floatingSide.fallbacks = state->floatingWindows();
    floatingSide.isEligible = [state, isHidden, onCurrentDesktop](const QString& id) {
        return state->isFloating(id) && !isHidden(id) && onCurrentDesktop(id);
    };
    // The resolver reads focusForFeedback from the SOURCE side only, and the
    // source of this verb is always the live focus derived above — one value,
    // no per-leg distinction.
    snappedSide.focusForFeedback = currentFocus;
    floatingSide.focusForFeedback = currentFocus;

    // No eager bookkeeping precedes the activation (unlike scroll's echo
    // queue): snap derives the focus side live and windowFocused's memory
    // writes are side-effect free, so the compositor's answering report is
    // all the state this verb needs.
    auto result = PhosphorEngine::resolveLayerFocusSwitch(floatingHasFocus, snappedSide, floatingSide);
    if (result.success && result.toTiled) {
        // Snap's tiling side is the zone layer — say so in the OSD.
        result.reason = QStringLiteral("snapped");
    }
    announceLayerSwitch(result, action, screen);
}

void SnapEngine::cycleFocus(bool forward, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::cycleFocus: forward=" << forward;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("cycle"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::CycleTargetResult result = resolver->getCycleTargetForWindow(windowId, forward, screenId);
    if (!result.success) {
        return;
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    qCDebug(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::rotateWindowsInLayout: clockwise=" << clockwise << "screen=" << screenId;
    if (!m_windowTracker || !m_layoutManager) {
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), screenId);
        return;
    }
    // A named screen snapping does not run, or snapping switched off, rotates
    // nothing; refused silently with a log, as the shortcut is (F367).
    if (snappingSwitchedOff() || (!screenId.isEmpty() && !isActiveOnScreen(screenId))) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "rotateWindowsInLayout: snapping does not run on" << screenId;
        return;
    }
    QVector<ZoneAssignmentEntry> entries = calculateRotation(clockwise, screenId);
    if (entries.isEmpty()) {
        auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), screenId);
        } else if (layout->zoneCount() < 2) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"), QString(),
                                      QString(), screenId);
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"), QString(),
                                      QString(), screenId);
        }
        return;
    }

    // Apply the batch through WTS's unified helper. UserInitiated intent
    // preserves historical rotate semantics (each window's snap updates
    // last-used-zone). The fallback resolver queries the compositor-layer
    // cursor/active-screen shadows via INavigationStateProvider — only
    // used when none of the built-in strategies (targetScreenId /
    // geometry.center() / QGuiApplication::screens()) yield a screen.
    PhosphorProtocol::WindowGeometryList geometries =
        applyBatchAssignments(entries, SnapIntent::UserInitiated, [this]() -> QString {
            if (!m_navState) {
                return QString();
            }
            QString cursor = m_navState->lastCursorScreenName();
            if (cursor.isEmpty()) {
                cursor = m_navState->lastActiveScreenName();
            }
            return cursor;
        });
    if (!geometries.isEmpty()) {
        Q_EMIT applyGeometriesBatch(geometries, QStringLiteral("rotate"));
    }

    const QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
    const QString reason = QStringLiteral("%1:%2").arg(direction).arg(entries.size());
    Q_EMIT navigationFeedback(true, QStringLiteral("rotate"), reason, entries.first().sourceZoneId,
                              entries.first().targetZoneId, screenId);
}

// Note: resnapToNewLayout and the resnapCurrentAssignments overloads
// (screenFilter, onlyWindows, ResnapFeedback) live in src/navigation.cpp, beside
// emitBatchedResnap, the separate batch entry. All of them emit
// resnapToNewLayoutRequested, which SnapAdaptor's applyEngineResnap commits.

} // namespace PhosphorSnapEngine
