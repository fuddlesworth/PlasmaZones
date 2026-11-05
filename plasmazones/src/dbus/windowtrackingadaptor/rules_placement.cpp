// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — rule-driven open placement and routing
//
// The open-path half of the rule resolvers: the SnapToZone placement directive
// (zone ordinals and names, re-validated by one shared reader), the
// RouteToScreen / RouteToDesktop open-routing on the snap and tiling channels,
// and the "does a rule own this window's target" verdict both channels share.
// The restore predicates, stampers and per-window param builders live in
// rules.cpp; the admission tests both TUs share are in rules_admission.h.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "rules_admission.h"

#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSnapEngine/PlacementDirective.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorRules/WindowQuery.h>

#include <QJsonArray>

#include <optional>

namespace PlasmaZones {

using namespace RuleAdmission;

namespace {

/// The SnapToZone targets of a Placement-slot action, re-validated against the
/// SAME bounds the descriptor validator applies at load (1..MaxZoneOrdinal for
/// ordinals; non-blank, at most MaxZoneNameLength for names) so a future loader
/// change can never feed a bad target into zone resolution. Names are trimmed
/// here once so the engine's lookup and the ownership predicate agree on what
/// counts as a name. The single reader for both params: the directive builder
/// and the two "does a rule own this window" predicates all go through it, so
/// the validity rule cannot drift between them.
struct PlacementTargets
{
    QList<int> ordinals;
    QStringList names;
    bool any() const
    {
        return !ordinals.isEmpty() || !names.isEmpty();
    }
};

PlacementTargets placementTargetsOf(const PhosphorRules::RuleAction& action)
{
    PlacementTargets targets;
    const QJsonArray ordinals = action.params.value(QString(PhosphorRules::ActionParam::Zones)).toArray();
    targets.ordinals.reserve(ordinals.size());
    for (const QJsonValue& v : ordinals) {
        if (!v.isDouble()) {
            continue;
        }
        // toInt(0) answers 0 for a non-integral double, which the range test
        // below rejects — the same verdict the load validator's integrality
        // check gives.
        const int n = v.toInt(0);
        if (n >= 1 && n <= PhosphorRules::MaxZoneOrdinal) {
            targets.ordinals.append(n);
        }
    }
    const QJsonArray names = action.params.value(QString(PhosphorRules::ActionParam::ZoneNames)).toArray();
    targets.names.reserve(names.size());
    for (const QJsonValue& v : names) {
        if (!v.isString()) {
            continue;
        }
        const QString name = v.toString().trimmed();
        if (!name.isEmpty() && name.size() <= PhosphorRules::MaxZoneNameLength) {
            targets.names.append(name);
        }
    }
    return targets;
}

/// Whether @p resolved carries a Placement-slot action with at least one valid
/// SnapToZone target. Gates on a VALID target, not the slot's mere presence:
/// placementTargetsOf drops out-of-range ordinals and blank names, and an
/// all-rejected SnapToZone payload owns nothing.
///
/// Validity is a PAYLOAD-SHAPE judgement, deliberately not a "resolves in the
/// active layout" one: a rule whose ordinals or names name no zone in the
/// layout the window lands on still OWNS the window's target (the engine
/// declines the snap and the remembered-placement fallback stays suppressed),
/// exactly as an ordinal beyond the layout's zone count always has. A name is
/// likelier than a low ordinal to be absent from a given layout, so a
/// names-only rule authored for one layout leaves the window wherever it
/// spawned on a layout without that name. Resolving against the layout here
/// would need the placement (screen, desktop) the engine is about to choose and
/// would make ownership flip with the layout switch; the span rule is
/// layout-agnostic by contract, so ownership follows the rule, not the layout.
bool hasValidPlacementTarget(const PhosphorRules::ResolvedActions& resolved)
{
    const auto placement = resolved.slot(QString(PhosphorRules::ActionSlot::Placement));
    return placement && placementTargetsOf(*placement).any();
}

} // namespace

PhosphorSnapEngine::PlacementDirective WindowTrackingAdaptor::placementZonesByRule(const QString& windowId,
                                                                                   const QString& screenId)
{
    // Placement is purely rule-driven: absent a matching SnapToZone / RouteToScreen
    // rule there is nothing to snap or route, so the answer is empty.
    if (!m_ruleStore) {
        return {};
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return {};
    }
    // Pin the query to the window's opening screen so a user-authored SnapToZone
    // rule carrying a `ScreenId` match (the settings screen-picker stores the
    // canonical id form the runtime reports) resolves against the screen the
    // window is actually opening on. buildRuleQueryForWindow leaves screenId empty
    // (the sibling Float / RestorePosition resolvers do not have the screen), so
    // the placement path is the one consumer that pins it.
    //
    // The open and snapToAppRule entries guarantee a non-empty screen, but the
    // unfloat path (SnapEngine::unfloatToZone, reachable from the two-argument
    // Snap.setWindowFloat D-Bus slot) documents an empty screen as one of its
    // tiers. Resolve it the way shouldFloatByRule does rather than stamping
    // nothing: an unstamped screen would leave ScreenId, ActiveLayout and the
    // screen-shape fields unanswered under an admission that still admits
    // rules referencing them, so a `None{ScreenId Equals X}` group would invert.
    const QString placementScreen = screenId.isEmpty() ? resolveScreenForWindow(windowId) : screenId;
    stampScreenContext(*query, placementScreen);

    ensureRuleEvaluator();
    // Shares m_ruleEvaluator with shouldFloatByRule / shouldRestoreFloatedPosition.
    // The shared evaluator cache is keyed on (windowId, ruleSet revision) only, so
    // the FIRST resolver to touch a window seeds the verdict the others reuse, and
    // that seed must come from a screen-stamped query. On a first placement down
    // the snap open path the seeder is applyOpenDesktopRouting, which
    // SnapAdaptor::resolveWindowRestore runs before the engine's
    // SnapEngine::resolveWindowRestore reaches here. When nothing has seeded it
    // yet (a restore that is no first placement, so no routing ran), this
    // resolve does, through the cache, and resolveWindowRestore consults the
    // float / restore predicates only after it, so a screen-constrained rule
    // resolves correctly for them too.
    //
    // When the memo already holds a verdict, this resolves FRESH instead of
    // reading it. The memo is the window's open-time verdict, and this resolver
    // also runs mid-session, for the Meta+F unfloat's rule tier and the
    // Snap.snapToAppRule slot: reading it there would answer with the screen
    // shape, active layout and desktop the window opened under, after a monitor
    // rotated, changed mode or switched layout. The fresh resolve leaves the
    // memo alone (clearing it would hand the next seed to an unstamped
    // predicate, the hazard invalidateRuleMemosForColorSchemeChange documents),
    // and on the open path it reads the same screen in the same turn as the
    // seed, so it answers the same.
    //
    // The degenerate no-screen case must NOT go through the memo either: every
    // cached caller passes admitScreenStamped (resolveCachedFiltered's
    // equivalent-admit precondition), and an unstamped verdict written under that
    // key would be read back by the restore predicates as if a screen had been
    // stamped. It resolves under the admission its stamps actually warrant, the
    // same shape shouldRestoreFloatedPosition's uncached branch uses.
    const bool seedMemo = !query->screenId.isEmpty() && !m_ruleEvaluator->resolveCachedIfPresent(windowId);
    const PhosphorRules::ResolvedActions resolved = seedMemo
        ? m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitWith(&admitScreenStamped, *query))
        : m_ruleEvaluator->resolveFiltered(*query, admitWith(admissionForStamped(*query), *query));

    PhosphorSnapEngine::PlacementDirective directive;

    // RouteToScreen target (optional, independent of SnapToZone): pin the placement
    // to a specific monitor. A non-empty target moves the window there and resolves
    // its zone on that screen. The id is the canonical screen-id form the picker and
    // the runtime both use; the snap engine declines the route if the target is not
    // currently a snapping-mode screen, so an absent / autotile / disabled target is
    // safe here.
    //
    // TAKEN, not read: this builder also serves SnapEngine::unfloatToZone, an
    // arbitrarily-later non-open path, so the routed answer must not outlive
    // the pass that produced it (see m_workspaceRoutedDesktop).
    const int routedWorkspaceDesktop = m_workspaceRoutedDesktop.take(windowId);
    const QString routedWorkspaceScreen = m_workspaceRoutedScreen.take(windowId);

    if (const auto route = resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen))) {
        // Trimmed at the read site: the loader validates the TRIMMED string but
        // stores the param verbatim, and ScreenIdentity::screensMatch does no
        // trimming of its own — so a padded id passes validation and then
        // matches no monitor at all.
        directive.targetScreenId =
            route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString().trimmed();
    } else if (!routedWorkspaceScreen.isEmpty()) {
        // No explicit RouteToScreen, but the realized workspace lives on a
        // monitor of its own and the window is being moved there. Resolve the
        // zones on THAT screen: the daemon's registry still reports the spawn
        // output (the output move is in flight over D-Bus), so an unpinned
        // directive would snap the window into a zone of the screen it is
        // leaving. This is the routed screen the placement is pinned to, not a
        // second move — the snap apply path and the output leg name the same
        // monitor.
        directive.targetScreenId = routedWorkspaceScreen;
    }

    // RouteToDesktop target (optional): when set, the zones resolve on this
    // desktop's layout and the snap commits in this desktop's context, so a
    // combined SnapToZone + RouteToDesktop rule lands the window in the right zone
    // of the destination desktop. The desktop MOVE itself is emitted separately by
    // applyOpenDesktopRouting (engine-neutral); this only steers the snap placement.
    //
    // A RouteToWorkspace realized on this window OUTRANKS the number, matching
    // the precedence emitOpenRoutingIfMatched already applies when it issues
    // the move: the name is the stronger identity, and the desktop it resolved
    // to is the one the window is actually landing on. The routing pass runs
    // first on this path (SnapAdaptor::resolveWindowRestore calls
    // applyOpenDesktopRouting before the engine's restore), so the answer is
    // already stashed by the time we read it (taken above, with the screen).
    if (routedWorkspaceDesktop >= 1) {
        directive.targetDesktop = routedWorkspaceDesktop;
    } else if (const auto route = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
        const int desktop = route->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        if (desktop >= 1) {
            directive.targetDesktop = desktop;
        }
    }

    const std::optional<PhosphorRules::RuleAction> action =
        resolved.slot(QString(PhosphorRules::ActionSlot::Placement));
    if (!action) {
        // No SnapToZone: return the (possibly route-only) directive. With no
        // targets the snap engine treats it as "nothing to snap", so a
        // RouteToScreen WITHOUT an accompanying SnapToZone produces no snap here.
        // The bare "move to monitor X" is performed by applyOpenScreenRouting on
        // the snap open-path facade (it runs only when nothing snapped the
        // window), not in this directive builder.
        return directive;
    }
    // Ordinals and names, re-validated by the shared reader (see placementTargetsOf).
    PlacementTargets targets = placementTargetsOf(*action);
    directive.zoneOrdinals = std::move(targets.ordinals);
    directive.zoneNames = std::move(targets.names);
    return directive;
}

bool WindowTrackingAdaptor::emitOpenRoutingIfMatched(const PhosphorRules::ResolvedActions& resolved,
                                                     const QString& windowId)
{
    // Named-workspace routing outranks the positional desktop number when a
    // cascade carries both: the name is the stronger identity (it survives
    // renumbering and monitor moves), and a rule specific enough to name a
    // workspace beats a broad rule pinning a number. The daemon's workspace
    // wiring resolves the name against the live declarations and no-ops an
    // undeclared one — in that case the positional route below still applies.
    const std::optional<PhosphorRules::RuleAction> workspaceRoute =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteWorkspace));
    // Cleared unconditionally first: this hash is read back later in the SAME
    // open round trip, and a pass that routes nothing must not leave the
    // previous open's answer standing for a reused window id.
    m_workspaceRoutedDesktop.remove(windowId);
    m_workspaceRoutedScreen.remove(windowId);
    // A RouteToScreen in the same cascade OWNS the window's monitor: the snap
    // twin (applyOpenScreenRouting) and the tiling twin
    // (applyOpenRoutingForTiling) both act on it, so asking the workspace route
    // for an output move as well would be two contradictory moves. Without one
    // in the cascade the workspace route is the only thing that can put the
    // window on the monitor whose slice owns the destination desktop.
    const bool cascadeOwnsScreen = resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen)).has_value();
    if (workspaceRoute && m_workspaceRouteResolver) {
        // Trimmed at the read site: the daemon's declaration list stores each
        // name trimmed (workspaces.cpp trims before binding), so an untrimmed
        // rule param would never match a declared workspace and would silently
        // fall through to the positional route.
        const QString name =
            workspaceRoute->params.value(QString(PhosphorRules::ActionParam::TargetWorkspaceName)).toString().trimmed();
        if (!name.isEmpty()) {
            // > 0 is the realized desktop; the placement-context builders read
            // it back so a combined SnapToZone (or tiling) + RouteToWorkspace
            // rule resolves in the DESTINATION desktop rather than the one the
            // window is leaving.
            QString ownerScreen;
            const int routedDesktop = m_workspaceRouteResolver(name, windowId, !cascadeOwnsScreen, &ownerScreen);
            if (routedDesktop > 0) {
                m_workspaceRoutedDesktop.insert(windowId, routedDesktop);
                // Stashed even when a RouteToScreen owns the monitor: the
                // placement builders prefer that action's explicit target, so
                // the owner screen simply goes unread on that path.
                if (!ownerScreen.isEmpty()) {
                    m_workspaceRoutedScreen.insert(windowId, ownerScreen);
                }
                qCInfo(lcDbusWindow) << "open-routing: routed" << windowId << "to named workspace" << name << "(desktop"
                                     << routedDesktop << "on" << ownerScreen << ")";
                return true;
            }
            if (routedDesktop < 0) {
                // Declared but momentarily unresolvable. The positional
                // RouteToDesktop below is the author's fallback for a name
                // this session does not HAVE, not for one that is briefly
                // unreadable, so applying it here would silently land the
                // window on a different desktop for the duration of a
                // transient reconciler op. Leave the window where it spawned.
                qCInfo(lcDbusWindow) << "open-routing: named workspace" << name
                                     << "is declared but not resolvable right now; leaving" << windowId
                                     << "on its spawn desktop";
                // MATCHED all the same, on the rule established below: the
                // answer says a route rule named this window, not that the
                // move succeeded.
                return true;
            }
        }
    }

    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop));
    if (!route) {
        return false;
    }
    // MATCHED is the answer this function gives, and it does not depend on the
    // target being usable: a rule that named this window's desktop matched
    // whether or not its payload survived the guard below. Hence one return,
    // with the emit conditional inside. No production caller reads the answer
    // today; the routing tests are its consumer.
    //
    // The read is deliberately defensive even though stored rules cannot reach
    // the else. Both routes into the store validate this parameter against the
    // RouteToDesktop descriptor (ruleaction_builtins_engine.cpp), which requires
    // an integral double in [1, MaxVirtualDesktopOrdinal): addRule goes through
    // Rule::isValid -> ActionRegistry::validate, and a hand-edited rules.json
    // goes through RuleAction::fromJson, which drops the action on the same
    // check. What the guard survives is a later relaxation of those bounds, and
    // an in-process producer that fills the slot directly.
    //
    // No upper bound here on purpose: the descriptor owns that, and the effect
    // re-guards the target against the LIVE desktop list before moving anything,
    // which is the only bound that is authoritative at that moment.
    const int desktop = route->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
    if (desktop >= 1) {
        qCInfo(lcDbusWindow) << "open-routing: routing" << windowId << "to virtual desktop" << desktop;
        Q_EMIT windowDesktopMoveRequested(windowId, desktop);
    } else {
        // Logged rather than passed over: the validators above make this
        // unreachable from a stored rule, so seeing it means one of them has
        // been relaxed or bypassed, and the window silently keeps whatever
        // desktop it opened on.
        qCWarning(lcDbusWindow) << "open-routing: matched RouteToDesktop for" << windowId
                                << "carries an unusable target" << desktop << "— not moving the window";
    }
    return true;
}

bool WindowTrackingAdaptor::applyOpenDesktopRouting(const QString& windowId, const QString& screenId)
{
    // Engine-neutral RouteToDesktop: when a matched rule pins the opening window
    // to a virtual desktop, ask the compositor to move it there. Independent of
    // snapping/tiling — the desktop move composes with the window's placement.
    // Called from the snap open-path facade (the autotile path uses
    // applyOpenRoutingForTiling, which also handles the screen redirect).
    if (!m_ruleStore) {
        return false;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return false;
    }
    // Pin the screen so a ScreenId-scoped rule resolves, mirroring placementZonesByRule.
    // resolveCached is keyed on windowId (+ rule-set revision). On the snap open path
    // this runs first (SnapAdaptor::resolveWindowRestore calls it before the engine's
    // resolveWindowRestore), so it is the resolve that seeds the window's memo with a
    // screen-stamped query, and the float / restore predicates read that verdict later.
    stampScreenContext(*query, screenId);
    ensureRuleEvaluator();
    return emitOpenRoutingIfMatched(
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitWith(&admitScreenStamped, *query)), windowId);
}

void WindowTrackingAdaptor::clearOpenWorkspaceRoute(const QString& windowId)
{
    m_workspaceRoutedDesktop.remove(windowId);
    m_workspaceRoutedScreen.remove(windowId);
}

void WindowTrackingAdaptor::evictRuleVerdicts(const QString& windowId)
{
    if (m_ruleEvaluator) {
        m_ruleEvaluator->evictCached(windowId);
    }
}

void WindowTrackingAdaptor::setRoutedOpenDispatcher(
    std::function<void(const PhosphorProtocol::WindowOpenedEntry&)> dispatcher)
{
    m_routedOpenDispatcher = std::move(dispatcher);
}

void WindowTrackingAdaptor::setWindowLeftScreenHook(std::function<void(const QString&, const QString&)> hook)
{
    m_windowLeftScreenHook = std::move(hook);
}

bool WindowTrackingAdaptor::applyOpenScreenRouting(const QString& windowId, const QString& screenId,
                                                   PhosphorEngine::RestoreReason reason)
{
    if (!m_ruleStore) {
        return false;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return false;
    }
    // Pin the screen so a ScreenId-scoped rule resolves, mirroring placementZonesByRule.
    stampScreenContext(*query, screenId);
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitWith(&admitScreenStamped, *query));

    // A SnapToZone action in the verdict does NOT disqualify the move, and used to.
    // The old guard returned whenever the Placement slot carried a valid ordinal,
    // reasoning that calculateSnapToPlacementRule resolves the zones ON the target
    // screen and snaps there, so this branch was unreachable for a route+snap rule
    // and moving here would double-place the window.
    //
    // Both halves of that were wrong. This function's caller
    // (SnapAdaptor::resolveWindowRestore) reaches it only when no snap happened,
    // and calculateSnapToPlacementRule is pure calculation whose every decline
    // returns before any commit — it declines when the routed (screen, desktop)
    // target is not in Snapping mode (the common case: a tiling-mode target), and
    // when that target resolves no layout, no surviving ordinal, or a degenerate
    // union geometry. A window opening with a RouteToScreen onto a tiling monitor
    // then reached neither path: it did not snap and it did not move, while the
    // same rule minus its SnapToZone action moved it fine. So the route below is
    // honoured whether or not a Placement slot rode along; a Placement slot with
    // no route still claims ownership (see the tail of this function).
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        // No route. A placement directive with at least one valid target still
        // means the rule system owns this window's target, whether or not the
        // engine committed a snap — return true so the remembered-placement
        // fallback does not relocate it.
        return hasValidPlacementTarget(resolved);
    }
    const QString target =
        route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString().trimmed();
    if (target.isEmpty()) {
        return false;
    }
    // screensMatch, not a raw compare: connector-name and EDID-id spellings can
    // name the SAME monitor, and a raw compare would treat that as a real move —
    // arming windowOutputMoveExpected and re-placing a window that is already
    // where the rule wants it. Still `true`: the rule matched and the window
    // sits where it demands, so no remembered-placement fallback may move it.
    if (PhosphorScreens::ScreenIdentity::screensMatch(target, screenId)) {
        return true;
    }
    // m_service is non-null post-construction (class invariant); screenManager()
    // itself may still be null (e.g. an unconfigured test fixture), so guard that.
    PhosphorScreens::ScreenManager* screens = m_service->screenManager();
    if (!screens) {
        return true;
    }
    const QRect dstAvail = screens->screenAvailableGeometry(target);
    if (!dstAvail.isValid()) {
        // Target monitor is not currently connected — leave the window on its spawn
        // screen (the rule fires again when that monitor returns). `true` even
        // though nothing moved: the rule owns the window's monitor, and a
        // remembered-placement fallback relocating it now would fight the
        // re-route when the monitor comes back.
        qCDebug(lcDbusWindow) << "applyOpenScreenRouting: route target" << target
                              << "is not currently connected — not moving" << windowId;
        return true;
    }
    // The daemon's frame-geometry shadow is written only by the effect's DEBOUNCED
    // flush, while this runs inside the synchronous window-open round trip. The
    // snap handler seeds the shadow immediately before an OPEN resolve, so the
    // open path usually does find an entry; the other drivers this route admits
    // (pending sweep, desktop arrival) push no seed, and for those the shadow is
    // still empty here. Returning on that would make a bare RouteToScreen rule
    // silently no-op for exactly the case it exists to handle, so the registry
    // metadata — pushed by the setWindowMetadata call that precedes the restore —
    // is the fallback.
    QRect cur = frameGeometry(windowId);
    const QString pendingKey = shadowWindowId(windowId);
    const auto pendingRect = m_pendingOpenGeometry.constFind(pendingKey);
    const bool havePendingRect = pendingRect != m_pendingOpenGeometry.constEnd() && pendingRect->isValid();
    // The open path may have asked the effect for a float-position restore
    // moments ago, inside this very resolve; the shadow cannot know yet, so
    // read what was asked for (see m_pendingOpenGeometry).
    if (havePendingRect) {
        cur = *pendingRect;
    }
    if (!cur.isValid() && !m_windowRegistry.isNull()) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
        if (const std::optional<PhosphorEngine::WindowMetadata> meta = m_windowRegistry->metadata(instanceId)) {
            if (meta->positionX && meta->positionY && meta->width && meta->height && *meta->width > 0
                && *meta->height > 0) {
                cur = QRect(*meta->positionX, *meta->positionY, *meta->width, *meta->height);
            }
        }
    }
    // The pending free SIZE is applied last, AFTER the metadata fallback has
    // had its chance to supply a position: the size restore and the metadata
    // push describe the same window at different moments, and the metadata
    // carries the un-restored managed size. Applying the size only when the
    // shadow already held a rect left the fallback path translating the very
    // size this open just asked the effect to replace. Skipped when a pending
    // RECT won above, since that rect already carries the restored size.
    if (!havePendingRect && cur.isValid()) {
        if (const auto pendingSize = m_pendingOpenSize.constFind(pendingKey);
            pendingSize != m_pendingOpenSize.constEnd() && pendingSize->isValid()) {
            cur.setSize(*pendingSize);
        }
    }
    if (!cur.isValid()) {
        // Neither the shadow nor the metadata knows where the window is — nothing
        // to translate onto the target screen. Still `true`: the rule owns the
        // window's monitor.
        qCDebug(lcDbusWindow) << "applyOpenScreenRouting: no frame geometry for" << windowId << "— not moving";
        return true;
    }

    // "The same spot on the other monitor" across differing resolutions: the
    // position kept relative to the available area, the size shrunk only where
    // the target is smaller, the whole frame clamped inside.
    const QRect routed =
        PhosphorEngine::GeometryUtils::carryRectOntoArea(cur, screens->screenAvailableGeometry(screenId), dstAvail);
    const int x = routed.x();
    const int y = routed.y();
    const int w = routed.width();
    const int h = routed.height();

    qCInfo(lcDbusWindow) << "applyOpenScreenRouting: routing" << windowId << "to monitor" << target << "at"
                         << QRect(x, y, w, h);
    // The screen the frame lands on, at the virtual-screen level, and the
    // engine that runs it.
    QString landed = Utils::effectiveScreenIdAt(screens, QRect(x, y, w, h).center());
    if (landed.isEmpty() || !PhosphorScreens::ScreenIdentity::belongsToPhysicalScreen(landed, target)) {
        landed = target;
    }
    PhosphorSnapEngine::SnapEngine* snap = snapEngine();
    PhosphorEngine::PlacementEngineBase* tiling = nullptr;
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(landed)) {
        tiling = m_autotileEngine.data();
    } else if (m_scrollEngine && m_scrollEngine->isActiveOnScreen(landed)) {
        tiling = m_scrollEngine.data();
    }
    if (tiling) {
        // The open float terminal's snap float on the spawn screen goes (F448),
        // then the window lands free on the target and its engine gets it as
        // an open: nothing else would tile it when the move's echo arrives
        // inside the apply (F472). A genuine open only for an Open (F510).
        announceOutputMove(windowId, landed, screenId, tiling);
        Q_EMIT applyGeometryRequested(windowId, x, y, w, h, QString(), landed, false,
                                      static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));
        int routedDesktop = 0;
        if (const auto desktopRoute = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
            routedDesktop = desktopRoute->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        }
        // Sent to a desktop not in view there: its arrival places it.
        const bool offView = routedDesktop >= 1 && routedDesktop != currentDesktopForScreen(landed);
        if (!offView && m_routedOpenDispatcher) {
            PhosphorProtocol::WindowOpenedEntry entry;
            entry.windowId = windowId;
            entry.screenId = landed;
            entry.focusEligible = reason == PhosphorEngine::RestoreReason::Open;
            m_routedOpenDispatcher(entry);
        }
        return true;
    }
    // A snapping target: the open float goes with the window, so Meta+F and
    // the next snap key act on the monitor it is on (F411, F245).
    const bool snapTarget = snap && snap->isActiveOnScreen(landed);
    if (snapTarget && snap->isWindowTracked(windowId) && snap->zoneForWindow(windowId).isEmpty()) {
        snap->migrateWindowToScreen(windowId, landed);
    }
    announceOutputMove(windowId, landed, screenId, snapTarget ? snap : nullptr);
    Q_EMIT applyGeometryRequested(windowId, x, y, w, h, QString(), landed, false,
                                  static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));
    if (snapTarget) {
        captureWindowPlacement(windowId, QString(), /*fromStateChange=*/true);
    }
    return true;
}

QString WindowTrackingAdaptor::applyOpenRoutingForTiling(const QString& windowId, const QString& screenId,
                                                         bool* directiveMatched)
{
    // Owned by this function, not the caller: a set-only out-param leaves a
    // caller's pre-set value standing on every no-match path, which reads as
    // "a rule matched" and silently vetoes the reopen claim.
    if (directiveMatched) {
        *directiveMatched = false;
    }
    if (!m_ruleStore) {
        return QString();
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return QString();
    }
    stampScreenContext(*query, screenId);
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitWith(&admitScreenStamped, *query));

    // RouteToDesktop is engine-neutral — emit it for autotile windows too.
    // Deliberately NOT folded into `directiveMatched`: a desktop or workspace
    // route says nothing about which monitor the window belongs on, so it must
    // not veto the reopen claim the way a RouteToScreen match does.
    emitOpenRoutingIfMatched(resolved, windowId);
    // Taken right after the write that produced it, for the same single-use
    // reason placementZonesByRule takes it: nothing further down this function
    // may leave a routed answer standing for a later non-open reader. Read
    // below, where the destination desktop is resolved.
    const int routedWorkspaceDesktop = m_workspaceRoutedDesktop.take(windowId);
    m_workspaceRoutedScreen.remove(windowId);

    const auto markMatched = [&] {
        if (directiveMatched) {
            *directiveMatched = true;
        }
    };

    // A valid SnapToZone placement directive owns the window's placement even
    // on this channel (the snap facade acts on it) — signal the match so the
    // caller's reopen-claim veto sees it, mirroring the snap twin's target check.
    // The routed-screen RETURN stays empty: placement is not a tiling
    // redirect.
    if (hasValidPlacementTarget(resolved)) {
        markMatched();
    }

    // RouteToScreen: redirect the window onto a different ENGINE-OWNED monitor
    // (autotile or scrolling). The snap open path handles snap-mode targets
    // itself (the placement directive), so here we only honour a target whose
    // mode an engine claims — a snap or disabled target is left to the
    // window's spawn screen (cross-engine routing is out of scope). Returning the target tells the caller to insert
    // the window into that screen's tiling state; the output-move marker stops the
    // effect from re-processing the resulting outputChanged as a fresh open.
    //
    // The RETURN and the MATCH signal are deliberately separate answers: the
    // return names a redirect target (empty = "insert on the spawn screen"),
    // while @p directiveMatched reports that a rule OWNS this window's
    // monitor — true on every matched-route exit below, including
    // already-on-target and target-not-connected. The snap twin
    // (applyOpenScreenRouting) folds both into one bool; overloading THIS
    // function's empty return the same way is what let the two channels
    // apply opposite reopen-claim precedence.
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return QString();
    }
    const QString target =
        route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString().trimmed();
    if (target.isEmpty()) {
        return QString();
    }
    markMatched();
    // screensMatch for the same reason the snap twin uses it: a differently
    // spelled id for the SAME monitor must read as "already there". A raw
    // compare would return the target as a distinct screen and the caller would
    // key tiling state under a second name for one output.
    if (!m_layoutManager || PhosphorScreens::ScreenIdentity::screensMatch(target, screenId)) {
        return QString();
    }
    // When the same rule also pins a target desktop (RouteToDesktop), the window
    // lands on THAT desktop of the target screen, so gate the autotile-mode check
    // against the destination desktop — not the target's current desktop. Mirrors
    // the snap path (calculateSnapToPlacementRule), which gates modeForScreen on the
    // routed desktop. Absent / 0 ⇒ the target screen's current desktop.
    // A realized RouteToWorkspace outranks the positional number here too (the
    // emitOpenRoutingIfMatched call at the top of this function stashed it),
    // for the same reason the snap directive prefers it.
    int destDesktop = currentDesktopForScreen(target);
    if (routedWorkspaceDesktop >= 1) {
        destDesktop = routedWorkspaceDesktop;
    } else if (const auto desktopRoute = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
        const int d = desktopRoute->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        if (d >= 1) {
            destDesktop = d;
        }
    }
    const auto targetMode = m_layoutManager->modeForScreen(target, destDesktop, m_layoutManager->currentActivity());
    if (targetMode != PhosphorZones::AssignmentEntry::Mode::Autotile
        && targetMode != PhosphorZones::AssignmentEntry::Mode::Scrolling) {
        qCDebug(lcDbusWindow) << "applyOpenRoutingForTiling: RouteToScreen target" << target
                              << "is not in a tiling-family mode — not redirecting" << windowId;
        return QString();
    }
    // Connectivity guard, mirroring the snap twin (applyOpenScreenRouting):
    // the mode answers from the STORED assignment, so a disconnected
    // monitor still passes it — and a routed open no engine can claim gets
    // DROPPED instead of tiling on the spawn screen.
    //
    // m_service is non-null post-construction (class invariant), so it is
    // dereferenced directly here as it is in the snap twin; screenManager()
    // itself may still be null (an unconfigured test fixture), so that is the
    // one thing checked.
    if (m_service->screenManager() && !m_service->screenManager()->screenAvailableGeometry(target).isValid()) {
        qCInfo(lcDbusWindow) << "applyOpenRoutingForTiling: RouteToScreen target" << target
                             << "is not connected — window opens on its spawn screen";
        return QString();
    }
    qCInfo(lcDbusWindow) << "applyOpenRoutingForTiling: routing" << windowId << "to engine-managed screen" << target;
    announceOutputMove(windowId, target, screenId,
                       targetMode == PhosphorZones::AssignmentEntry::Mode::Autotile ? m_autotileEngine.data()
                                                                                    : m_scrollEngine.data());
    return target;
}

} // namespace PlasmaZones
