// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Per-context rule resolution for LayoutRegistry — the read side of the
// assignment cascade. Contents, in file order: resolveAssignmentEntry, the
// gap / lock / default-assignment resolvers, the shared cascade-miss default
// tail (resolveDefaultAssignmentEntryForContext), the overlay resolver, the
// per-engine parameter resolvers (autotile tiling params + scrolling params),
// and the exact-context-rule finders (exactContextEntry, hasExactContextRule,
// findExactContextRule, exactContextRuleId). Split from
// layoutregistry_assignments.cpp for file-size; the assignment CRUD /
// mutators / query wrappers stay there.
//
// Phase 3b: per-context assignment is resolved on the unified Rule engine.
// See layoutregistry_assignments.cpp for the resolution-model overview.

#include <PhosphorZones/LayoutRegistry.h>

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/Rule.h>

#include <QColor>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <optional>

namespace PhosphorZones {

namespace PWR = PhosphorRules;

// See layoutregistry_assignments.cpp for the RuleHelpers rationale — the
// rule-shape classification / context helpers live in
// layoutregistry_rulehelpers.cpp / _p.h and are shared across the registry TUs.
using namespace RuleHelpers;

namespace {

/// True if @p s is one of the three hex shapes PhosphorRules' `hasHexColor`
/// admits: `#RGB`, `#RRGGBB` or `#AARRGGBB`. Kept as a local shape test rather
/// than `QColor::isValidColorName`, which is wider (SVG keywords, and the
/// longer `#RRRGGGBBB` forms) and would let "transparent" through to a QML
/// `color` property as an invisible indicator.
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

/// True if @p match POSITIVELY pins @p field to a value — an `Equals` leaf on
/// the field reachable without passing through a negation. Used to rank gap-rule
/// specificity (a per-monitor ScreenId pin beats a per-mode Mode pin). Unlike
/// `MatchExpression::referencesAnyField`, this deliberately does NOT count a leaf
/// inside a `None{}` negation: a rule matching "every screen EXCEPT X" does not
/// pin THIS screen, so it must not be ranked as a per-monitor override. `All`
/// recurses (a conjunction genuinely constrains the field); `Any` requires
/// EVERY child to pin it — `Any{ScreenId==X, AppId==Y}` can match with the
/// screen leaf FALSE, so one pinning child does not constrain the field. Stops
/// at None. Exotic non-`Equals` shapes fall through to the generic priority tier.
bool matchPinsFieldPositively(const PWR::MatchExpression& match, PWR::Field field)
{
    switch (match.kind()) {
    case PWR::MatchExpression::Kind::Leaf:
        return match.predicate().field == field && match.predicate().op == PWR::Operator::Equals;
    case PWR::MatchExpression::Kind::All:
        for (const PWR::MatchExpression& child : match.children()) {
            if (matchPinsFieldPositively(child, field)) {
                return true;
            }
        }
        return false;
    case PWR::MatchExpression::Kind::Any: {
        if (match.children().isEmpty()) {
            return false;
        }
        for (const PWR::MatchExpression& child : match.children()) {
            if (!matchPinsFieldPositively(child, field)) {
                return false;
            }
        }
        return true;
    }
    case PWR::MatchExpression::Kind::None:
        return false; // a negation does not positively pin the field
    }
    return false;
}

} // namespace

// ── Rule-backed cascade resolution ──────────────────────────────────────────

std::optional<AssignmentEntry> LayoutRegistry::resolveAssignmentEntry(const QString& screenId, int virtualDesktop,
                                                                      const QString& activity) const
{
    // Resolution is per-slot. There are four independent slots — engine mode,
    // snapping layout, tiling algorithm, scrolling template — each resolved
    // separately, so a payload-only rule (a SetSnappingLayout,
    // SetTilingAlgorithm or SetScrollingTemplate with NO SetEngineMode) sets
    // the payload for its engine in this context WITHOUT forcing the engine
    // mode. The engine mode is the global default's (or another rule's); the
    // payload slot is just filled.
    //
    // The winner per slot is simply the HIGHEST-PRIORITY matching rule carrying
    // that slot's action, ties broken by list order. There is no specificity
    // computation and no catch-all exclusion — `priority` is the only
    // precedence value, and a user rule authored at a higher priority than a
    // context assignment WILL win. This delegates to the shared
    // RuleEvaluator::highestPriorityMatch — the one place the descending-
    // priority, tie-break-by-list-order walk lives, so this resolver can never
    // drift from the evaluator's own ordering.
    //
    // If no slot matched at all it's a genuine miss (nullopt) and the caller
    // routes to the global default. If at least one slot matched, the entry's
    // base is the engine-mode rule (mode + the payload tokens that rule itself
    // carries, preserving mode-toggle losslessness); the per-slot payload
    // winners then override the snapping-layout / tiling-algorithm /
    // scrolling-template fields. When only a payload rule matched (no
    // engine-mode rule), the base is the global default for this context, so
    // the resolved mode is the default's and the payload rule merely fills its
    // slot.
    //
    // Hot-path cache via the shared revision-invalidated memoizer. The linear
    // walk is O(N rules × M predicates per match); overlay/OSD callers issue it
    // per cursor-move, and the connector + virtual-screen fallback chain in the
    // public resolvers triples that on a miss. A @c nullopt is cached too — a
    // genuine miss must not re-walk three times per cursor frame.
    //
    // Only the RULE-derived resolution is cached, keeping the cache a pure
    // function of the rule set (its revision-invalidation contract). The global
    // default — an external provider, not part of the rule set and not
    // revision-tracked — is folded in AFTER the cache so a default-setting
    // change (snapping toggled, a different default layout/algorithm) is
    // reflected immediately, with no rule-set revision bump (a settings edit
    // produces none). Caching the default-derived entry would otherwise pin a
    // stale mode/layout on the layout-only path until the next rule edit. The
    // fold-in is a handful of O(1) provider / per-context-slot reads, so the
    // expensive priority walk stays memoized.
    // Live tiled-window count for this context (nullopt when not actively
    // tiling). It is NOT part of the rule set, so it cannot ride the cache's
    // revision-invalidation contract like the global default does. Instead it
    // is folded into the cache KEY (the `mode` slot of this cache, which for the
    // assignment resolver carries only the count + orientation composite below) so a
    // count change yields a fresh entry rather than a stale hit, while
    // count-independent callers (overlay / OSD per cursor-move, where the count is
    // steady) keep hitting the cache.
    const std::optional<int> tiledCount =
        m_tiledWindowCountProvider ? m_tiledWindowCountProvider(screenId, virtualDesktop, activity) : std::nullopt;
    // Both the tiled count and the screen orientation are non-rule-set inputs the
    // stamped query depends on, so both must ride the cache KEY (the assignment
    // resolver stamps orientation but not activeLayout, so no "al:" component here).
    const QString orientationToken = screenOrientationToken(screenId);
    // The colour scheme rides the key for the same non-rule-set reason as the
    // orientation, and is stamped below — scheme-driven assignments (a
    // different layout for a dark desk setup) are legitimate, and the token
    // is palette-derived so there is no recursion hazard.
    const QString schemeToken = colorSchemeToken();
    const QString countCacheKey = (tiledCount ? (QLatin1String("twc:") + QString::number(*tiledCount)) : QString())
        + QLatin1String("|or:") + orientationToken + QLatin1String("|cs:") + schemeToken;

    const std::optional<RuleSlotResolution> rules = resolveCachedContext(
        m_contextResolveCache, m_contextResolveCacheRevision, screenId, virtualDesktop, activity, countCacheKey,
        [&]() -> std::optional<RuleSlotResolution> {
            PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            // The tiled count is supplied ONLY to the assignment resolver's
            // query, because algorithm switching is its only intended use.
            //
            // Every OTHER resolver therefore excludes Field::TiledWindowCount
            // STRUCTURALLY rather than relying on the field being absent.
            // "Absent means inert" is true for a positive leaf and FALSE for a
            // negated one: an absent field evaluates its leaf false, and
            // `None{}` matches when no child matched, so
            // `None{TiledWindowCount Equals 0}` would fire on EVERY context —
            // gapping, locking or restyling all of them. Same polarity trap as
            // Mode and ActiveLayout, same structural fix.
            query.tiledWindowCount = tiledCount;
            // Orientation is layout-independent, so it is stamped on EVERY context
            // query (unlike tiledWindowCount / activeLayout) — an orientation rule
            // can drive the assignment itself (portrait monitor → different layout).
            // Reuse the token already captured for the cache key (mirrors the
            // gap/lock/overlay lambdas) rather than re-reading the provider.
            query.screenOrientation = orientationToken;
            // Colour scheme: layout-independent like orientation, so it is
            // stamped on every context query including this cascade.
            query.colorScheme = schemeToken;
            // Field::ActiveLayout is deliberately NOT stamped here: the active
            // layout IS this resolver's output, and reading it (assignmentIdForScreen
            // → resolveAssignmentEntry) would recurse. So an ActiveLayout rule cannot
            // drive the layout assignment (a circular request); it is populated only
            // by the post-assignment resolvers (gap / lock / overlay).

            // Highest-priority matching rule per slot wins (ties by list order).
            // A rule that REFERENCES Field::ActiveLayout is structurally excluded from
            // the assignment path: activeLayout is left unstamped above, so such a
            // rule evaluates against a placeholder empty value. A positive leaf
            // (ActiveLayout Equals X) never matches that placeholder, but a negated
            // predicate (None{ActiveLayout Equals X}) WOULD spuriously match and force
            // a wrong assignment. Dropping any ActiveLayout-referencing rule here keeps
            // ActiveLayout a strictly post-assignment field regardless of predicate
            // polarity, not merely by relying on the empty-value coincidence.
            //
            // Field::Mode is excluded for exactly the same reason: these
            // resolvers are MODE-AGNOSTIC, so makeContextQuery leaves mode
            // unstamped, and WindowQuery::valueForField returns an ENGAGED
            // empty string for it — which a positive leaf never matches but a
            // negated None{Mode Equals "tiling"} does.
            // TiledWindowCount is deliberately NOT excluded here, unlike in the
            // six sibling resolvers. This is the one resolver that STAMPS it
            // (line above), so both polarities evaluate correctly and the
            // algorithm-switch feature it exists for — `TiledWindowCount
            // GreaterThan 1 → SetTilingAlgorithm bsp` — depends on rules
            // referencing it winning a slot here. Excluding it would leave the
            // provider, the cache key and the daemon's count-change re-resolve
            // hooks all wired to nothing.
            const auto slotMatch = [&](bool (*carriesSlot)(const PWR::Rule&)) -> const PWR::Rule* {
                return m_evaluator->highestPriorityMatch(query, [carriesSlot](const PWR::Rule& rule) {
                    // negatesAnyField(windowSourcedFields): a positive window
                    // leaf is inert here by design (absent field, leaf false),
                    // but a leaf under a `none{}` INVERTS on absence and would
                    // match every context. Table-derived, so a new window
                    // field can never silently miss the guard.
                    return carriesSlot(rule)
                        && !rule.match.referencesAnyField({PWR::Field::ActiveLayout, PWR::Field::Mode})
                        && !rule.match.negatesAnyField(PWR::windowSourcedFields());
                });
            };

            const PWR::Rule* modeRule = slotMatch(hasEngineModeAction);
            const PWR::Rule* snapRule = slotMatch(hasSnappingLayoutAction);
            const PWR::Rule* algoRule = slotMatch(hasTilingAlgorithmAction);
            const PWR::Rule* templateRule = slotMatch(hasScrollingTemplateAction);

            if (modeRule == nullptr && snapRule == nullptr && algoRule == nullptr && templateRule == nullptr) {
                return std::nullopt; // genuine miss — the caller routes to the default
            }

            // The engine-mode rule decides the mode (and carries its own
            // payload tokens, preserving mode-toggle losslessness); the
            // per-slot payload winners fill their own field. Each slot
            // independently took the highest-priority matching rule, so the
            // payload slots track their own winner regardless of the
            // engine-mode rule.
            RuleSlotResolution resolved;
            if (modeRule != nullptr) {
                resolved.modeEntry = entryFromRuleMatchActions(*modeRule);
            }
            if (snapRule != nullptr) {
                resolved.snappingLayout = entryFromRuleMatchActions(*snapRule).snappingLayout;
            }
            if (algoRule != nullptr) {
                resolved.tilingAlgorithm = entryFromRuleMatchActions(*algoRule).tilingAlgorithm;
            }
            if (templateRule != nullptr) {
                resolved.scrollingTemplate = entryFromRuleMatchActions(*templateRule).scrollingTemplateLayout;
            }
            return resolved;
        });

    if (!rules) {
        return std::nullopt; // genuine miss — the caller routes to the default
    }

    // Base: the engine-mode rule's decoded entry. With no engine-mode rule, the
    // live default for this context supplies the mode — a layout-only rule must
    // not force one. Resolved here, OUTSIDE the cache, so it tracks settings.
    AssignmentEntry entry = rules->modeEntry
        ? *rules->modeEntry
        : resolveDefaultAssignmentEntryForContext(screenId, virtualDesktop, activity);

    // Per-slot layout winners override their field.
    if (rules->snappingLayout) {
        entry.snappingLayout = *rules->snappingLayout;
    }
    if (rules->tilingAlgorithm) {
        entry.tilingAlgorithm = *rules->tilingAlgorithm;
    }
    if (rules->scrollingTemplate) {
        entry.scrollingTemplateLayout = *rules->scrollingTemplate;
    }
    return entry;
}

QString LayoutRegistry::rulesVisibleActiveLayoutId(const QString& screenId, int virtualDesktop,
                                                   const QString& activity) const
{
    // The assignment id, except that a Scrolling context with a resolved
    // template substitutes the PREFIXED "scrolling:<templateUuid>" — parity
    // with autotile's "autotile:<algorithmId>" stamp, so a rule can target
    // one specific template. Recursion-safe: scrollingTemplateForContext
    // re-enters the same memoized resolveAssignmentEntry that
    // assignmentIdForScreen just consulted, and the cascade root never calls
    // back into the gap/lock/overlay resolvers. The bare sentinel therefore
    // now means "scrolling with no template" to rules; "any scrolling
    // screen" is a Mode leaf.
    QString id = assignmentIdForScreen(screenId, virtualDesktop, activity);
    if (PhosphorLayout::LayoutId::isScrolling(id)) {
        const ScrollingTemplate templ = scrollingTemplateForContext(screenId, virtualDesktop, activity);
        if (templ.isValid()) {
            id = PhosphorLayout::LayoutId::makeScrollingId(templ.id.toString());
        }
    }
    return id;
}

ContextGapOverride LayoutRegistry::resolveContextGaps(const QString& screenId, int virtualDesktop,
                                                      const QString& activity, const QString& mode) const
{
    // This returns CONTEXT OVERRIDE rules only — the per-screen, per-desktop and
    // per-activity gap rules that sit ABOVE the per-layout tier in the geometry
    // cascade (getEffectiveInnerGap / getEffectiveOuterGaps tier 1). It is NOT
    // the cascade's default tier: the global default gap is CONFIG-backed (the
    // consumer's global inner/outer gap settings) and is read there, at the
    // cascade's default tier, with the per-layout override sitting between this
    // override layer and that default. The clean tiering is: context overrides
    // (here) → per-layout → global default (config) → compile default.
    //
    // Unlike resolveAssignmentEntry (single winning engine-mode rule), gap
    // overrides are read PER SLOT from the evaluator's ResolvedActions, so a
    // gap-only rule and, say, a separate engine-mode rule on the same context
    // compose — and two gap rules touching different slots both apply. No
    // engine-mode gate and no isValid() filter: a gap-only context rule is a
    // first-class override here.
    //
    // m_evaluator is a construction invariant (built in initCommon from the
    // required rule store, never reset), so it is dereferenced unguarded here
    // and in every sibling resolver — as resolveAssignmentEntry, the busiest
    // resolver in this file, already did.

    // The active layout AND the screen orientation are folded into the cache key
    // (see contextCacheKeyToken) so a Field::ActiveLayout / Field::ScreenOrientation
    // gap rule refreshes when either changes; both are stamped onto the query below.
    // Safe from recursion: rulesVisibleActiveLayoutId routes through resolveAssignmentEntry,
    // which never calls back into the gap resolver.
    const QString activeLayoutId = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    const QString orientationToken = screenOrientationToken(screenId);
    const QString schemeToken = colorSchemeToken();

    // Hot-path cache via the shared revision-invalidated memoizer: the geometry
    // path resolves the same context twice per op (zone padding + outer gaps)
    // and N× inside a multi-zone snap, all with identical arguments.
    return resolveCachedContext(
        m_contextGapCache, m_contextGapCacheRevision, screenId, virtualDesktop, activity,
        contextCacheKeyToken(mode, activeLayoutId, orientationToken, schemeToken), [&]() -> ContextGapOverride {
            ContextGapOverride gaps;
            // Thread the placement mode into the query so a per-mode `Mode
            // Equals "snapping"/"tiling"/"scrolling"` gap rule resolves for
            // the asking engine and stays inert for the others.
            PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity, mode);
            query.screenOrientation = orientationToken;
            query.activeLayout = activeLayoutId;
            query.colorScheme = schemeToken;

            // Resolve each gap slot from the highest-priority matching rule that
            // carries that slot's action. Any CATCH-ALL managed rule is EXCLUDED.
            // No current build writes one: the global default gaps are config-backed
            // and the managed baseline gap rule was retired (ConfigDefaults::
            // baselineGapRuleId survives only as a startup strip target). The guard
            // is LEGACY DEFENCE — a rules.json written by an older version can still
            // carry that baseline, and it may be read here before the daemon's strip
            // pass has run. Were it admitted, its catch-all match would fill every
            // gap slot with the old global default and masquerade as a top-tier
            // context override, shadowing both the per-layout tier and the live
            // config default that must sit below context overrides. SCREEN-scoped gap rules
            // (per-monitor overrides authored via the Appearance page's monitor
            // scope) have a SPECIFIC match, so they are NOT catch-all and DO
            // participate here as context overrides. Per slot, the winner is chosen
            // by MATCH SPECIFICITY first (ScreenId-pinned > Mode-pinned > other),
            // with priority breaking ties within a tier — so a per-monitor override
            // outranks a global per-mode gap regardless of their raw priorities (see
            // winningAction below). The catch-all baseline is excluded by the
            // "carries a gap slot, not the catch-all baseline" filter.
            //
            // ONE walk PER SPECIFICITY TIER, not per slot. resolveFiltered
            // accumulates the first action filling EACH slot in the same
            // descending-priority, tie-break-by-list-order walk
            // highestPriorityMatch performs, so a single pass answers all seven
            // slots independently. Resolving them one at a time cost up to 21
            // O(rules × predicates) walks per cache miss on the geometry hot
            // path, which the overlay / OSD callers hit per cursor-move.
            const PWR::ActionRegistry& registry = PWR::ActionRegistry::instance();
            // The seven slots this resolver reads. A rule carrying none of them
            // is not admitted, so an unrelated rule can neither be ranked into a
            // tier nor terminate the walk with an Exclude action.
            static const QSet<QString> gapSlots = {
                QString(PWR::ActionSlot::InnerGap),           QString(PWR::ActionSlot::OuterGap),
                QString(PWR::ActionSlot::UsePerSideOuterGap), QString(PWR::ActionSlot::OuterGapTop),
                QString(PWR::ActionSlot::OuterGapBottom),     QString(PWR::ActionSlot::OuterGapLeft),
                QString(PWR::ActionSlot::OuterGapRight),
            };
            // A mode-agnostic caller (empty @p mode) leaves WindowQuery::mode
            // unstamped, and valueForField returns an ENGAGED empty string
            // for it — which a positive `Mode Equals x` leaf never matches
            // but a negated `None{Mode Equals x}` matches on EVERY context.
            // So exclude Field::Mode structurally in that case, exactly as
            // the four mode-agnostic resolvers do, rather than relying on
            // the empty value to coincide with a non-match.
            const bool modeAgnostic = mode.isEmpty();
            const auto carries = [&registry, modeAgnostic](const PWR::Rule& rule) {
                if (rule.managed && rule.match.isCatchAll()) {
                    return false;
                }
                if (modeAgnostic && rule.match.referencesAnyField({PWR::Field::Mode})) {
                    return false;
                }
                // TiledWindowCount is never stamped on a gap query, and an
                // ABSENT field makes a leaf evaluate false — which makes
                // `None{TiledWindowCount Equals 0}` evaluate TRUE for every
                // context. Excluded structurally, exactly as Mode is.
                if (rule.match.referencesAnyField({PWR::Field::TiledWindowCount})) {
                    return false;
                }
                // Same polarity trap for every WINDOW-sourced field, but
                // scoped to NEGATED references only: a positive window
                // leaf is inert here by design, while a leaf under a
                // `none{}` inverts on absence and the rule would gap
                // every context.
                if (rule.match.negatesAnyField(PWR::windowSourcedFields())) {
                    return false;
                }
                for (const PWR::RuleAction& a : rule.actions) {
                    if (gapSlots.contains(registry.slotFor(a))) {
                        return true;
                    }
                }
                return false;
            };
            // Resolve each slot by MATCH SPECIFICITY, not raw priority: a
            // per-monitor (ScreenId-pinned) gap override is more specific than
            // a per-mode (Mode-pinned) one, which is more specific than any
            // other context rule carrying the slot. Tiers are consulted in
            // turn so a per-monitor override beats a global per-mode gap (the
            // v4 cascade, where the monitor-specific value won), with priority
            // breaking ties within a tier. Specificity uses
            // matchPinsFieldPositively (an Equals leaf not under a negation),
            // so a "every screen except X" negated rule is NOT mis-ranked as a
            // per-monitor override and instead falls through to the generic
            // priority tier. Only the gap cascade is specificity-ordered;
            // appearance slots resolve by priority alone in the effect.
            const PWR::ResolvedActions screenTier =
                m_evaluator->resolveFiltered(query, [&carries](const PWR::Rule& rule) {
                    return carries(rule) && matchPinsFieldPositively(rule.match, PWR::Field::ScreenId);
                });
            // The Mode-pinned tier is PROVABLY EMPTY for a mode-agnostic
            // caller: carries() already rejects every Mode-referencing rule
            // there, and a Mode-pinned rule references Mode by definition. Skip
            // the walk rather than pay it for a guaranteed-empty result.
            const PWR::ResolvedActions modeTier = modeAgnostic
                ? PWR::ResolvedActions{}
                : m_evaluator->resolveFiltered(query, [&carries](const PWR::Rule& rule) {
                      return carries(rule) && matchPinsFieldPositively(rule.match, PWR::Field::Mode);
                  });
            const PWR::ResolvedActions anyTier = m_evaluator->resolveFiltered(query, carries);
            const auto winningAction = [&screenTier, &modeTier,
                                        &anyTier](QLatin1StringView slot) -> std::optional<PWR::RuleAction> {
                const QString slotId = QString(slot);
                if (const auto action = screenTier.slot(slotId)) {
                    return action;
                }
                if (const auto action = modeTier.slot(slotId)) {
                    return action;
                }
                return anyTier.slot(slotId);
            };
            // qRound(toDouble()), not toInt(): QJsonValue::toInt() returns its
            // default for a non-whole number, so a hand-edited `"value": 8.5`
            // would silently resolve gap 0 while the load validator (a plain
            // range check) accepts it.
            const auto readInt = [&winningAction](QLatin1StringView slot, std::optional<int>& out) {
                if (const auto action = winningAction(slot)) {
                    out = qRound(action->params.value(PWR::ActionParam::Value).toDouble());
                }
            };
            const auto readBool = [&winningAction](QLatin1StringView slot, std::optional<bool>& out) {
                if (const auto action = winningAction(slot)) {
                    out = action->params.value(PWR::ActionParam::Value).toBool();
                }
            };
            readInt(PWR::ActionSlot::InnerGap, gaps.innerGap);
            readInt(PWR::ActionSlot::OuterGap, gaps.outerGap);
            readBool(PWR::ActionSlot::UsePerSideOuterGap, gaps.usePerSideOuterGap);
            readInt(PWR::ActionSlot::OuterGapTop, gaps.outerGapTop);
            readInt(PWR::ActionSlot::OuterGapBottom, gaps.outerGapBottom);
            readInt(PWR::ActionSlot::OuterGapLeft, gaps.outerGapLeft);
            readInt(PWR::ActionSlot::OuterGapRight, gaps.outerGapRight);
            return gaps;
        });
}

bool LayoutRegistry::resolveContextLocked(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Mirror resolveContextGaps: a per-slot read off the evaluator's
    // ResolvedActions, not the single-winner assignment walk. The Locked slot
    // is filled by the highest-priority matching context rule carrying a
    // LockContext action; we report its boolean value. No engine-mode gate —
    // a lock-only context rule is a first-class overlay.

    // Active layout + orientation folded into the cache key + stamped onto the
    // query, so a Field::ActiveLayout / ScreenOrientation lock rule works and
    // refreshes when either changes.
    const QString activeLayoutId = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    const QString orientationToken = screenOrientationToken(screenId);
    const QString schemeToken = colorSchemeToken();

    // Hot-path cache via the shared revision-invalidated memoizer: the lock
    // check runs per cursor-move while a selector is open and on every
    // layout-switch attempt.
    return resolveCachedContext(
        m_contextLockCache, m_contextLockCacheRevision, screenId, virtualDesktop, activity,
        contextCacheKeyToken(QString(), activeLayoutId, orientationToken, schemeToken), [&]() -> bool {
            PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            query.screenOrientation = orientationToken;
            query.activeLayout = activeLayoutId;
            query.colorScheme = schemeToken;
            // Filtered highestPriorityMatch, not the unfiltered
            // resolve(): this resolver is mode-agnostic, so mode is
            // unstamped and reads back as an ENGAGED empty string —
            // a negated None{Mode Equals "tiling"} on a LockContext
            // rule would spuriously match and lock the context. Same
            // structural exclusion the assignment and
            // default-assignment resolvers apply.
            const PWR::Rule* rule = m_evaluator->highestPriorityMatch(query, [](const PWR::Rule& r) {
                // TiledWindowCount excluded for the
                // same negation-polarity reason as
                // Mode: unstamped here, so a
                // negated leaf on it matches every
                // context and locks all of them. Window-sourced
                // fields carry the negation-scoped form of the same
                // guard: a positive window leaf is inert by design,
                // but `none{AppId == x}` would lock EVERY context.
                if (r.match.referencesAnyField({PWR::Field::Mode, PWR::Field::TiledWindowCount})
                    || r.match.negatesAnyField(PWR::windowSourcedFields())) {
                    return false;
                }
                for (const PWR::RuleAction& action : r.actions) {
                    if (action.type == QLatin1String(PWR::ActionType::LockContext)) {
                        return true;
                    }
                }
                return false;
            });
            if (!rule) {
                return false;
            }
            for (const PWR::RuleAction& action : rule->actions) {
                if (action.type == QLatin1String(PWR::ActionType::LockContext)) {
                    return action.params.value(PWR::ActionParam::Value).toBool();
                }
            }
            return false;
        });
}

std::optional<bool> LayoutRegistry::resolveContextDefaultAssignment(const QString& screenId, int virtualDesktop,
                                                                    const QString& activity) const
{
    // Mirror resolveContextLocked: a per-slot read off the evaluator's
    // ResolvedActions, not the single-winner assignment walk. The
    // DefaultAssignment slot is filled by the highest-priority matching context
    // rule carrying a DefaultLayoutAssignment action; we report its boolean
    // value (true = allow / force the default through, false = suppress). No
    // engine-mode gate — a default-assignment-only context rule is a first-class
    // overlay. std::nullopt means "no override rule" — the caller follows the
    // global setting.

    // Orientation folded into the cache key so a Field::ScreenOrientation
    // default-assignment rule refreshes on rotation (activeLayout is deliberately
    // NOT stamped/folded here — see the recursion note in the lambda below).
    const QString orientationToken = screenOrientationToken(screenId);
    const QString schemeToken = colorSchemeToken();
    return resolveCachedContext(
        m_contextDefaultAssignmentCache, m_contextDefaultAssignmentCacheRevision, screenId, virtualDesktop, activity,
        contextCacheKeyToken(QString(), QString(), orientationToken, schemeToken), [&]() -> std::optional<bool> {
            PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            query.screenOrientation = orientationToken; // reuse the cache-key token
            query.colorScheme = schemeToken;
            // Field::ActiveLayout NOT stamped here: this resolver is part of the
            // assignment cascade (assignmentIdForScreen reaches it via
            // resolveDefaultAssignmentEntryForContext), so stamping the active
            // layout here would recurse. See resolveAssignmentEntry.
            //
            // Because activeLayout is unstamped, an ActiveLayout-referencing rule
            // must be structurally excluded from this no-stamp resolver too (the
            // exact symmetry resolveAssignmentEntry's slotMatch enforces): a negated
            // None{ActiveLayout Equals X} predicate would otherwise spuriously match
            // the empty placeholder and wrongly force/suppress the default. Use a
            // filtered highestPriorityMatch rather than the unfiltered resolve().
            const PWR::Rule* rule = m_evaluator->highestPriorityMatch(query, [](const PWR::Rule& r) {
                // Plus the negation-scoped window-field guard: a `none{}` over
                // an unstamped window field matches every context and would
                // wrongly force/suppress the default everywhere.
                if (r.match.referencesAnyField(
                        {PWR::Field::ActiveLayout, PWR::Field::Mode, PWR::Field::TiledWindowCount})
                    || r.match.negatesAnyField(PWR::windowSourcedFields())) {
                    return false;
                }
                for (const PWR::RuleAction& action : r.actions) {
                    if (action.type == QLatin1String(PWR::ActionType::DefaultLayoutAssignment)) {
                        return true;
                    }
                }
                return false;
            });
            if (rule != nullptr) {
                for (const PWR::RuleAction& action : rule->actions) {
                    if (action.type == QLatin1String(PWR::ActionType::DefaultLayoutAssignment)) {
                        return action.params.value(PWR::ActionParam::Value).toBool();
                    }
                }
            }
            return std::nullopt;
        });
}

std::optional<bool> LayoutRegistry::resolveContextOsdEnabled(const QString& screenId, int virtualDesktop,
                                                             const QString& activity) const
{
    // Mirror resolveContextLocked: a per-slot read off the evaluator, not the
    // single-winner assignment walk. The OsdEnabled slot is filled by the
    // highest-priority matching context rule carrying a SetOsdEnabled action;
    // we report its boolean value. std::nullopt means "no override rule" —
    // the caller follows the global OSD toggles.
    //
    // Unlike resolveContextDefaultAssignment, activeLayout IS stamped (and
    // folded into the cache key): OSD resolution never runs inside the
    // assignment cascade, so there is no recursion hazard, and a rule
    // scoping OSD visibility on the active layout is legitimate.
    const QString activeLayoutId = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    const QString orientationToken = screenOrientationToken(screenId);
    const QString schemeToken = colorSchemeToken();
    return resolveCachedContext(
        m_contextOsdCache, m_contextOsdCacheRevision, screenId, virtualDesktop, activity,
        contextCacheKeyToken(QString(), activeLayoutId, orientationToken, schemeToken), [&]() -> std::optional<bool> {
            PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            query.screenOrientation = orientationToken;
            query.activeLayout = activeLayoutId;
            query.colorScheme = schemeToken;
            // Same structural exclusions as resolveContextLocked: Mode and
            // TiledWindowCount are unstamped here, so a negated leaf on
            // either would spuriously match every context; window-sourced
            // fields carry the negation-scoped form of the same guard.
            const PWR::Rule* rule = m_evaluator->highestPriorityMatch(query, [](const PWR::Rule& r) {
                if (r.match.referencesAnyField({PWR::Field::Mode, PWR::Field::TiledWindowCount})
                    || r.match.negatesAnyField(PWR::windowSourcedFields())) {
                    return false;
                }
                for (const PWR::RuleAction& action : r.actions) {
                    if (action.type == QLatin1String(PWR::ActionType::SetOsdEnabled)) {
                        return true;
                    }
                }
                return false;
            });
            if (rule != nullptr) {
                for (const PWR::RuleAction& action : rule->actions) {
                    if (action.type == QLatin1String(PWR::ActionType::SetOsdEnabled)) {
                        return action.params.value(PWR::ActionParam::Value).toBool();
                    }
                }
            }
            return std::nullopt;
        });
}

AssignmentEntry LayoutRegistry::resolveDefaultAssignmentEntryForContext(const QString& screenId, int virtualDesktop,
                                                                        const QString& activity) const
{
    // Single cascade-miss tail for every per-context resolver. The per-context
    // DefaultLayoutAssignment override (if present) wins over the global
    // suppress baseline:
    //   - false (suppress) → invalid entry: this context gets no default.
    //   - true  (allow)    → raw synth: force the default through even when the
    //                        global suppress setting is on.
    //   - no override      → the global-gated resolveDefaultAssignmentEntry.
    if (const auto contextOverride = resolveContextDefaultAssignment(screenId, virtualDesktop, activity)) {
        return *contextOverride ? resolveDefaultAssignmentEntryRaw() : AssignmentEntry{};
    }
    return resolveDefaultAssignmentEntry();
}

ContextOverlayOverride LayoutRegistry::resolveContextOverlay(const QString& screenId, int virtualDesktop,
                                                             const QString& activity) const
{
    // Per-slot read across all matching context rules (mirrors resolveContextGaps):
    // independent overlay-shader / overlay-style rules compose, and
    // each populated field overrides the active layout's own value at the overlay
    // build site. No engine-mode gate — an overlay-only context rule is first-class.

    // Active layout + orientation folded into the cache key + stamped onto the
    // query, so a Field::ActiveLayout / ScreenOrientation overlay rule works and
    // refreshes when either changes.
    const QString activeLayoutId = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    const QString orientationToken = screenOrientationToken(screenId);
    const QString schemeToken = colorSchemeToken();

    return resolveCachedContext(
        m_contextOverlayCache, m_contextOverlayCacheRevision, screenId, virtualDesktop, activity,
        contextCacheKeyToken(QString(), activeLayoutId, orientationToken, schemeToken),
        [&]() -> ContextOverlayOverride {
            ContextOverlayOverride overlay;
            PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
            query.screenOrientation = orientationToken;
            query.activeLayout = activeLayoutId;
            query.colorScheme = schemeToken;
            // Mode-referencing rules are structurally excluded: this resolver
            // is mode-agnostic, so mode is unstamped and a negated
            // None{Mode Equals "tiling"} would spuriously match and apply an
            // overlay override the user scoped to another mode. Same rule the
            // assignment / default-assignment / lock resolvers enforce.
            const PWR::ResolvedActions resolved = m_evaluator->resolveFiltered(query, [](const PWR::Rule& r) {
                // TiledWindowCount joins Mode: neither is stamped on an overlay
                // query, and an absent field makes a leaf false, so a negated
                // leaf on it matches every context and restyles every screen.
                // Window-sourced fields get the negation-scoped guard for the
                // same inversion (positive leaves stay inert by design).
                return !r.match.referencesAnyField({PWR::Field::Mode, PWR::Field::TiledWindowCount})
                    && !r.match.negatesAnyField(PWR::windowSourcedFields());
            });

            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayShader))) {
                const QString id = action->params.value(PWR::ActionParam::EffectId).toString();
                if (!id.isEmpty()) {
                    overlay.shaderId = id;
                    // Optional shader uniform overrides — empty when the rule
                    // overrides only the shader id (shader defaults apply).
                    overlay.shaderParams = action->params.value(PWR::ActionParam::Params).toObject().toVariantMap();
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayStyle))) {
                // Wire token → OverlayDisplayMode int so consumers compare against
                // the same enum Layout::overlayDisplayMode() exposes (0 =
                // ZoneRectangles, 1 = LayoutPreview).
                const QString token = action->params.value(PWR::ActionParam::Value).toString();
                if (token == PWR::OverlayStyleToken::Rectangles) {
                    overlay.style = 0;
                } else if (token == PWR::OverlayStyleToken::Preview) {
                    overlay.style = 1;
                }
            }
            // Appearance overrides — each fills its own optional so an unset
            // property falls through to the global config value at the consumer.
            // Colours are parsed from the `#AARRGGBB` wire hex; QColor reads a
            // 9-digit hex alpha-first, matching the picker's toHexArgb output. The
            // descriptor validators reject malformed values at load; the guards here
            // are defense in depth so a hand-edited rules.json that slipped a bad value
            // can only fall through to config or a sane bound, never apply a broken
            // override. Every slot therefore checks the payload's TYPE before it
            // reads: colours must parse to a valid QColor, numerics must be
            // isDouble() and bools isBool(), or the slot falls through to config.
            // Without the type gate an absent or string Value read back as 0 and
            // APPLIED — opacity 0 (an invisible overlay), showZoneNumbers off —
            // which is the opposite of falling through. On top of the type gate,
            // opacities are clamped to [0, 1] and border width / radius are
            // qRound()ed then floored at 0 (negatives are nonsensical, and
            // QJsonValue::toInt() would have turned a fractional 2.5 into 0; the
            // load validator enforces the upper bounds, which the shared constants
            // own).
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayHighlightColor))) {
                if (const QColor c(action->params.value(PWR::ActionParam::Value).toString()); c.isValid()) {
                    overlay.highlightColor = c;
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayInactiveColor))) {
                if (const QColor c(action->params.value(PWR::ActionParam::Value).toString()); c.isValid()) {
                    overlay.inactiveColor = c;
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayBorderColor))) {
                if (const QColor c(action->params.value(PWR::ActionParam::Value).toString()); c.isValid()) {
                    overlay.borderColor = c;
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayActiveOpacity))) {
                if (const QJsonValue v = action->params.value(PWR::ActionParam::Value); v.isDouble()) {
                    overlay.activeOpacity = qBound(0.0, v.toDouble(), 1.0);
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayInactiveOpacity))) {
                if (const QJsonValue v = action->params.value(PWR::ActionParam::Value); v.isDouble()) {
                    overlay.inactiveOpacity = qBound(0.0, v.toDouble(), 1.0);
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayBorderWidth))) {
                if (const QJsonValue v = action->params.value(PWR::ActionParam::Value); v.isDouble()) {
                    overlay.borderWidth = std::max(0, qRound(v.toDouble()));
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayBorderRadius))) {
                if (const QJsonValue v = action->params.value(PWR::ActionParam::Value); v.isDouble()) {
                    overlay.borderRadius = std::max(0, qRound(v.toDouble()));
                }
            }
            if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverlayShowZoneNumbers))) {
                if (const QJsonValue v = action->params.value(PWR::ActionParam::Value); v.isBool()) {
                    overlay.showZoneNumbers = v.toBool();
                }
            }
            return overlay;
        });
}

ContextTilingParams LayoutRegistry::resolveContextTilingParams(const QString& screenId, int virtualDesktop,
                                                               const QString& activity) const
{
    // Per-slot read (mirrors resolveContextGaps), but NOT cached: this runs on
    // screen / layout changes via the daemon's updateEngineScreens, not the hot
    // per-cursor path. Being uncached lets us stamp the active layout AND the
    // screen orientation onto the query without folding either into a cache key
    // (no cached entry to go stale). Safe from recursion: rulesVisibleActiveLayoutId
    // routes through resolveAssignmentEntry, which never calls this resolver.
    // Mode IS stamped (same rationale as resolveContextScrollingParams): the
    // resolver only runs for autotile screens, and a user rule pinning
    // `Mode Equals "tiling"` alongside a tiling-param action would silently
    // never fire against an unstamped query.
    PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity, QStringLiteral("tiling"));
    stampScreenOrientation(query, screenId);
    stampColorScheme(query);
    query.activeLayout = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    // Filtered resolve, but with NO managed catch-all exclusion (unlike
    // resolveContextGaps'): the baseline rule carries only gap/default slots,
    // never tiling params, so there is no catch-all to exclude here. The
    // predicate below is the field-polarity guard only.
    const PWR::ResolvedActions resolved = m_evaluator->resolveFiltered(query, [](const PWR::Rule& r) {
        // TiledWindowCount is not stamped here either, and an absent field
        // makes a leaf false, so a negated leaf on it would match every
        // context. Mode IS stamped above, so it stays admitted. Window-sourced
        // fields carry the negation-scoped guard (positive leaves stay inert
        // by design; a `none{}` leaf inverts on absence).
        return !r.match.referencesAnyField({PWR::Field::TiledWindowCount})
            && !r.match.negatesAnyField(PWR::windowSourcedFields());
    });

    ContextTilingParams params;
    // No defense-in-depth clamps here, unlike the scrolling resolver's
    // width bound: the tile engine re-clamps every one of these on
    // consumption (maxWindows/masterCount floors, split-ratio bounds), so a
    // second clamp would only duplicate its policy. The scrolling width is
    // clamped at THIS layer because the strip consumes it raw.
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::MaxWindows))) {
        params.maxWindows = action->params.value(PWR::ActionParam::Value).toInt();
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::SplitRatio))) {
        params.splitRatio = action->params.value(PWR::ActionParam::Value).toDouble();
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::MasterCount))) {
        params.masterCount = action->params.value(PWR::ActionParam::Value).toInt();
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::InsertPosition))) {
        // Wire token → AutotileInsertPosition int (End 0 / AfterFocused 1 / AsMaster 2),
        // the same value the per-screen config store holds.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::InsertPositionToken::End) {
            params.insertPosition = 0;
        } else if (token == PWR::InsertPositionToken::AfterFocused) {
            params.insertPosition = 1;
        } else if (token == PWR::InsertPositionToken::AsMaster) {
            params.insertPosition = 2;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverflowBehavior))) {
        // Wire token → AutotileOverflowBehavior int (Float 0 / Unlimited 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::OverflowBehaviorToken::Float) {
            params.overflowBehavior = 0;
        } else if (token == PWR::OverflowBehaviorToken::Unlimited) {
            params.overflowBehavior = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::DragBehavior))) {
        // Wire token → AutotileDragBehavior int (Float 0 / Reorder 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::DragBehaviorToken::Float) {
            params.dragBehavior = 0;
        } else if (token == PWR::DragBehaviorToken::Reorder) {
            params.dragBehavior = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::AlgorithmParams))) {
        // Target algorithm token + free-form custom-param blob (mirrors the
        // overlay shader-uniform override). The daemon applies the params only
        // when the target matches the screen's effective algorithm.
        params.algorithmParamTarget = action->params.value(PWR::ActionParam::Algorithm).toString();
        params.algorithmParams = action->params.value(PWR::ActionParam::Params).toObject().toVariantMap();
    }
    return params;
}

ContextScrollingParams LayoutRegistry::resolveContextScrollingParams(const QString& screenId, int virtualDesktop,
                                                                     const QString& activity) const
{
    // Per-slot read, uncached for the same reasons resolveContextTilingParams is:
    // it runs on screen / layout changes rather than the hot per-cursor path, which
    // lets the query carry the active layout and the screen orientation without
    // folding either into a cache key.
    // Field::Mode IS stamped: this resolver only runs for screens the
    // cascade already put in Scrolling mode, so the stamp costs nothing —
    // but a user rule that pins `Mode Equals "scrolling"` alongside a
    // scroll-param action (a redundant-but-legal spelling) would silently
    // never fire against an unstamped query.
    PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity, QStringLiteral("scrolling"));
    stampScreenOrientation(query, screenId);
    stampColorScheme(query);
    query.activeLayout = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    // Filtered resolve with no managed catch-all exclusion: same baseline-slot
    // rationale as the tiling-param resolver above.
    const PWR::ResolvedActions resolved = m_evaluator->resolveFiltered(query, [](const PWR::Rule& r) {
        // TiledWindowCount is not stamped here, and an absent field makes a
        // leaf false, so a negated leaf on it would match every context. Mode
        // IS stamped above, so it stays admitted. Window-sourced fields carry
        // the negation-scoped guard, same as the tiling-param twin.
        return !r.match.referencesAnyField({PWR::Field::TiledWindowCount})
            && !r.match.negatesAnyField(PWR::windowSourcedFields());
    });

    ContextScrollingParams params;
    // Defense in depth for the two fraction slots (the descriptor validator
    // already rejects out-of-range payloads at load). The policy is REJECT AND
    // FALL THROUGH, not clamp, matching the open path in
    // WindowTrackingAdaptor::scrollOpenRuleParams: a hand-edited rules.json
    // carrying a non-numeric Value would toDouble() to 0.0 and CLAMP UP to the
    // 5% minimum, and a 50.0 would saturate to full width — both of them
    // applying an override the user never wrote. Left unset, the field falls
    // through to the configured default instead. The bounds are the installed
    // PhosphorRules constants, the same pair the descriptor validator checks.
    // Both fractions share that pair: it is named for column WIDTH but bounds
    // the window HEIGHT fraction too (see the Min/MaxColumnWidthRatio doc).
    const auto readFraction = [&resolved](QLatin1StringView slot, std::optional<double>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        const double fraction = v.toDouble();
        if (v.isDouble() && fraction >= PWR::MinColumnWidthRatio && fraction <= PWR::MaxColumnWidthRatio) {
            out = fraction;
        }
    };
    readFraction(PWR::ActionSlot::ScrollDefaultColumnWidth, params.defaultColumnWidth);
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::CenterFocusedColumn))) {
        // Wire token → the centering int (never 0 / always 1 / on overflow 2), the
        // same value the config store holds.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::CenterFocusedColumnToken::Never) {
            params.centerFocusedColumn = 0;
        } else if (token == PWR::CenterFocusedColumnToken::Always) {
            params.centerFocusedColumn = 1;
        } else if (token == PWR::CenterFocusedColumnToken::OnOverflow) {
            params.centerFocusedColumn = 2;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::ScrollDefaultColumnDisplay))) {
        // Wire token → the column display int (normal 0 / tabbed 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::ColumnDisplayToken::Normal) {
            params.defaultColumnDisplay = 0;
        } else if (token == PWR::ColumnDisplayToken::Tabbed) {
            params.defaultColumnDisplay = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::ScrollInsertPosition))) {
        // Wire token → the ScrollInsertPosition int the engine consumes.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::ScrollInsertPositionToken::RightOfActive) {
            params.insertPosition = 0;
        } else if (token == PWR::ScrollInsertPositionToken::LeftOfActive) {
            params.insertPosition = 1;
        } else if (token == PWR::ScrollInsertPositionToken::First) {
            params.insertPosition = 2;
        } else if (token == PWR::ScrollInsertPositionToken::Last) {
            params.insertPosition = 3;
        } else if (token == PWR::ScrollInsertPositionToken::IntoActiveColumn) {
            params.insertPosition = 4;
        }
    }
    readFraction(PWR::ActionSlot::ScrollDefaultWindowHeight, params.defaultWindowHeight);

    // ── tab indicator (niri's `tab-indicator` layout block) ──
    // Same REJECT AND FALL THROUGH policy as readFraction above: a hand-edited
    // rules.json carrying the wrong JSON type must leave the field unset so it
    // falls through to the configured value, never coerce to 0 and apply an
    // override the user did not write. The descriptor validators already
    // reject these at load; this is the defence in depth for what bypasses
    // them.
    const auto readBool = [&resolved](QLatin1StringView slot, std::optional<bool>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        if (v.isBool()) {
            out = v.toBool();
        }
    };
    // Bounds are the descriptors' own, so a value the validator accepts is a
    // value this resolver accepts.
    const auto readInt = [&resolved](QLatin1StringView slot, std::optional<int>& out, double lo, double hi) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        const double d = v.toDouble();
        if (v.isDouble() && d >= lo && d <= hi) {
            out = qRound(d);
        }
    };
    const auto readColor = [&resolved](QLatin1StringView slot, std::optional<QString>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        // Hex shapes only, matching the descriptors' hasHexColor exactly.
        // This helper passes the string through verbatim to a QML `color`
        // property, so a store that bypassed the loader's validation must not
        // get its string through here either. Deliberately NOT
        // QColor::isValidColorName, which is WIDER than the descriptor: it
        // also admits SVG keywords, and "transparent" would reach the overlay
        // as a fully invisible indicator while every setting reported it on.
        if (v.isString() && isHexColorString(v.toString())) {
            out = v.toString();
        }
    };

    readBool(PWR::ActionSlot::TabIndicatorEnabled, params.tabIndicatorEnabled);
    readBool(PWR::ActionSlot::TabIndicatorHideWhenSingleTab, params.tabIndicatorHideWhenSingleTab);
    readBool(PWR::ActionSlot::TabIndicatorPlaceWithinColumn, params.tabIndicatorPlaceWithinColumn);
    readInt(PWR::ActionSlot::TabIndicatorGap, params.tabIndicatorGap, PWR::MinTabIndicatorGap, PWR::MaxTabIndicatorGap);
    readInt(PWR::ActionSlot::TabIndicatorWidth, params.tabIndicatorWidth, PWR::MinTabIndicatorWidth,
            PWR::MaxTabIndicatorWidth);
    readInt(PWR::ActionSlot::TabIndicatorGapsBetweenTabs, params.tabIndicatorGapsBetweenTabs, 0,
            PWR::MaxTabIndicatorGap);
    readInt(PWR::ActionSlot::TabIndicatorCornerRadius, params.tabIndicatorCornerRadius,
            PWR::TabIndicatorCornerRadiusPill, PWR::MaxTabIndicatorCornerRadius);
    readColor(PWR::ActionSlot::TabIndicatorActiveColor, params.tabIndicatorActiveColor);
    readColor(PWR::ActionSlot::TabIndicatorInactiveColor, params.tabIndicatorInactiveColor);
    readColor(PWR::ActionSlot::TabIndicatorUrgentColor, params.tabIndicatorUrgentColor);

    // Drop indicator. Same per-property cascade as the tab indicator above, so
    // a theme rule can set the colours while a separate rule turns it off.
    readBool(PWR::ActionSlot::DropIndicatorEnabled, params.dropIndicatorEnabled);
    readColor(PWR::ActionSlot::DropIndicatorColor, params.dropIndicatorColor);
    readColor(PWR::ActionSlot::DropIndicatorBorderColor, params.dropIndicatorBorderColor);
    readInt(PWR::ActionSlot::DropIndicatorBorderWidth, params.dropIndicatorBorderWidth,
            PWR::MinDropIndicatorBorderWidth, PWR::MaxDropIndicatorBorderWidth);
    readInt(PWR::ActionSlot::DropIndicatorBorderRadius, params.dropIndicatorBorderRadius,
            PWR::MinDropIndicatorBorderRadius, PWR::MaxDropIndicatorBorderRadius);
    // Fraction, not an int: read the way TabIndicatorLength is, and bounded to
    // the same [min, max] the descriptor validates so a hand-edited rule
    // cannot smuggle an out-of-range opacity past the authoring UI.
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::DropIndicatorOpacity))) {
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        const double fraction = v.toDouble();
        if (v.isDouble() && fraction >= PWR::MinDropIndicatorOpacity && fraction <= PWR::MaxDropIndicatorOpacity) {
            params.dropIndicatorOpacity = fraction;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::TabIndicatorLength))) {
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        const double fraction = v.toDouble();
        if (v.isDouble() && fraction >= PWR::MinTabIndicatorLengthRatio
            && fraction <= PWR::MaxTabIndicatorLengthRatio) {
            params.tabIndicatorLength = fraction;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::TabIndicatorStyle))) {
        // Wire token → the style int (chips 0 / bar 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::TabIndicatorStyleToken::Chips) {
            params.tabIndicatorStyle = 0;
        } else if (token == PWR::TabIndicatorStyleToken::Bar) {
            params.tabIndicatorStyle = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::TabIndicatorPosition))) {
        // Wire token → TabIndicatorPosition (left 0 / right 1 / top 2 / bottom 3).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::TabIndicatorPositionToken::Left) {
            params.tabIndicatorPosition = 0;
        } else if (token == PWR::TabIndicatorPositionToken::Right) {
            params.tabIndicatorPosition = 1;
        } else if (token == PWR::TabIndicatorPositionToken::Top) {
            params.tabIndicatorPosition = 2;
        } else if (token == PWR::TabIndicatorPositionToken::Bottom) {
            params.tabIndicatorPosition = 3;
        }
    }
    return params;
}

AssignmentEntry LayoutRegistry::exactContextEntry(const QString& screenId, int virtualDesktop,
                                                  const QString& activity) const
{
    // ENABLED-BLIND BY DESIGN, exactly like hasExactContextRule: findExactContextRule
    // never consults `rule.enabled`, so a DISABLED explicit assignment reports its
    // stored entry here. That is the contract the settings UI needs — the Monitors
    // page renders the pin the user authored (and carries its sibling-mode layout
    // fields through a mode toggle) whether or not the rule is currently switched on.
    // The cascade resolvers are the opposite: the evaluator skips disabled rules, so
    // a context whose only rule is disabled falls through to the gated default. Never
    // reach for this function to answer "what is in effect" — it answers "what is
    // stored".
    if (const PhosphorRules::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity)) {
        return entryFromRuleMatchActions(*rule);
    }
    return {};
}

bool LayoutRegistry::hasExactContextRule(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    return findExactContextRule(screenId, virtualDesktop, activity) != nullptr;
}
const PhosphorRules::Rule* LayoutRegistry::findExactContextRule(const QString& screenId, int virtualDesktop,
                                                                const QString& activity) const
{
    // The deterministic v5 derivation lets us look up a stored assignment by
    // its identity tuple — the bridge guarantees identical tuples produce
    // identical ids. We scan the rule list once for the id (the linear walk
    // is unavoidable here because callers need a stable in-set pointer, not
    // the value-copy `ruleById` returns), then guard with
    // hasEngineModeAction + matchIsExactContext so a hand-edited match that
    // no longer satisfies the canonical context shape can never be returned
    // even if its id happens to match the deterministic derivation.
    //
    // The win over the previous implementation is the predicate cost: the
    // old scan called matchIsExactContext on EVERY rule's match expression;
    // this scan compares ids first and only evaluates the context-shape
    // predicate on the unique candidate.
    //
    // O(N) intentional: a hash lookup would need RuleSet to expose a
    // pointer-returning accessor into its in-set storage with a documented
    // dangle-on-setRules contract — too sharp an edge for the win at the
    // rule counts we care about (<= ~1000 rules per profile). Revisit only
    // if rule counts grow well into the thousands.
    // Two-pass scan: prefer the deterministic-id rule (cheap id compare,
    // the canonical shape the bridge produces). If no rule has the
    // canonical id, fall back to a shape-based scan that picks up
    // user-authored assignment rules whose UUIDs were generated by the
    // settings UI (ruletemplates.cpp's `QUuid::createUuid()` path).
    //
    // NEITHER branch gates on `isPureAssignmentRule`. A MIXED rule — one
    // carrying SetOpacity / OverrideAnimation* / Float / Exclude /
    // LockContext alongside the assignment slots — is still the rule that
    // assigns this context, so it is claimed like any other. The gate used
    // to sit on the shape fallback because the rebuild paths emit only the
    // assignment slot actions and would strip the rest; they now carry
    // non-assignment actions across (carryOverNonAssignmentActions), so the
    // rebuild is lossless and refusing to claim buys nothing. It also cost
    // something: an unclaimed mixed rule sent upsertAssignmentRule down the
    // addRule branch, leaving a duplicate at the same cascade band.
    //
    // The deterministic-id branch never returns nullptr either, for the same
    // family of reason: a caller that gets nothing back takes the addRule
    // path, and the rule it builds carries this exact id, which RuleSet
    // rejects as a collision. A malformed id-carrying rule therefore falls
    // THROUGH to the shape scan rather than short-circuiting the lookup.
    const QUuid candidateId = PWR::ContextRuleBridge::assignmentRuleIdFor(screenId, virtualDesktop, activity);
    const PWR::Rule* shapeMatch = nullptr;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        if (rule.id == candidateId) {
            if (hasEngineModeAction(rule) && matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
                return &rule;
            }
            // Deterministic-id rule exists but was hand-edited away from the
            // canonical form (its SetEngineMode removed, or its match shape
            // changed). Do NOT return nullptr: upsertAssignmentRule would then
            // take its addRule branch, makeAssignmentRule would stamp this very
            // id, and RuleSet::addRule rejects a colliding id — so the
            // Monitors-page write silently no-ops and the context becomes
            // unassignable until the user deletes the edited rule.
            //
            // Fall through to the SHAPE test, applied to THIS rule as well as
            // the rest: a rule that lost only its SetEngineMode but kept a
            // layout slot and the exact-context match is still the rule that
            // assigns this context. A bare `continue` here skipped that test
            // for the id rule itself, so the very rule most likely to be the
            // right answer was the one rule excluded from consideration.
            if (shapeMatch == nullptr && hasAnyAssignmentSlotAction(rule)
                && matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
                shapeMatch = &rule;
            }
            continue;
        }
        // Remember the first exact-context assignment rule we see — we only
        // return it if no deterministic-id rule exists in the set.
        if (shapeMatch == nullptr && hasAnyAssignmentSlotAction(rule)
            && matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
            shapeMatch = &rule;
        }
    }
    return shapeMatch;
}

QUuid LayoutRegistry::exactContextRuleId(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity);
    return rule ? rule->id : QUuid();
}

} // namespace PhosphorZones
