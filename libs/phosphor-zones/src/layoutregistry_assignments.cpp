// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.
//
// Phase 3b: per-context assignment is resolved on the unified Rule engine.
// There is one resolution model: a windowless WindowQuery evaluated through
// RuleEvaluator against the RuleStore's rule set, and the winner of each action
// slot is the highest-priority matching rule (ties broken by list order). There
// is no specificity formula and no provider-default tail — priority is the only
// precedence value, and a genuine miss (no rule fills any slot) routes to the
// settings-gated default. Connector-name / virtual-screen fallback is NOT a
// priority band — it is a query-side recursive key rewrite, kept here in
// layoutForScreen() / the shared resolve helper.

#include <PhosphorZones/LayoutRegistry.h>

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/Rule.h>

#include <algorithm>
#include <optional>

namespace PhosphorZones {

namespace PWR = PhosphorRules;

// The rule-shape classification / context helpers (contextRuleName,
// decodeDims, the matchIsExactContext* family, hasEngineModeAction,
// isContextAssignmentRule, entryFromRuleMatchActions, makeContextQuery)
// are pure functions with no LayoutRegistry-member dependency and live in
// layoutregistry_rulehelpers.cpp / _p.h, keeping the rule-shape vocabulary in
// one place separate from the registry's member-bound resolution logic here.
using namespace RuleHelpers;

namespace {

/// Run @p tryOne against the query-side screen-id fallback chain — the original
/// id, then its connector-name → stable-id rewrite, then a virtual screen's
/// physical id — and return the first engaged optional, or an empty optional if
/// every variant misses. @p tryOne must return a @c std::optional; an engaged
/// optional holding a "settled" value (e.g. a non-null-but-empty Layout*) stops
/// the chain, exactly as the inline retries did. Centralizes the rewrite shared
/// by layoutForScreen / assignmentIdForScreen / assignmentEntryForScreen /
/// hasMatchingAssignmentRule so the four cannot drift.
template<typename TryFn>
auto resolveWithScreenFallback(const QString& screenId, TryFn&& tryOne) -> decltype(tryOne(screenId))
{
    if (auto result = tryOne(screenId)) {
        return result;
    }
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (auto result = tryOne(resolved)) {
                return result;
            }
        }
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        if (auto result = tryOne(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId))) {
            return result;
        }
    }
    return {};
}

} // namespace

void LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const AssignmentEntry& entry)
{
    // Pass the entry's mode through `modeToWireString` so a future Mode
    // (e.g. Scrolling) round-trips without collapsing to Snapping/Autotile.
    // The earlier `bool autotile` shape silently produced "snapping" for any
    // mode that wasn't Autotile, which would corrupt a Scrolling assignment
    // on save and round-trip back as Snapping on load.
    const QString modeToken = modeToWireString(entry.mode);

    const PWR::Rule* existing = findExactContextRule(screenId, virtualDesktop, activity);
    // Priority is the only precedence value (highest wins per slot). On UPDATE
    // preserve the rule's stored priority; on CREATE seed a winning top value so
    // a freshly authored assignment outranks any prior one.
    const int priority =
        existing != nullptr ? existing->priority : nextAssignmentPriority(m_ruleStore->ruleSet().rules());
    // Pass an empty rule name — the settings UI renders an auto-friendly
    // title from the rule's match (with lookup-resolved screen/activity
    // labels). Stamping a raw `screenId · Desktop N · Activity` here would
    // bake connector strings and activity UUIDs into the stored rule.
    PWR::Rule rule =
        PWR::ContextRuleBridge::makeAssignmentRule(QString(), screenId, virtualDesktop, activity, modeToken,
                                                   entry.snappingLayout, entry.tilingAlgorithm, priority);

    if (existing == nullptr) {
        m_ruleStore->addRule(rule);
    } else {
        rule.id = existing->id; // preserve the rule's identity across the update
        // makeAssignmentRule always stamps enabled = true; an upsert must not
        // silently re-enable a rule the user disabled. A disabled context
        // assignment is still an explicit assignment — preserve the flag.
        rule.enabled = existing->enabled;
        m_ruleStore->updateRule(rule);
    }
}

bool LayoutRegistry::removeAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity)
{
    const QUuid existing = exactContextRuleId(screenId, virtualDesktop, activity);
    if (existing.isNull()) {
        return false;
    }
    return m_ruleStore->removeRule(existing);
}

bool LayoutRegistry::purgeSnappingLayoutFromAssignments(const QString& layoutId)
{
    // A snap layout was deleted. Every rule whose SetSnappingLayout action
    // carries this id must lose that reference — but NOT the whole rule, and
    // NOT its other actions.
    //
    // Two rule shapes can carry a SetSnappingLayout action for the deleted id:
    //
    //  1. A pure context-assignment rule (per-screen / -desktop / -activity).
    //     An Autotile-mode context rule can still carry a stale
    //     SetSnappingLayout (mode-toggle losslessness), so blanket-deleting it
    //     would drop its SetEngineMode + SetTilingAlgorithm autotile intent.
    //     Rebuild via ContextRuleBridge::makeAssignmentActions with the
    //     snapping layout cleared but mode + tilingAlgorithm preserved; only
    //     drop the whole rule when, after the clear, nothing but a default
    //     (Snapping) engine-mode remains — a pure Snapping mode-only context
    //     rule encodes no intent beyond the default.
    //
    //  2. A window-property rule that legitimately carries a SetSnappingLayout
    //     action (the phased rollout introduces exactly such rules). Rebuilding
    //     it through makeAssignmentActions would force-inject a SetEngineMode
    //     action and drop every other action it carries. Instead, surgically
    //     remove ONLY the matching SetSnappingLayout action and leave all
    //     other actions intact; drop the rule only if nothing meaningful
    //     remains after the removal.
    QList<PWR::Rule> kept;
    // De-duplicate (screenId, virtualDesktop) — two distinct rules pinned
    // to the same screen/desktop with different activities would otherwise
    // produce duplicate layoutAssigned emissions at the post-update loop
    // below, causing 2-3× redundant refresh work in observers (overlays,
    // settings tiles, autotile state). Note: the `layoutAssigned` signal
    // signature has no activity field, so activity-only rules on the
    // same screen (virtualDesktop=0) collapse to one `(screen, 0)` key —
    // observers that key on activity rely on the rule-store's broader
    // `rulesChanged` signal for their refresh instead of layoutAssigned.
    QSet<QPair<QString, int>> affected;
    bool changed = false;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        const bool referencesDeleted =
            std::any_of(rule.actions.cbegin(), rule.actions.cend(), [&layoutId](const PWR::RuleAction& action) {
                return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                    && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId;
            });
        if (!referencesDeleted) {
            kept.append(rule);
            continue;
        }
        changed = true;

        // Gate on isPureAssignmentRule (not isContextAssignmentRule) — the
        // Shape-1 rebuild path emits ONLY the three assignment slot actions
        // via makeAssignmentActions, so a mixed context rule carrying
        // SetOpacity / OverrideAnimation* / Float / Exclude / LockContext /
        // DefaultLayoutAssignment alongside its assignment actions would silently lose those
        // non-assignment actions on rebuild. Mixed rules fall through to Shape 2's
        // surgical SetSnappingLayout removal, which preserves every
        // other action verbatim.
        if (isContextAssignmentRule(rule) && isPureAssignmentRule(rule)) {
            // Shape 1: rebuild the lossless context-action set with the dead
            // snapping reference cleared; mode + tilingAlgorithm survive.
            const AssignmentEntry entry = entryFromRuleMatchActions(rule);
            const ContextDims dims = decodeDims(rule.match);
            // Track the affected (screen, desktop) for the post-update
            // layoutAssigned emit — every observer keyed on this rule's
            // context needs to refresh, whether the rule was dropped or just
            // rebuilt.
            affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
            if (entry.mode == AssignmentEntry::Snapping && entry.tilingAlgorithm.isEmpty()) {
                // Nothing meaningful remains — a bare Snapping engine-mode is
                // the default. Drop the whole rule.
                qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped context rule" << rule.id.toString()
                                    << "— only a default Snapping mode remained after clearing the deleted layout";
                continue;
            }
            PWR::Rule rebuilt = rule;
            rebuilt.actions = PWR::ContextRuleBridge::makeAssignmentActions(modeToWireString(entry.mode), QString(),
                                                                            entry.tilingAlgorithm);
            kept.append(rebuilt);
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: rebuilt context rule" << rule.id.toString()
                                << "— cleared deleted snapping layout, preserved mode/tilingAlgorithm";
            continue;
        }

        // Shape 2: a window-property (or otherwise non-context) rule. Remove
        // only the SetSnappingLayout actions referencing the deleted layout;
        // every other action is preserved verbatim.
        PWR::Rule trimmed = rule;
        trimmed.actions.erase(std::remove_if(trimmed.actions.begin(), trimmed.actions.end(),
                                             [&layoutId](const PWR::RuleAction& action) {
                                                 return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                                                     && action.params.value(PWR::ActionParam::LayoutId).toString()
                                                     == layoutId;
                                             }),
                              trimmed.actions.end());
        if (trimmed.actions.isEmpty()) {
            // The rule's only action was the dead snapping reference — nothing
            // meaningful remains, so drop it.
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped rule" << rule.id.toString()
                                << "— its only action referenced the deleted snapping layout";
            continue;
        }
        kept.append(trimmed);
        qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: trimmed rule" << rule.id.toString()
                            << "— removed the SetSnappingLayout action for the deleted layout, kept all others";
    }
    if (changed) {
        m_ruleStore->setAllRules(kept);
        // Notify per-screen observers (overlays, autotile state, settings
        // tile caption, etc.) so they refresh against the new cascade.
        // Mirrors `clearAutotileAssignments`'s emit pattern — without it,
        // a layout delete left those consumers showing stale assignments.
        for (const auto& [sid, desk] : std::as_const(affected)) {
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
    }
    return changed;
}

// ── Mutators ────────────────────────────────────────────────────────────────

void LayoutRegistry::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity,
                                  PhosphorZones::Layout* layout)
{
    if (layout) {
        // Preserve an existing tilingAlgorithm — only mode + snappingLayout
        // change (the mode-toggle losslessness invariant). One exact-shape
        // lookup reads the stored rule's actions directly: a wider cascade
        // entry must NOT bleed its tilingAlgorithm into this narrower rule,
        // so only the rule pinning exactly this tuple seeds the entry.
        AssignmentEntry entry;
        if (const PWR::Rule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
            entry = entryFromRuleMatchActions(*existing);
        }
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layout->id().toString();
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        qCDebug(lcZonesLib) << "assignLayout: screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity)
                            << "layout=" << layout->name();
    } else {
        // Clearing: remove the exact-shape rule entirely. Skip the signal
        // when there was nothing to remove.
        if (!removeAssignmentRule(screenId, virtualDesktop, activity)) {
            return;
        }
        qCDebug(lcZonesLib) << "assignLayout: removed screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity);
    }

    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
}

void LayoutRegistry::assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                      const QString& layoutId)
{
    if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        // Store autotile assignment — set mode to Autotile, preserve the
        // existing snappingLayout. One exact-shape lookup (see assignLayout)
        // — only the rule pinning exactly this tuple seeds the entry.
        AssignmentEntry entry;
        if (const PWR::Rule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
            entry = entryFromRuleMatchActions(*existing);
        }
        entry.mode = AssignmentEntry::Autotile;
        entry.tilingAlgorithm = PhosphorLayout::LayoutId::extractAlgorithmId(layoutId);
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    } else {
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

void LayoutRegistry::setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                              const AssignmentEntry& entry)
{
    // Store the entry unconditionally — mode-only entries (empty snapping +
    // empty tiling) are valid when explicitly set by the KCM to preserve
    // mode at a context level.
    upsertAssignmentRule(screenId, virtualDesktop, activity, entry);

    qCDebug(lcZonesLib) << "setAssignmentEntryDirect: screen=" << screenId << "desktop=" << virtualDesktop
                        << "activity=" << activity << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm;

    PhosphorZones::Layout* layout = nullptr;
    if (entry.mode == AssignmentEntry::Snapping && !entry.snappingLayout.isEmpty()) {
        layout = layoutById(QUuid::fromString(entry.snappingLayout));
    }
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
}

// ── Queries ─────────────────────────────────────────────────────────────────

PhosphorZones::Layout* LayoutRegistry::layoutForScreen(const QString& screenId, int virtualDesktop,
                                                       const QString& activity) const
{
    // Connector-name / virtual-screen fallback: a query-side recursive key
    // rewrite, not a priority band. The retry is gated on "no cascade entry
    // at all", NOT "no snap Layout*" — the legacy walkCascade terminated at
    // the first matching entry (including a narrower Autotile entry) and only
    // then fell through to the global default. Treating an Autotile entry as
    // a miss would let a connector/VS retry surface a different (snapping)
    // assignment the legacy walk never reached. So tryResolve returns:
    //   - nullopt   -> no cascade entry; the caller may retry.
    //   - {nullptr} -> an entry exists but yields no snap Layout* (Autotile,
    //                  or a snap entry with empty/unknown layout id) — the
    //                  cascade is settled, fall through to defaultLayout().
    //   - {layout}  -> resolved snap layout.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<PhosphorZones::Layout*> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt; // genuine miss — the caller may retry
        }
        // An entry exists; the cascade is settled — never retry. An Autotile
        // entry (or a snap entry with an empty layout id) has no snap Layout*.
        if (entry->mode == AssignmentEntry::Autotile || entry->snappingLayout.isEmpty()) {
            return std::optional<PhosphorZones::Layout*>(nullptr);
        }
        return std::optional<PhosphorZones::Layout*>(layoutById(QUuid::fromString(entry->snappingLayout)));
    };

    // Connector-name then virtual-screen fallback (the physical screen's
    // assignment is inherited). A settled {nullptr} stops the chain; a genuine
    // miss (nullopt) and a {nullptr} both defer to the registry-wide default.
    // layoutForScreen returns a snap Layout* and has no autotile counterpart;
    // autotile-mode resolution is the autotile engine's job.
    const auto result = resolveWithScreenFallback(screenId, tryResolve);
    return (result && *result) ? *result : defaultLayout();
}

void LayoutRegistry::clearAssignment(const QString& screenId, int virtualDesktop, const QString& activity)
{
    assignLayout(screenId, virtualDesktop, activity, nullptr);
}

bool LayoutRegistry::hasExplicitAssignment(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Exact-shape store lookup — NEVER a resolve() (which always returns the
    // catch-all). True iff a rule whose match is exactly this tuple's shape
    // exists in the rule set.
    //
    // A DISABLED exact-context rule still counts as an explicit assignment:
    // "explicit" means the user stored an entry for this tuple, not that the
    // entry is currently active. resolveAssignmentEntry() (via the evaluator)
    // skips disabled rules, so the two introspection APIs intentionally
    // diverge — hasExplicitAssignment reports stored intent, the resolvers
    // report the effective cascade result. The KCM relies on this so a
    // disabled assignment is not lost from the UI.
    return hasExactContextRule(screenId, virtualDesktop, activity);
}

QString LayoutRegistry::assignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                              const QString& activity) const
{
    // Shared cascade with layoutForScreen, but accepts any entry whose
    // activeLayoutId() is non-empty (incl. Autotile entries). Connector /
    // virtual-screen fallback applies here too.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<QString> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        const QString id = entry->activeLayoutId();
        return id.isEmpty() ? std::nullopt : std::optional<QString>(id);
    };

    if (const auto result = resolveWithScreenFallback(screenId, tryResolve)) {
        return *result;
    }

    // No stored entry in the cascade — fall through to the level-1 global
    // default via the injected providers, honoring the global suppress setting
    // and any per-context DefaultLayoutAssignment override.
    const AssignmentEntry def = resolveDefaultAssignmentEntryForContext(screenId, virtualDesktop, activity);
    return def.activeLayoutId();
}

AssignmentEntry LayoutRegistry::assignmentEntryForScreen(const QString& screenId, int virtualDesktop,
                                                         const QString& activity) const
{
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<AssignmentEntry> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        if (entry->activeLayoutId().isEmpty()) {
            return std::nullopt;
        }
        return entry;
    };

    if (const auto result = resolveWithScreenFallback(screenId, tryResolve)) {
        return *result;
    }

    // Cascade miss — synthesize from the level-1 global default, honoring the
    // global suppress setting and any per-context DefaultLayoutAssignment override.
    return resolveDefaultAssignmentEntryForContext(screenId, virtualDesktop, activity);
}

AssignmentEntry::Mode LayoutRegistry::modeForScreen(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).mode;
}

QString LayoutRegistry::snappingLayoutForScreen(const QString& screenId, int virtualDesktop,
                                                const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).snappingLayout;
}

QString LayoutRegistry::tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop,
                                                 const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).tilingAlgorithm;
}

bool LayoutRegistry::hasMatchingAssignmentRule(const QString& screenId, int virtualDesktop,
                                               const QString& activity) const
{
    return resolveWithScreenFallback(screenId,
                                     [this, virtualDesktop, &activity](const QString& sid) {
                                         return resolveAssignmentEntry(sid, virtualDesktop, activity);
                                     })
        .has_value();
}

bool LayoutRegistry::isDefaultAssignmentSuppressedForContext(const QString& screenId, int virtualDesktop,
                                                             const QString& activity) const
{
    // The "is the synthesized default suppressed" primitive: a per-context
    // DefaultLayoutAssignment override decides locally, otherwise the global gate.
    if (const auto contextOverride = resolveContextDefaultAssignment(screenId, virtualDesktop, activity)) {
        return !*contextOverride;
    }
    return m_defaultAssignmentSuppressedProvider && m_defaultAssignmentSuppressedProvider();
}

bool LayoutRegistry::isContextActiveLayoutSuppressed(const QString& screenId, int virtualDesktop,
                                                     const QString& activity) const
{
    // An active layout exists (explicit assignment at any cascade level, or a
    // default forced through by an "allow" override) — never suppressed.
    if (!assignmentIdForScreen(screenId, virtualDesktop, activity).isEmpty()) {
        return false;
    }
    // An enabled PINNED engine-mode assignment rule covers this context — a
    // rule that overrides the global suppress setting. The context stays
    // active even when the rule sets only the mode (no layout): the layout
    // falls back to the default exactly as it did before suppression existed,
    // and the overlay / zone selector show because the mode is on.
    if (hasMatchingAssignmentRule(screenId, virtualDesktop, activity)) {
        return false;
    }
    // No active layout and no covering assignment rule. Report suppressed ONLY
    // when the cause is the suppress feature (per-context override or the global
    // gate). Any OTHER empty-assignment state (e.g. snapping enabled with no
    // global default layout id) returns false, so callers keep their existing
    // defaultLayout() fallback there.
    return isDefaultAssignmentSuppressedForContext(screenId, virtualDesktop, activity);
}

} // namespace PhosphorZones
