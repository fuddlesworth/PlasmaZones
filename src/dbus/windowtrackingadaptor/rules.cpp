// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — rule-driven restore predicates and per-window params
//
// Per-window rule resolution for the restore and appearance paths: the
// evaluator constructor, the mode-neutral screen resolver, the contextual
// query builder, the floated-position / zone / size restore predicates, the
// unfloat-fallback predicate, the screen and mode context stampers, the
// open-float gate, the tab-colour / drop-indicator / scrolling-open param
// builders, and the colour-scheme memo invalidator. The placement-zone
// resolution and screen/desktop open-routing half lives in
// rules_placement.cpp; the admission tests both halves share are in
// rules_admission.h.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "rules_admission.h"

#include "core/interfaces/isettings.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/RuleStore.h>

#include <functional>

namespace PlasmaZones {

using namespace RuleAdmission;

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

/// The Field::Mode MATCH vocabulary for a placement mode — NOT the SetEngineMode
/// action vocabulary, which spells the middle one "autotile". The one spelling
/// site in this file: every resolver that stamps Mode goes through here, so a
/// token cannot drift between the stampers and the scrolling open path.
QString modeMatchToken(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return QStringLiteral("tiling");
    case PhosphorZones::AssignmentEntry::Scrolling:
        return QStringLiteral("scrolling");
    case PhosphorZones::AssignmentEntry::Snapping:
        break;
    }
    return QStringLiteral("snapping");
}

/// Read one hex-colour action slot into @p out under @p key. Shared by the
/// tab-colour and drop-indicator param builders. An empty value means "no
/// override" on these paths, not "clear to nothing" — an empty colour
/// reaching the overlay would read as the theme fallback anyway, so
/// dropping it keeps the map honest about what matched. Shape-checked for
/// the same reason the zones-layer twin is
/// (layoutregistry_contextresolve.cpp): the value goes through to a QML
/// `color` property verbatim, where anything unparseable renders as an
/// invalid colour rather than falling back to the theme; the descriptors
/// already enforce hex-only, so this only catches a payload that reached
/// the store without passing them.
void readHexColorSlot(const PhosphorRules::ResolvedActions& resolved, QLatin1StringView slot, const QString& key,
                      QVariantMap& out)
{
    const auto action = resolved.slot(QString(slot));
    if (!action) {
        return;
    }
    const QString value = action->params.value(QString(PhosphorRules::ActionParam::Value)).toString();
    if (isHexColorString(value)) {
        out.insert(key, value);
    }
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

QString WindowTrackingAdaptor::resolveScreenForWindow(const QString& windowId) const
{
    // Mode-neutral: the service's accessor is snap-only (it reads the owning
    // SnapState), so on its own it returns empty for every tiling-tracked
    // window and for any window that has not been placed yet. Fall through to
    // each engine's own tracker, which is what lastActiveScreenName() already
    // does for the focused window — the difference is that this resolves the
    // screen of the window being ASKED about, never the focused one.
    //
    // Order is deliberate: the service first, so windows it already resolves
    // keep the canonicalizing lookup (issue #628 composite-id skew) and nothing
    // that resolves today changes. The engine fallbacks only fill cases that
    // previously came back empty.
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
    if (m_scrollEngine) {
        const QString tracked = m_scrollEngine->screenForTrackedWindow(windowId);
        if (!tracked.isEmpty()) {
            return tracked;
        }
    }
    return QString();
}

std::optional<PhosphorRules::WindowQuery>
WindowTrackingAdaptor::buildContextualRuleQuery(const QString& windowId, const QString& screenIdHint) const
{
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return std::nullopt;
    }
    // Screen-derived context fields. WindowRegistry metadata carries no screen,
    // so the trio can only be stamped here: the open path knows the screen the
    // window is landing on (screenIdHint, which may be a route target that
    // differs from where the window currently sits), and every other path reads
    // the live screen-for-window resolution. An unresolvable screen stamps
    // nothing, leaving the fields as buildRuleQueryForWindow left them rather
    // than stamping a guess.
    const QString screenId = screenIdHint.isEmpty() ? resolveScreenForWindow(windowId) : screenIdHint;
    stampScreenContext(*query, screenId);
    return query;
}

bool WindowTrackingAdaptor::shouldRestoreFloatedPosition(const QString& windowId,
                                                         PhosphorZones::AssignmentEntry::Mode mode, bool useCache,
                                                         const QString& screenIdHint)
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
    const std::optional<PhosphorRules::WindowQuery> query =
        buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
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
    // The UNCACHED branch (the autotile wiring): the predicate also runs
    // mid-session, from insertWindow via backfillWindows, where the memoised
    // open verdict may be stale — and it carries a screen hint, because a
    // hintless resolveScreenForWindow consults the service and the snap engine
    // before the autotile engine, so stale snap state would resolve the WRONG
    // screen. The fresh query is stamped with the hinted screen's context and
    // resolved outside the shared memo, so it can neither read nor poison it.
    if (!useCache) {
        PhosphorRules::WindowQuery hinted = *query;
        stampScreenContext(hinted, screenIdHint.isEmpty() ? resolveScreenForWindow(windowId) : screenIdHint);
        const PhosphorRules::ResolvedActions freshResolved =
            m_ruleEvaluator->resolveFiltered(hinted, admitWith(admissionForStamped(hinted), hinted));
        if (const std::optional<PhosphorRules::RuleAction> action =
                freshResolved.slot(QString(PhosphorRules::ActionSlot::RestorePosition))) {
            return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
        }
        return globalDefault;
    }
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitWith(&admitScreenStamped, *query));
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
    const std::optional<PhosphorRules::WindowQuery> query =
        buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return globalDefault;
    }
    ensureRuleEvaluator();
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveCachedFiltered(windowId, *query, admitWith(&admitScreenStamped, *query));
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
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
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
    // Filter picked from what the stamp actually landed (an empty screen or
    // missing registry leaves fields unstamped, and admitting rules that
    // reference an unstamped field inverts negated leaves — see the helper).
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, admitWith(admissionForStamped(*query), *query));
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestoreSizeOnUnsnap))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldUnfloatFallbackToZone(const QString& windowId, const QString& screenId)
{
    // A matched SetUnfloatFallbackToZone rule wins, otherwise the global
    // setting decides — the rule ?? config layering the SnapEngine's
    // UnfloatFallbackPredicate contract names.
    const bool globalDefault = m_settings->snapUnfloatFallbackToZone();
    if (!m_ruleStore) {
        return globalDefault;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return globalDefault;
    }
    // Stamp the CALLER's screen, not screenForWindow: the engine hands us the
    // RESOLVED restore screen (tracked float screen with output-unplugged
    // fallback), which is where the fallback zone would land — the screen a
    // ScreenId-scoped rule should be judged against.
    stampScreenAndMode(*query, windowId, screenId);
    ensureRuleEvaluator();
    // Mid-session resolver: an unfloat fires long after open, so the same
    // uncached-resolve reasoning as shouldRestoreSizeOnUnsnap above applies
    // verbatim (resolveCached would return the open-time verdict).
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, admitWith(admissionForStamped(*query), *query));
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::UnfloatFallbackToZone))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

void WindowTrackingAdaptor::stampScreenContext(PhosphorRules::WindowQuery& query, const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return;
    }
    query.screenId = screenId;
    if (!m_layoutManager) {
        return;
    }
    // ActiveLayout is the layout active on that SCREEN right now — the id
    // assignmentIdForScreen resolves for the screen's current desktop and
    // activity, which is the same id the windowless context cascade stamps and
    // the same one the daemon publishes to the KWin effect. Keeping all three on
    // one definition is the point: `ActiveLayout Equals <uuid>` has to mean the
    // same thing whether it sits on a context rule, a daemon-resolved window
    // rule, or an effect-resolved appearance rule.
    //
    // Deliberately NOT the window's own desktop/activity (contrast Mode below,
    // which stampScreenAndMode resolves through the window's context). The
    // effect resolves this field from a per-SCREEN cache the daemon publishes
    // for each screen's current context and cannot do otherwise (it has no
    // layout registry), so stamping the window's context here would make the
    // identical leaf match on one side and not the other for any window sitting
    // on a desktop other than the one its screen is showing. The trade-off is
    // explicit: a window on desktop 2 whose screen currently shows desktop 1
    // matches desktop 1's layout.
    const int screenDesktop = currentDesktopForScreen(screenId);
    query.activeLayout =
        m_layoutManager->assignmentIdForScreen(screenId, screenDesktop, m_layoutManager->currentActivity());

    // ScreenOrientation was dead here for exactly the reason ActiveLayout was:
    // nothing stamped it on this side, and the effect resolves only APPEARANCE
    // actions, so it never sees an open-path rule at all. The open-path actions
    // are resolved here and nowhere else — a rule pairing one of them with a
    // ScreenOrientation leaf matched nothing, and (the field being a
    // non-optional context field) a negated leaf matched everything.
    m_layoutManager->stampScreenOrientation(query, screenId);
}

void WindowTrackingAdaptor::stampScreenAndMode(PhosphorRules::WindowQuery& query, const QString& windowId,
                                               const QString& screenId)
{
    stampScreenContext(query, screenId);
    if (screenId.isEmpty() || !m_layoutManager) {
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
    query.mode = modeMatchToken(m_layoutManager->modeForScreen(screenId, desktop, activity));
}

bool WindowTrackingAdaptor::shouldFloatByRule(const QString& windowId, const QString& screenId)
{
    // Float is purely rule-driven: there is no global "float on open" setting, so
    // absent a matching rule the answer is "do not float".
    if (!m_ruleStore) {
        return false;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return false;
    }
    // Pin the opening screen and the mode it resolves to; without them a rule
    // pairing ScreenId or Mode with Float never matches, a silent inertness
    // since the rules editor offers exactly that pairing. An empty screenId is
    // legal (the autotile float predicate is consulted mid-session through
    // insertShouldFloat for already-tracked windows): resolve the window's
    // screen from the service and the engine trackers before giving up.
    stampScreenAndMode(*query, windowId, screenId.isEmpty() ? resolveScreenForWindow(windowId) : screenId);

    ensureRuleEvaluator();
    // UNCACHED, for the same reason scrollOpenRuleParams is: resolveCached is
    // keyed on (windowId, ruleSet revision) ALONE, so on a hit the freshly
    // built query is ignored — including the ScreenId and Mode stamped above.
    // The first screen a window was asked about would then decide the verdict
    // for its whole lifetime, and a Mode-paired rule would answer for the
    // wrong mode. One resolve per open is the cost. Filter picked from what
    // the stamp actually landed, same as shouldRestoreSizeOnUnsnap.
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, admitWith(admissionForStamped(*query), *query));
    // The Float action carries free-form params (no Value key), so the verdict is
    // the PRESENCE of the filled slot, not a bool payload.
    return resolved.slot(QString(PhosphorRules::ActionSlot::Float)).has_value();
}

QVariantMap WindowTrackingAdaptor::tabColorRuleParams(const QString& windowId)
{
    if (!m_ruleStore) {
        return {};
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
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
    // The colour-scheme token is in the key for the same reason with a
    // different trigger: it moves on a light/dark flip with no rules edit, so
    // the revision does not budge (see the TabColorMemoEntry doc).
    const quint64 revision = m_ruleStore->ruleSet().revision();
    // Shadow-keyed so the writer and the windowClosed remover share one key
    // space BY CONSTRUCTION — the close path removes by shadowWindowId, and
    // a raw-keyed entry would survive until the pruneStaleWindows sweep.
    // (The sole caller today passes canonical ids, so this is pinning the
    // pairing structurally rather than fixing a live leak.)
    const QString memoKey = shadowWindowId(windowId);
    const auto memoIt = m_tabColorMemo.constFind(memoKey);
    if (memoIt != m_tabColorMemo.constEnd() && memoIt->revision == revision && memoIt->title == query->title
        && memoIt->captionNormal == query->captionNormal && memoIt->virtualDesktop == query->virtualDesktop
        && memoIt->activity == query->activity && memoIt->colorScheme == query->colorScheme) {
        return memoIt->colors;
    }
    // admitNothingStamped, not admitScreenStamped: this query stamps NO
    // context field, so ScreenId joins the exclusions (the helper's doc
    // carries the negated-leaf rationale).
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, admitWith(&admitNothingStamped, *query));
    const QVariantMap colors = tabColorsFromResolved(resolved);
    m_tabColorMemo.insert(memoKey,
                          TabColorMemoEntry{revision, query->title, query->captionNormal, query->virtualDesktop,
                                            query->activity, query->colorScheme, colors});
    return colors;
}

void WindowTrackingAdaptor::invalidateRuleMemosForColorSchemeChange()
{
    // Only the tab-colour memo. This deliberately does NOT clear the shared
    // RuleEvaluator cache, and that restraint is load-bearing: the six cached
    // resolvers seed one memo keyed on (windowId, revision), and the ordering
    // invariant documented above shouldRestoreFloatedPosition requires a
    // ScreenId-STAMPING resolver to seed it first. Four of the six stamp and
    // run only at open; the other two are engine-injected predicates that run
    // mid-session on unfloat and restore. Dropping the cache under a live
    // session therefore hands the next seed to an unstamped predicate, which
    // writes an empty screenId and makes every ScreenId-scoped rule either
    // stop firing or, negated, fire for every window — until the next revision
    // bump. Nothing is lost by keeping it: those six slots are open-path
    // verdicts, and a colour-scheme flip does not re-run the open path.
    //
    // The tab-colour memo is the one that genuinely needs a flip to re-drive,
    // and its key now carries the scheme token, so this clear is a
    // determinism belt rather than the correctness mechanism.
    m_tabColorMemo.clear();
}

QVariantMap WindowTrackingAdaptor::tabColorsFromResolved(const PhosphorRules::ResolvedActions& resolved)
{
    QVariantMap out;
    // readHexColorSlot (shared with the drop-indicator builder) carries the
    // empty-means-no-override and shape-check rationale.
    readHexColorSlot(resolved, PhosphorRules::ActionSlot::TabColorActive, WindowColorKeys::activeColor(), out);
    readHexColorSlot(resolved, PhosphorRules::ActionSlot::TabColorInactive, WindowColorKeys::inactiveColor(), out);
    readHexColorSlot(resolved, PhosphorRules::ActionSlot::TabColorUrgent, WindowColorKeys::urgentColor(), out);
    return out;
}

QVariantMap WindowTrackingAdaptor::dropIndicatorRuleParams(const QString& windowId)
{
    if (!m_ruleStore) {
        return {};
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return {};
    }
    // Same NO-screen-stamp reasoning as tabColorRuleParams, and the same
    // consequence: a rule pairing a per-window drop colour with a ScreenId or
    // Mode condition is inert here by design, because the caller (drag start)
    // knows the window but the indicator can move between screens during the
    // drag it is being resolved for. "Recolour the indicator on this screen"
    // is spelled with the per-CONTEXT actions instead.
    //
    // NO memo either, unlike the tab colours: this resolves ONCE per drag
    // rather than per tab per relayout, so a cache would cost more in
    // invalidation correctness than the single resolve it saves.
    ensureRuleEvaluator();
    // admitNothingStamped: same no-stamp shape as the tab colours above.
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, admitWith(&admitNothingStamped, *query));
    QVariantMap out;
    // Keyed by the QML property names the drop-indicator slot reads, so the
    // overlay's layering is one lookup per property; readHexColorSlot
    // carries the shape-check rationale.
    readHexColorSlot(resolved, PhosphorRules::ActionSlot::DragDropIndicatorColor, WindowColorKeys::indicatorColor(),
                     out);
    readHexColorSlot(resolved, PhosphorRules::ActionSlot::DragDropIndicatorBorderColor,
                     WindowColorKeys::indicatorBorderColor(), out);
    return out;
}

QVariantMap WindowTrackingAdaptor::scrollOpenRuleParams(const QString& windowId, const QString& screenId)
{
    QVariantMap out;
    if (!m_ruleStore) {
        return out;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId, m_settings);
    if (!query) {
        return out;
    }
    // An empty opening screen cannot be stamped, and resolving with ScreenId
    // engaged-but-empty under the Mode-admitting filter below re-opens the
    // negated-leaf inversion (see admissionForStamped). Every live caller
    // passes the window's opening screen, so refusing the rule params here
    // is the honest degenerate answer, not a lost feature.
    if (screenId.isEmpty()) {
        return out;
    }
    // Pin the two context fields this path knows and buildRuleQueryForWindow
    // cannot: the window is opening on @p screenId, and it is opening on a
    // SCROLLING screen by construction (only ScrollEngine calls this). Without
    // them a user-authored open rule carrying a ScreenId or Mode condition
    // never matches, and those are the two conditions the settings editor
    // offers alongside the scrolling open actions.
    stampScreenContext(*query, screenId);
    query->mode = modeMatchToken(PhosphorZones::AssignmentEntry::Scrolling);

    ensureRuleEvaluator();
    // Deliberately UNCACHED, unlike the sibling predicates. resolveCached is
    // keyed on (windowId, ruleSet revision) alone, so seeding it from this
    // extra-stamped query would hand the screen-only placement resolver a
    // verdict built under a Mode condition it never asked for (and vice
    // versa, on whichever path resolves first). This is one resolve per
    // window open on a scrolling screen.
    const PhosphorRules::ResolvedActions resolved =
        m_ruleEvaluator->resolveFiltered(*query, admitWith(&admitScreenAndModeStamped, *query));
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
    if (const auto action = resolved.slot(QString(PhosphorRules::ActionSlot::OpenMaximized))) {
        out.insert(ScrollOpenKeys::maximized(),
                   action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool());
    }
    if (const auto action = resolved.slot(QString(PhosphorRules::ActionSlot::OpenFocused))) {
        out.insert(ScrollOpenKeys::focused(),
                   action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool());
    }
    return out;
}

} // namespace PlasmaZones
