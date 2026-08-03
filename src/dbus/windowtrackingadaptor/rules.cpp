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
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/RuleStore.h>

#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace PlasmaZones {

namespace {

/// True if @p s is one of the three hex shapes PhosphorRules' `hasHexColor`
/// admits: `#RGB`, `#RRGGBB` or `#AARRGGBB`. Mirrors the twin in
/// layoutregistry_contextresolve.cpp; deliberately NOT
/// `QColor::isValidColorName`, which also admits SVG keywords, so
/// "transparent" would reach the overlay as an invisible indicator.
bool isHexColorString(const QString& s)
{
    if ((s.size() != 4 && s.size() != 7 && s.size() != 9) || s.at(0) != QLatin1Char('#')) {
        return false;
    }
    for (int i = 1; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            return false;
        }
    }
    return true;
}

/// Structural admission tests for the open-path resolvers, one per stamping
/// shape. Every one of them is the same guard the zones-layer context resolvers
/// apply (layoutregistry_contextresolve.cpp), for the same reason.
///
/// An UNSTAMPED context field is not merely inert. WindowQuery::valueForField
/// returns an ENGAGED empty string for the string-valued context fields, so a
/// POSITIVE leaf (`Mode Equals scrolling`) correctly never matches — but a
/// NEGATED one (`None{Mode Equals scrolling}`) matches precisely BECAUSE the
/// inner leaf failed, and the rule then fires for EVERY window. Excluding rules
/// that reference an unstamped field closes both polarities instead of relying
/// on the empty value to coincide with a non-match.
///
/// ActiveLayout, ScreenOrientation and TiledWindowCount are stamped by NO
/// resolver on this path (they are context-cascade fields), so they are
/// excluded everywhere here.
const QSet<PhosphorRules::Field>& neverStampedFields()
{
    static const QSet<PhosphorRules::Field> fields = {
        PhosphorRules::Field::ActiveLayout,
        PhosphorRules::Field::ScreenOrientation,
        PhosphorRules::Field::TiledWindowCount,
    };
    return fields;
}

/// Admission test for a resolver that stamps ScreenId but NOT Mode — the shape
/// every resolveCached caller on this path shares. Passed identically by all
/// six so the memo they share stays coherent (see resolveCachedFiltered's
/// precondition).
bool admitScreenStamped(const PhosphorRules::Rule& rule)
{
    return !rule.match.referencesAnyField(neverStampedFields())
        && !rule.match.referencesAnyField({PhosphorRules::Field::Mode});
}

/// Admission test for a resolver that stamps BOTH ScreenId and Mode
/// (shouldFloatByRule, scrollOpenRuleParams, shouldRestoreSizeOnUnsnap). Mode
/// stays admitted; only the never-stamped context fields are excluded.
bool admitScreenAndModeStamped(const PhosphorRules::Rule& rule)
{
    return !rule.match.referencesAnyField(neverStampedFields());
}

} // namespace

void WindowTrackingAdaptor::ensureRuleEvaluator()
{
    // See the declaration doc: the single construction site for the
    // full-store evaluator, so the placement-only terminal scope is applied
    // in exactly one place. Blanket Exclude keeps its historical effect
    // (an excluded window gets no placement policy either); the decoration-
    // and animation-scoped exclusions are inert here, resolved instead by
    // the effect's dedicated sliced evaluators.
    if (m_ruleEvaluator) {
        return;
    }
    m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    m_ruleEvaluator->setTerminalActionScope(
        {QString(PhosphorRules::ActionType::Exclude), QString(PhosphorRules::ActionType::ExcludePlacement)});
}

bool WindowTrackingAdaptor::shouldRestoreFloatedPosition(const QString& windowId,
                                                         PhosphorZones::AssignmentEntry::Mode mode)
{
    // m_settings is a hard ctor dependency (qFatal on null), so it is non-null
    // here — deref unguarded like every other method in this class. The global
    // default is per-engine (snap- vs autotile- vs scroll-floated); the
    // RestorePosition rule override below is engine-neutral.
    const bool globalDefault = mode == PhosphorZones::AssignmentEntry::Mode::Autotile
        ? m_settings->autotileRestoreFloatedWindowsOnLogin()
        : mode == PhosphorZones::AssignmentEntry::Mode::Scrolling ? m_settings->scrollingRestoreFloatedWindowsOnLogin()
                                                                  : m_settings->snappingRestoreFloatedWindowsOnLogin();

    // No rule store / metadata → the global setting is the whole policy.
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }

    ensureRuleEvaluator();
    // resolveCached is keyed on (windowId, ruleSet revision) and ignores the
    // query on a hit. Six callers share the memo: four stamp ScreenId
    // (placementZonesByRule and the three open-routing resolvers) and two
    // stamp nothing (this one and shouldRestoreToZoneOnLogin).
    //
    // The load-bearing ORDERING invariant: on every open path a STAMPING
    // resolver runs before either unstamped one, so the memo is seeded with a
    // ScreenId-stamped query. The snap path routes at
    // src/dbus/snapadaptor/snaprestore.cpp (applyOpenDesktopRouting) BEFORE
    // calling the engine's resolveWindowRestore, which is what reaches
    // placementZonesByRule and these predicates; the tiling path runs
    // applyOpenRoutingForTiling before the engine's windowOpened. All four
    // stampers stamp the SAME screenId, and an unstamped reader consuming a
    // stamped verdict only ever sees a SUPERSET (ScreenId-scoped rules can
    // match; nothing it needs is lost). The hazard is the reverse order: an
    // unstamped predicate resolving FIRST would seed an empty screenId and
    // every ScreenId-scoped SnapToZone / RouteToScreen rule would silently
    // stop firing for that window. Do not reorder these calls.
    //
    // CAVEAT on "all four stamp the SAME screenId": they stamp the SPAWN
    // screen. Under RouteToScreen the window is then dispatched to the routed
    // TARGET, so the memo keeps the spawn screen and a rule pairing
    // `ScreenId == target` with RestorePosition (or RestoreToZoneOnLogin) does
    // not fire for a routed window. The snap side has the same shape; the
    // scrolling path was verified unaffected. Re-stamping the routed screen
    // before engine dispatch would close it, at the cost of a second resolve.
    //
    // shouldFloatByRule and scrollOpenRuleParams opted out of the cache
    // entirely, because they stamp Mode as well.
    //
    // admitScreenStamped is the SAME predicate all six cached callers pass, as
    // resolveCachedFiltered's precondition requires — a filtered verdict is
    // memoized under the window id, so a caller admitting a different rule set
    // would poison the memo for the others.
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitScreenStamped);
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
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitScreenStamped);
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
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }
    // Stamp the window's CURRENT screen and the mode that screen resolves to.
    // Being uncached, this resolver can carry them for free (there is no memo
    // to key them into), and without them a rule pairing ScreenId or Mode with
    // SetRestoreSizeOnUnsnap is silently inert — a pairing the rules editor
    // offers. The mode token is the MATCH vocabulary ("snapping" / "tiling" /
    // "scrolling"), not the SetEngineMode action vocabulary.
    stampScreenAndMode(*query, windowId,
                       m_service ? m_service->screenForWindow(windowId, m_lastActiveScreenId) : QString());
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
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveFiltered(*query, admitScreenAndModeStamped);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestoreSizeOnUnsnap))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

void WindowTrackingAdaptor::stampScreenAndMode(PhosphorRules::WindowQuery& query, const QString& windowId,
                                               const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }
    query.screenId = screenId;
    if (!m_layoutManager) {
        return;
    }
    // The WINDOW's context, resolved through WindowContext's OWN accessors
    // rather than picked apart here. "Which context governs this window" has
    // exactly one answer and it lives on WindowContext: effectiveDesktop
    // resolves a window spanning SEVERAL desktops to the screen's current one
    // when that is among them, and handles sticky. Neither is expressible from
    // the query, because buildRuleQueryForWindow copies only the scalar
    // `virtualDesktop` and never the span — so deriving it inline made the
    // OPEN-time float verdict disagree with the daemon's LIVE float resolver
    // for the very same window, which is a per-mode float invariant break.
    // init_engines.cpp's resolver does exactly this.
    int desktop = currentDesktopForScreen(screenId);
    QString activity = m_layoutManager->currentActivity();
    if (m_windowRegistry) {
        const auto ctx = m_windowRegistry->windowContext(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        if (ctx) {
            desktop = ctx->effectiveDesktop(desktop);
            activity = ctx->effectiveActivity(activity);
        }
    }
    // The MATCH vocabulary ("snapping" / "tiling" / "scrolling"), not the
    // SetEngineMode action vocabulary, which spells the middle one "autotile".
    switch (m_layoutManager->modeForScreen(screenId, desktop, activity)) {
    case PhosphorZones::AssignmentEntry::Snapping:
        query.mode = QStringLiteral("snapping");
        break;
    case PhosphorZones::AssignmentEntry::Autotile:
        query.mode = QStringLiteral("tiling");
        break;
    case PhosphorZones::AssignmentEntry::Scrolling:
        query.mode = QStringLiteral("scrolling");
        break;
    }
}

bool WindowTrackingAdaptor::shouldFloatByRule(const QString& windowId, const QString& screenId)
{
    // Float is purely rule-driven: there is no global "float on open" setting, so
    // absent a matching rule the answer is "do not float".
    if (!m_ruleStore) {
        return false;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return false;
    }
    // Pin the opening screen and the mode it resolves to; without them a rule
    // pairing ScreenId or Mode with Float never matches, a silent inertness
    // since the rules editor offers exactly that pairing.
    stampScreenAndMode(*query, windowId, screenId);

    ensureRuleEvaluator();
    // UNCACHED, for the same reason scrollOpenRuleParams is: resolveCached is
    // keyed on (windowId, ruleSet revision) ALONE, so on a hit the freshly
    // built query is ignored — including the ScreenId and Mode stamped above.
    // The first screen a window was asked about would then decide the verdict
    // for its whole lifetime, and a Mode-paired rule would answer for the
    // wrong mode. One resolve per open is the cost.
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveFiltered(*query, admitScreenAndModeStamped);
    // The Float action carries free-form params (no Value key), so the verdict is
    // the PRESENCE of the filled slot, not a bool payload.
    return resolved.slot(QString(PhosphorRules::ActionSlot::Float)).has_value();
}

QVariantMap WindowTrackingAdaptor::tabColorRuleParams(const QString& windowId)
{
    if (!m_ruleStore) {
        return {};
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return {};
    }
    // NO screen/mode stamping here, unlike scrollOpenRuleParams: this runs
    // from the strip-relayout path, which knows the window but not which
    // screen's resolve it belongs to, and a wrong stamp is worse than none —
    // it would make a ScreenId-conditioned rule match the wrong monitor. A
    // rule pairing a tab colour with a ScreenId or Mode condition is therefore
    // inert on this path by design; the per-CONTEXT colour actions are the
    // spelling for "recolour tabs on this screen".
    ensureRuleEvaluator();
    // This path uses its OWN memo and never touches the shared one. Two
    // separate reasons, and the shared memo cannot satisfy either:
    //
    //  - POISONING. The shared memo's key is (windowId, revision) with the
    //    admit filter deliberately NOT part of it (ruleevaluator.cpp), so a
    //    verdict resolved under a different filter lands under the same key the
    //    six admitScreenStamped callers read. Per the negation guard in
    //    internal.h that is not merely a superset: a negated group matches
    //    BECAUSE its inner leaf fails on an unstamped query, so one foreign
    //    entry can make a SnapToZone / RouteToScreen rule fire for every
    //    window. Seeding it from here would also break the ordering invariant
    //    documented above shouldRestoreFloatedPosition — the two existing
    //    unstamped readers only run on the open path, AFTER a stamper.
    //  - COST. Merely READING the shared memo without seeding is not viable
    //    either: every one of its six seeders runs on the OPEN path only, and
    //    a rules save bumps the revision, so after any save the peek misses
    //    forever for every already-open window (they will not open again).
    //    This function runs per tab on every re-enrichment, which includes
    //    every window TITLE change, so a permanent miss means a full rule-set
    //    walk per tab per retitle.
    //
    // A private memo keyed the same way is free of both: nothing else reads it,
    // so its filter can be exactly right for THIS query, and a second call
    // costs a hash lookup again.
    // The key is the revision PLUS the query fields that move under a live
    // window. Title is load-bearing: the enrichment refresh that calls this is
    // driven by title changes, so a revision-only key would pin a
    // `Title contains …` rule to its first verdict for the window's lifetime.
    const quint64 revision = m_ruleStore->ruleSet().revision();
    const auto memoIt = m_tabColorMemo.constFind(windowId);
    if (memoIt != m_tabColorMemo.constEnd() && memoIt->revision == revision && memoIt->title == query->title
        && memoIt->captionNormal == query->captionNormal && memoIt->virtualDesktop == query->virtualDesktop
        && memoIt->activity == query->activity) {
        return memoIt->colors;
    }
    // The filter is stricter than admitScreenStamped: this query stamps NO
    // context field, and WindowQuery::valueForField returns an ENGAGED empty
    // QVariant for ScreenId, so a `None{ScreenId Equals …}` group would match
    // because its inner leaf fails — the same inversion the shared memo's
    // filter exists to prevent, which admitScreenStamped does not cover here
    // precisely because it assumes ScreenId IS stamped.
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, [](const PhosphorRules::Rule& rule) {
            return admitScreenStamped(rule) && !rule.match.referencesAnyField({PhosphorRules::Field::ScreenId});
        });
    const QVariantMap colors = tabColorsFromResolved(resolved);
    m_tabColorMemo.insert(windowId,
                          TabColorMemoEntry{revision, query->title, query->captionNormal, query->virtualDesktop,
                                            query->activity, colors});
    return colors;
}

QVariantMap WindowTrackingAdaptor::tabColorsFromResolved(const PhosphorRules::ResolvedActions& resolved)
{
    QVariantMap out;
    const auto readColor = [&resolved, &out](QLatin1StringView slot, const QString& key) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QString value = action->params.value(QString(PhosphorRules::ActionParam::Value)).toString();
        // Empty means "no override" on this path, not "clear to nothing": an
        // empty colour reaching the overlay would read as the theme fallback
        // anyway, so dropping it here keeps the map honest about what matched.
        //
        // Shape-checked for the same reason its twin in the zones layer is
        // (layoutregistry_contextresolve.cpp): this value goes through to a
        // QML `color` property verbatim, where anything unparseable renders as
        // an invalid colour rather than falling back to the theme. The
        // descriptors already enforce hex-only, so this only catches a payload
        // that reached the store without passing them.
        if (isHexColorString(value)) {
            out.insert(key, value);
        }
    };
    readColor(PhosphorRules::ActionSlot::TabColorActive, QStringLiteral("activeColor"));
    readColor(PhosphorRules::ActionSlot::TabColorInactive, QStringLiteral("inactiveColor"));
    readColor(PhosphorRules::ActionSlot::TabColorUrgent, QStringLiteral("urgentColor"));
    return out;
}

QVariantMap WindowTrackingAdaptor::scrollOpenRuleParams(const QString& windowId, const QString& screenId)
{
    QVariantMap out;
    if (!m_ruleStore) {
        return out;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return out;
    }
    // Pin the two context fields this path knows and buildRuleQueryForWindow
    // cannot: the window is opening on @p screenId, and it is opening on a
    // SCROLLING screen by construction (only ScrollEngine calls this). Without
    // them a user-authored open rule carrying a ScreenId or Mode condition
    // never matches, and those are the two conditions the settings editor
    // offers alongside the scrolling open actions.
    query->screenId = screenId;
    query->mode = QStringLiteral("scrolling");

    ensureRuleEvaluator();
    // Deliberately UNCACHED, unlike the sibling predicates. resolveCached is
    // keyed on (windowId, ruleSet revision) alone, so seeding it from this
    // extra-stamped query would hand the screen-only placement resolver a
    // verdict built under a Mode condition it never asked for (and vice
    // versa, on whichever path resolves first). This is one resolve per
    // window open on a scrolling screen.
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveFiltered(*query, admitScreenAndModeStamped);
    if (const auto action = resolved.slot(QString(PhosphorRules::ActionSlot::OpenColumnWidth))) {
        // Only forward a genuinely numeric fraction inside the SHARED
        // column-width bounds (PhosphorRules::Min/MaxColumnWidthRatio, the
        // same pair the descriptor validator and the context resolver
        // clamp against): a missing or malformed Value would toDouble() to
        // 0.0, and an out-of-range hand-edit (say 50.0) must fall back to
        // the configured default, not saturate a clamp.
        const auto value = action->params.value(QString(PhosphorRules::ActionParam::Value));
        const double fraction = value.toDouble(0.0);
        if (value.isDouble() && fraction >= PhosphorRules::MinColumnWidthRatio
            && fraction <= PhosphorRules::MaxColumnWidthRatio) {
            out.insert(ScrollOpenKeys::widthFraction(), fraction);
        }
    }
    if (const auto action = resolved.slot(QString(PhosphorRules::ActionSlot::OpenWindowHeight))) {
        // Same reject-not-clamp policy as the width slot above; the height
        // fraction shares the width pair's bounds.
        const auto value = action->params.value(QString(PhosphorRules::ActionParam::Value));
        const double fraction = value.toDouble(0.0);
        if (value.isDouble() && fraction >= PhosphorRules::MinColumnWidthRatio
            && fraction <= PhosphorRules::MaxColumnWidthRatio) {
            out.insert(ScrollOpenKeys::heightFraction(), fraction);
        }
    }
    if (const auto action = resolved.slot(QString(PhosphorRules::ActionSlot::OpenTabbed))) {
        out.insert(ScrollOpenKeys::tabbed(), action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool());
    }
    if (const auto action = resolved.slot(QString(PhosphorRules::ActionSlot::OpenColumnPlacement))) {
        const QString token = action->params.value(QString(PhosphorRules::ActionParam::Value)).toString();
        out.insert(ScrollOpenKeys::consume(), token == QLatin1String(PhosphorRules::ColumnPlacementToken::Consume));
    }
    return out;
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

    ensureRuleEvaluator();
    // Shares m_ruleEvaluator with shouldFloatByRule / shouldRestoreFloatedPosition;
    // resolveCached is keyed on (windowId, ruleSet revision) and returns every matched
    // slot, so reading the Placement slot off the same verdict is free. Same open-path
    // lifetime guarantee (resolved once per window lifetime) as the sibling predicates.
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitScreenStamped);

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
    // applyOpenRoutingForTiling, which also handles the screen redirect).
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
    ensureRuleEvaluator();
    emitRouteToDesktopIfMatched(m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitScreenStamped), windowId);
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
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitScreenStamped);

    // Bare RouteToScreen only. A rule that ALSO carries SnapToZone routes AND snaps
    // via the placement directive (calculateSnapToPlacementRule resolves the zones
    // ON the target screen and returns shouldSnap, so the facade never reaches the
    // no-snap branch that calls this); moving here too would double-place the window.
    //
    // Gate on the RESOLVED directive carrying at least one valid ordinal, not on
    // the Placement slot's mere presence. placementZonesByRule drops
    // out-of-range ordinals, so an all-rejected SnapToZone payload produces an
    // empty directive: the engine snaps nothing, and returning here on presence
    // alone would drop the accompanying RouteToScreen too, leaving the window
    // neither snapped nor routed.
    if (const auto placement = resolved.slot(QString(PhosphorRules::ActionSlot::Placement))) {
        const QJsonArray ordinals = placement->params.value(QString(PhosphorRules::ActionParam::Zones)).toArray();
        const bool anyValid = std::any_of(ordinals.cbegin(), ordinals.cend(), [](const QJsonValue& v) {
            const int n = v.toInt(0);
            return n >= 1 && n <= PhosphorRules::MaxZoneOrdinal;
        });
        if (anyValid) {
            return;
        }
    }
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return;
    }
    const QString target = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    // screensMatch, not a raw compare: connector-name and EDID-id spellings can
    // name the SAME monitor, and a raw compare would treat that as a real move —
    // arming windowOutputMoveExpected and re-placing a window that is already
    // where the rule wants it.
    if (target.isEmpty() || PhosphorScreens::ScreenIdentity::screensMatch(target, screenId)) {
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
    // the target screen's, then clamp so the whole frame fits. Preserves "the same
    // spot on the other monitor" across differing resolutions; an unknown /
    // degenerate source area falls back to the target's top-left. The SIZE is
    // carried over unchanged unless the target work area is smaller in that axis,
    // in which case it shrinks to fit rather than overflowing the monitor.
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
    Q_EMIT windowOutputMoveExpected(windowId, target, screenId);
    Q_EMIT applyGeometryRequested(windowId, x, y, w, h, QString(), target, false);
}

QString WindowTrackingAdaptor::applyOpenRoutingForTiling(const QString& windowId, const QString& screenId)
{
    if (!m_ruleStore) {
        return QString();
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return QString();
    }
    query->screenId = screenId;
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitScreenStamped);

    // RouteToDesktop is engine-neutral — emit it for autotile windows too.
    emitRouteToDesktopIfMatched(resolved, windowId);

    // RouteToScreen: redirect the window onto a different ENGINE-OWNED monitor
    // (autotile or scrolling). The snap open path handles snap-mode targets
    // itself (the placement directive), so here we only honour a target whose
    // mode an engine claims — a snap or disabled target is left to the
    // window's spawn screen (cross-engine routing is out of scope). Returning the target tells the caller to insert
    // the window into that screen's tiling state; the output-move marker stops the
    // effect from re-processing the resulting outputChanged as a fresh open.
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return QString();
    }
    const QString target = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    // screensMatch for the same reason the snap twin uses it: a differently
    // spelled id for the SAME monitor must read as "already there". A raw
    // compare would return the target as a distinct screen and the caller would
    // key tiling state under a second name for one output.
    if (target.isEmpty() || !m_layoutManager || PhosphorScreens::ScreenIdentity::screensMatch(target, screenId)) {
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
    Q_EMIT windowOutputMoveExpected(windowId, target, screenId);
    return target;
}

} // namespace PlasmaZones
