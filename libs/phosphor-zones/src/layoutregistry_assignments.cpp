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
/// by layoutForScreen / storedAssignmentIdForScreen (which assignmentIdForScreen
/// delegates to) / assignmentEntryForScreen / hasMatchingAssignmentRule so the
/// four callers cannot drift.
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

bool LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
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
    PWR::Rule rule = PWR::ContextRuleBridge::makeAssignmentRule(QString(), screenId, virtualDesktop, activity,
                                                                modeToken, entry.snappingLayout, entry.tilingAlgorithm,
                                                                priority, entry.scrollingTemplateLayout);

    if (existing == nullptr) {
        // A rule may already hold this DETERMINISTIC id without being claimable
        // as an assignment — removeAssignmentRule strips the slot actions and
        // keeps the rule alive for whatever else it carried, and a user can
        // hand-edit one the same way. addRule rejects the colliding id, and
        // reporting that as success made the context permanently unassignable
        // with no diagnostic. Update the existing rule in place instead,
        // merging the new slot actions onto its surviving ones.
        const std::optional<PWR::Rule> byId = m_ruleStore->ruleSet().ruleById(rule.id);
        if (byId.has_value()) {
            PWR::Rule merged = rule;
            merged.name = byId->name;
            merged.managed = byId->managed;
            merged.enabled = byId->enabled;
            // Preserve the stored priority like the exact-rule branch above:
            // a reclaim is an update of an existing rule, and re-seeding it to
            // the fresh top value would silently change its precedence against
            // the other context rules (and defeat the no-op guard below, since
            // Rule::operator== compares priority).
            merged.priority = byId->priority;
            carryOverNonAssignmentActions(merged, *byId);
            if (merged == *byId) {
                return false;
            }
            return m_ruleStore->updateRule(merged);
        }
        return m_ruleStore->addRule(rule);
    }
    rule.id = existing->id; // preserve the rule's identity across the update
    // makeAssignmentRule always stamps enabled = true; an upsert must not
    // silently re-enable a rule the user disabled. A disabled context
    // assignment is still an explicit assignment — preserve the flag.
    rule.enabled = existing->enabled;
    // Preserve the user-facing identity too. makeAssignmentRule is handed an
    // empty name, and the shape fallback in findExactContextRule claims
    // user-authored pure assignment rules — which the rules editor lets the
    // user name — so rebuilding without these blanked the name on the next
    // Monitors-page write. It also broke the guard below: Rule::operator==
    // compares name, so a named rule never compared equal and every
    // identical re-apply still bumped the revision.
    rule.name = existing->name;
    rule.managed = existing->managed;
    // And every action that is not one of the four assignment slots. The
    // deterministic context id means this rebuild lands on the stored rule
    // regardless of any purity gate upstream, so a merge is the only
    // non-destructive rebuild. Runs before the no-op guard so a rule whose
    // extra actions are unchanged still compares equal.
    carryOverNonAssignmentActions(rule, *existing);
    // NO-OP GUARD. RuleSet::updateRule has no equality check of its own, so an
    // identical re-apply (the KCM's "apply all" over unchanged values) still
    // bumped the store's monotonic revision — which drops all five context
    // caches, rewrites rules.json and fans out rulesChanged — and the callers
    // then emitted layoutAssigned for a layout that did not change. Violates
    // "only emit signals when the value actually changes".
    //
    // Scoped deliberately to the WRITE, not to any downstream apply pass:
    // suppressing an apply is what previously broke the scrolling→snapping
    // restore. A genuine change still writes and still emits.
    if (rule == *existing) {
        return false;
    }
    m_ruleStore->updateRule(rule);
    return true;
}

bool LayoutRegistry::removeAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity)
{
    const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity);
    if (rule == nullptr) {
        return false;
    }
    // Symmetric with the rebuild paths' merge. findExactContextRule claims
    // MIXED rules (a context assignment the user also hung a SetOpacity or
    // LockContext on), so a wholesale removeRule here would destroy those
    // extra actions when the user merely CLEARS the context's assignment on
    // the Monitors page. Strip the three assignment slots instead and keep the
    // rule alive for whatever else it carries; only delete it outright when
    // nothing survives. Same shape purgeSnappingLayoutFromAssignments already
    // uses for its Shape-2 rules.
    PWR::Rule stripped;
    carryOverNonAssignmentActions(stripped, *rule);
    if (stripped.actions.isEmpty()) {
        return m_ruleStore->removeRule(rule->id);
    }
    PWR::Rule kept = *rule;
    kept.actions = stripped.actions;
    return m_ruleStore->updateRule(kept);
}

bool LayoutRegistry::purgeSnappingLayoutFromAssignments(const QString& layoutId)
{
    // A manual layout was deleted. Every rule referencing it — as a snapping
    // layout (SetSnappingLayout) or as a scrolling template
    // (SetScrollingTemplate, same UUID value shape) — must lose that
    // reference, but NOT the whole rule, and NOT its other actions.
    //
    // Two rule shapes can carry such an action for the deleted id:
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
                return (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                        || action.type == QLatin1String(PWR::ActionType::SetScrollingTemplate))
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
            // reference(s) cleared; every slot that does not point at the
            // deleted layout survives.
            AssignmentEntry entry = entryFromRuleMatchActions(rule);
            if (entry.snappingLayout == layoutId) {
                entry.snappingLayout.clear();
            }
            if (entry.scrollingTemplateLayout == layoutId) {
                entry.scrollingTemplateLayout.clear();
            }
            const ContextDims dims = decodeDims(rule.match);
            // Track the affected (screen, desktop) for the post-update
            // layoutAssigned emit — every observer keyed on this rule's
            // context needs to refresh, whether the rule was dropped or just
            // rebuilt.
            affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
            if (entry.mode == AssignmentEntry::Snapping && entry.snappingLayout.isEmpty()
                && entry.tilingAlgorithm.isEmpty() && entry.scrollingTemplateLayout.isEmpty()) {
                // Nothing meaningful remains — a bare Snapping engine-mode is
                // the default. Drop the whole rule. The snappingLayout guard
                // is load-bearing: the clear above only empties the field
                // that MATCHED the deleted id, so a rule whose template was
                // deleted can still carry a live snapping assignment, and
                // dropping it would destroy that assignment.
                qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped context rule" << rule.id.toString()
                                    << "— only a default Snapping mode remained after clearing the deleted layout";
                continue;
            }
            PWR::Rule rebuilt = rule;
            rebuilt.actions =
                PWR::ContextRuleBridge::makeAssignmentActions(modeToWireString(entry.mode), entry.snappingLayout,
                                                              entry.tilingAlgorithm, entry.scrollingTemplateLayout);
            kept.append(rebuilt);
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: rebuilt context rule" << rule.id.toString()
                                << "— cleared the deleted layout's references, preserved the other slots";
            continue;
        }

        // Shape 2: a window-property (or otherwise non-context) rule. Remove
        // only the SetSnappingLayout actions referencing the deleted layout;
        // every other action is preserved verbatim.
        //
        // A MIXED context rule (context-only match + assignment actions +
        // some non-assignment action) lands here too — it fails
        // isPureAssignmentRule but still carries a context whose observers
        // must refresh, so record it in `affected` for the layoutAssigned
        // emit below, exactly like the Shape-1 branch. A PURE window-property
        // rule deliberately gets no entry: it has no context dims to key on,
        // and it cannot influence context resolution anyway (the windowless
        // context query never matches its window leaves, and slotMatch drops
        // window-negating rules), so there is no engine state to re-derive —
        // the store write's rulesChanged still reaches every projection
        // consumer.
        if (isContextAssignmentRule(rule)) {
            const ContextDims dims = decodeDims(rule.match);
            affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
        }
        PWR::Rule trimmed = rule;
        trimmed.actions.erase(
            std::remove_if(trimmed.actions.begin(), trimmed.actions.end(),
                           [&layoutId](const PWR::RuleAction& action) {
                               return (action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                                       || action.type == QLatin1String(PWR::ActionType::SetScrollingTemplate))
                                   && action.params.value(PWR::ActionParam::LayoutId).toString() == layoutId;
                           }),
            trimmed.actions.end());
        if (trimmed.actions.isEmpty()) {
            // The rule's only action was the dead layout reference — nothing
            // meaningful remains, so drop it.
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped rule" << rule.id.toString()
                                << "— its only action referenced the deleted layout";
            continue;
        }
        // Mirror Shape 1's bare-default drop: a context rule reduced to a
        // lone default-Snapping SetEngineMode encodes no intent beyond the
        // default and would otherwise survive here forever (template-carrying
        // rules land in this branch whenever a non-assignment action rides
        // along).
        if (isContextAssignmentRule(trimmed) && trimmed.actions.size() == 1
            && entryFromRuleMatchActions(trimmed).mode == AssignmentEntry::Snapping) {
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped context rule" << rule.id.toString()
                                << "— only a default Snapping mode remained after trimming the deleted layout";
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
        // The return value is deliberately IGNORED. upsertAssignmentRule
        // suppresses a redundant WRITE (no revision bump, no rules.json
        // rewrite, no cache drop), which is the part that was violating
        // emit-on-change. layoutAssigned is not a value-changed signal
        // though: it is the sole trigger for updateEngineScreens() and
        // updateLayoutFilter(), so an idempotent re-apply that a caller
        // issues precisely to force a re-derive must still fan out. This is
        // the shape that previously broke the scrolling→snapping restore
        // when a "duplicate" pass was suppressed.
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
    // The tiling-family sentinels ("autotile:…" and the bare "scrolling:")
    // carry no Layout* to resolve, so both route through the entry-upsert
    // path. Classification goes through AssignmentEntry::fromLayoutId —
    // the ONE mode cascade — seeded from the exact-shape rule so the
    // sibling mode's stored fields survive (the lossless-toggle contract).
    // A hand-rolled isAutotile-only branch here once made a D-Bus
    // assign("scrolling:") fall into the null-Layout arm of assignLayout,
    // which CLEARS the assignment.
    if (PhosphorLayout::LayoutId::isAutotile(layoutId) || PhosphorLayout::LayoutId::isScrolling(layoutId)) {
        AssignmentEntry existing;
        if (const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity)) {
            existing = entryFromRuleMatchActions(*rule);
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, existing);
        // Emit unconditionally — see assignLayout: the write suppression is
        // real, but layoutAssigned drives the engine-screen re-derive and an
        // idempotent D-Bus assign must still reach it.
        upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
        Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    } else {
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

void LayoutRegistry::assignScrollingTemplate(const QString& screenId, int virtualDesktop, const QString& activity,
                                             const QString& layoutId)
{
    // Seed from the exact-shape rule so the sibling mode's stored fields
    // survive (the lossless-toggle contract), exactly like assignLayoutById's
    // sentinel branch. The mode flips to Scrolling: picking a template from
    // the picker IS choosing scrolling semantics for the context, and a
    // template on a non-scrolling entry would be dead data the cascade never
    // reads.
    AssignmentEntry entry;
    if (const PWR::Rule* rule = findExactContextRule(screenId, virtualDesktop, activity)) {
        entry = entryFromRuleMatchActions(*rule);
    }
    entry.mode = AssignmentEntry::Scrolling;
    // Normalize to the canonical braced form at the library choke point so
    // every caller (adaptor, controller, registry) stores one spelling. A
    // braceless bus-supplied uuid stored verbatim would defeat the purge's
    // exact string compare on layout delete and the upsert no-op guard's
    // byte-wise action compare. Empty stays empty (clears the template).
    const QUuid parsed = QUuid::fromString(layoutId);
    entry.scrollingTemplateLayout = parsed.isNull() ? QString() : parsed.toString();
    upsertAssignmentRule(screenId, virtualDesktop, activity, entry);
    // Emit unconditionally — see assignLayout: the write suppression is real,
    // but layoutAssigned drives the engine-screen re-derive (which pushes the
    // template vocabulary), and an idempotent re-apply must still reach it.
    Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
}

PhosphorZones::Layout* LayoutRegistry::scrollingTemplateForContext(const QString& screenId, int virtualDesktop,
                                                                   const QString& activity) const
{
    // Mode-gated BY DESIGN: a template preserved on a non-Scrolling context
    // (the lossless-toggle contract) must not resolve — the engine push and
    // the picker consume the LIVE template only. The raw field-inspection
    // twin is scrollingTemplateLayoutForScreen, which reads the stored field
    // regardless of mode (parity with snappingLayoutForScreen).
    const auto entry = resolveAssignmentEntry(screenId, virtualDesktop, activity);
    if (!entry || entry->mode != AssignmentEntry::Scrolling || entry->scrollingTemplateLayout.isEmpty()) {
        return nullptr;
    }
    // A deleted or unknown template id degrades to "no template" — the
    // caller falls back to the settings preset lists. layoutById returns
    // nullptr for unknown ids, so no extra validation is needed.
    return layoutById(QUuid::fromString(entry->scrollingTemplateLayout));
}

void LayoutRegistry::setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                              const AssignmentEntry& entry)
{
    // Store the entry — mode-only entries (empty snapping + empty tiling) are
    // valid when explicitly set by the KCM to preserve mode at a context
    // level. An identical re-apply writes nothing, but still emits: this is a
    // D-Bus entry point and layoutAssigned is what re-derives the engine
    // screens, so an idempotent call made to force that re-derive must reach
    // it. See assignLayout for the full reasoning.
    upsertAssignmentRule(screenId, virtualDesktop, activity, entry);

    qCDebug(lcZonesLib) << "setAssignmentEntryDirect: screen=" << screenId << "desktop=" << virtualDesktop
                        << "activity=" << activity << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm << "template=" << entry.scrollingTemplateLayout;

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
    //   - {nullptr} -> an entry exists but yields no snap Layout* (any
    //                  non-Snapping mode, or a snap entry with empty/unknown
    //                  layout id) — the cascade is settled, fall through to
    //                  defaultLayout().
    //   - {layout}  -> resolved snap layout.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<PhosphorZones::Layout*> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt; // genuine miss — the caller may retry
        }
        // An entry exists; the cascade is settled — never retry. Any
        // NON-Snapping mode has no snap Layout*: an Autotile or Scrolling
        // entry may still CARRY a non-empty snappingLayout (the lossless
        // mode-toggle contract preserves the field), and resolving it here
        // would hand ungated consumers (zone detection, window drag) zones
        // from a layout the screen is not using. A snap entry with an empty
        // layout id settles the same way.
        if (entry->mode != AssignmentEntry::Snapping || entry->snappingLayout.isEmpty()) {
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

// Shared predicate for the two cascade visitors below: a resolved entry
// with an EMPTY activeLayoutId() is still an EXPLICIT Snapping mode pin
// when the user stored an ENABLED exact-context rule for this tuple that
// actually carries a SetEngineMode action — the Monitors page stages one
// when leaving Scrolling while the context suppresses the default layout,
// so there is no layout to pin. Requiring the mode action keeps a
// layout-only exact rule (which pins no mode) from anchoring an entry
// whose mode came from elsewhere. Both visitors MUST honour this the same
// way, or modeForScreen and assignmentIdForScreen disagree about the same
// context (mode Snapping while the id path synthesizes the default tier's
// autotile id). (Autotile and Scrolling mode-only entries stay visible
// through their bare id sentinels; only Snapping has no sentinel because
// its ids are layout UUIDs.)
bool LayoutRegistry::hasExplicitSnappingModePin(const QString& sid, int virtualDesktop, const QString& activity,
                                                const AssignmentEntry& entry) const
{
    if (entry.mode != AssignmentEntry::Snapping) {
        return false;
    }
    const PWR::Rule* exact = findExactContextRule(sid, virtualDesktop, activity);
    // `enabled` is belt-and-braces: the evaluator already skips disabled
    // rules, so a resolution that reaches this predicate normally came from
    // an enabled rule — but findExactContextRule deliberately IGNORES the
    // flag (upsert semantics), so the conjunct keeps the pin from anchoring
    // on a disabled exact rule should the two ever pair up via a broader
    // enabled rule resolving the same tuple.
    return exact != nullptr && exact->enabled && hasEngineModeAction(*exact);
}

QString LayoutRegistry::assignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                              const QString& activity) const
{
    // The stored cascade walk lives in resolveStoredAssignmentId — this
    // method is that walk plus the level-1 default tail, and delegating keeps
    // the two from ever resolving the cascade differently. The optional's
    // ENGAGED-but-empty state is load-bearing: an explicit mode-only Snapping
    // pin settles the chain with no layout identity, and treating that as a
    // miss would synthesize the default tier's autotile id here.
    if (const auto stored = resolveStoredAssignmentId(screenId, virtualDesktop, activity)) {
        return *stored;
    }

    // No stored entry in the cascade — fall through to the level-1 global
    // default via the injected providers, honoring the global suppress setting
    // and any per-context DefaultLayoutAssignment override.
    const AssignmentEntry def = resolveDefaultAssignmentEntryForContext(screenId, virtualDesktop, activity);
    return def.activeLayoutId();
}

QString LayoutRegistry::storedAssignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const
{
    return resolveStoredAssignmentId(screenId, virtualDesktop, activity).value_or(QString());
}

std::optional<QString> LayoutRegistry::resolveStoredAssignmentId(const QString& screenId, int virtualDesktop,
                                                                 const QString& activity) const
{
    // Shared cascade with layoutForScreen, accepting any entry whose
    // activeLayoutId() is non-empty (incl. Autotile entries), but a miss
    // stays a miss: no level-1 default synthesis. Distinguishes "this
    // context has its own assignment (exact or rule-based)" from "the
    // resolver would hand out the registry-wide default". Connector /
    // virtual-screen fallback applies here too.
    auto tryResolve = [this, virtualDesktop, &activity](const QString& sid) -> std::optional<QString> {
        const auto entry = resolveAssignmentEntry(sid, virtualDesktop, activity);
        if (!entry) {
            return std::nullopt;
        }
        const QString id = entry->activeLayoutId();
        if (id.isEmpty()) {
            // An explicit mode-only Snapping pin SETTLES the chain with an
            // empty id (there is genuinely no layout identity) instead of
            // deferring to a sibling screen or the default tier — keeping
            // this API consistent with assignmentEntryForScreen's mode.
            if (hasExplicitSnappingModePin(sid, virtualDesktop, activity, *entry)) {
                return std::optional<QString>(QString());
            }
            return std::nullopt;
        }
        return std::optional<QString>(id);
    };

    // Disengaged, NOT an empty string: an engaged-empty result means "the
    // cascade settled on a mode-only Snapping pin", which assignmentIdForScreen
    // reads as "do not synthesize the default tier".
    return resolveWithScreenFallback(screenId, tryResolve);
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
            // See hasExplicitSnappingModePin above: an explicit mode-only
            // Snapping pin is accepted; anything else defers to the
            // connector/VS fallback and then the default tier.
            if (hasExplicitSnappingModePin(sid, virtualDesktop, activity, *entry)) {
                return entry;
            }
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

QString LayoutRegistry::scrollingTemplateLayoutForScreen(const QString& screenId, int virtualDesktop,
                                                         const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).scrollingTemplateLayout;
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
    // An enabled assignment-family rule covers this context (mode, layout,
    // OR algorithm slot — any authored intent overrides the global suppress
    // setting). The context stays active even when the rule sets only one
    // slot: the rest falls back to the default exactly as it did before
    // suppression existed, and the overlay / zone selector show.
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
