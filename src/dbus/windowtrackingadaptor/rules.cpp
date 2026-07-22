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
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/RuleStore.h>

#include <QJsonArray>

namespace PlasmaZones {

bool WindowTrackingAdaptor::shouldRestoreFloatedPosition(const QString& windowId,
                                                         PhosphorZones::AssignmentEntry::Mode mode)
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
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }

    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    // Shares m_ruleEvaluator with shouldFloatByRule; resolveCached is keyed on
    // (windowId, ruleSet revision) and ignores the query on a hit. Safe because both
    // are open-path (resolved once per window lifetime — see shouldFloatByRule) and
    // the effect pushes the window's full metadata before the engine's open-path
    // resolve, so the first (and only) resolve for a window sees complete metadata.
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);
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
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
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
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
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

bool WindowTrackingAdaptor::shouldFloatByRule(const QString& windowId)
{
    // Float is purely rule-driven: there is no global "float on open" setting, so
    // absent a matching rule the answer is "do not float".
    if (!m_ruleStore) {
        return false;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return false;
    }

    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    // resolveCached is keyed on (windowId, ruleSet revision); on a cache hit the
    // freshly built `query` is ignored. That is safe because windowId is both
    // lifetime-stable AND unique: a reopened window gets a fresh instanceId (new
    // key → miss) and a mid-session appId rename changes the composite key too, so
    // a cached verdict can never outlive the metadata it was built from. Both the
    // float and restore predicates are open-path (resolved once per lifetime).
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);
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
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
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
    // The shared evaluator cache is keyed on windowId only, so the FIRST resolver
    // to touch a window seeds the verdict the others reuse. On the open path that
    // is this placement resolve: SnapEngine::resolveWindowRestore calls
    // calculateSnapToPlacementRule up front, before it consults the float /
    // restore predicates — so the screen-pinned query populates the cache first
    // and a screen-constrained rule resolves correctly.
    query->screenId = screenId;

    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
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
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return;
    }
    // Pin the screen so a ScreenId-scoped rule resolves, mirroring placementZonesByRule.
    // resolveCached is keyed on windowId (+ rule-set revision), so on the snap open path
    // this reuses the verdict placementZonesByRule already seeded — no second evaluation.
    query->screenId = screenId;
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    emitRouteToDesktopIfMatched(m_ruleEvaluator->resolveCached(windowId, *query), windowId);
}

void WindowTrackingAdaptor::applyOpenScreenRouting(const QString& windowId, const QString& screenId)
{
    if (!m_ruleStore) {
        return;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return;
    }
    // Pin the screen so a ScreenId-scoped rule resolves, mirroring placementZonesByRule.
    query->screenId = screenId;
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    // Bare RouteToScreen only. A rule that ALSO carries SnapToZone routes AND snaps
    // via the placement directive (calculateSnapToPlacementRule resolves the zones
    // ON the target screen and returns shouldSnap, so the facade never reaches the
    // no-snap branch that calls this); moving here too would double-place the window.
    if (resolved.slot(QString(PhosphorRules::ActionSlot::Placement))) {
        return;
    }
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
    const QRect cur = frameGeometry(windowId);
    if (!cur.isValid()) {
        // No geometry pushed yet — nothing to translate onto the target screen.
        return;
    }

    // Map the window's position relative to its current screen's available area onto
    // the target screen's, preserving size, then clamp so the whole frame fits.
    // Preserves "the same spot on the other monitor" across differing resolutions; an
    // unknown / degenerate source area falls back to the target's top-left.
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
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return QString();
    }
    query->screenId = screenId;
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
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
    if (m_layoutManager->modeForScreen(target, destDesktop, m_layoutManager->currentActivity())
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
