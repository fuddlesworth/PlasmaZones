// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.
//
// Phase 3b: the per-context assignment cascade is reimplemented on the
// unified WindowRule engine. There is one resolution model: a windowless
// WindowQuery evaluated through RuleEvaluator against the WindowRuleStore's
// rule set. The deterministic Assignment cascade (exact → activity → desktop
// → screen → provider default) is reproduced by the ContextRuleBridge
// priority formula — higher priority pins more context dimensions, so the
// evaluator's descending-priority, first-action-per-slot walk returns the
// same result the old walkCascade() did. Connector-name / virtual-screen
// fallback is NOT a priority band — it is a query-side recursive key rewrite,
// kept here in layoutForScreen() / the shared resolve helper.

#include <PhosphorZones/LayoutRegistry.h>

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowQuery.h>
#include <PhosphorWindowRule/WindowRule.h>

#include <algorithm>
#include <optional>

namespace PhosphorZones {

namespace PWR = PhosphorWindowRule;

// The rule-shape classification / context helpers (contextRuleName,
// decodeDims, the matchIsExactContext* family, hasEngineModeAction,
// isContextAssignmentRule, entryFromRuleMatchActions, makeContextQuery)
// are pure functions with no LayoutRegistry-member dependency and live in
// layoutregistry_rulehelpers.cpp / _p.h — pulled in here so this TU stays
// under the project's 800-line ceiling.
using namespace RuleHelpers;

// ── Rule-backed cascade resolution ──────────────────────────────────────────

std::optional<AssignmentEntry> LayoutRegistry::resolveAssignmentEntry(const QString& screenId, int virtualDesktop,
                                                                      const QString& activity) const
{
    // The cascade is reproduced by the rule set's descending-priority order.
    // The legacy walkCascade treated the provider default as a MISS
    // (resolveDefaultAssignmentEntry handled it via injected lambdas), so the
    // catch-all rule is excluded here — only PINNED context rules count as a
    // cascade hit.
    //
    // The descending-priority, tie-break-by-list-order walk is the shared
    // RuleEvaluator::highestPriorityMatch — the one place that walk lives, so
    // this resolver can never drift from the evaluator's own ordering. The
    // candidate filter keeps only pinned context-assignment rules.
    // SetSnappingLayout and SetTilingAlgorithm share the `layout` action slot,
    // so the winning rule's full action list is read, preserving the
    // mode-toggle losslessness invariant (an entry keeps BOTH snappingLayout
    // and tilingAlgorithm regardless of active mode).
    const PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity);
    const PWR::WindowRule* winner = m_evaluator->highestPriorityMatch(query, [](const PWR::WindowRule& rule) {
        return hasEngineModeAction(rule) && !rule.match.isCatchAll();
    });
    if (winner == nullptr) {
        return std::nullopt;
    }
    return entryFromRuleMatchActions(*winner);
}

bool LayoutRegistry::hasExactContextRule(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    return findExactContextRule(screenId, virtualDesktop, activity) != nullptr;
}
const PhosphorWindowRule::WindowRule* LayoutRegistry::findExactContextRule(const QString& screenId, int virtualDesktop,
                                                                           const QString& activity) const
{
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (!hasEngineModeAction(rule)) {
            continue;
        }
        if (matchIsExactContext(rule.match, screenId, virtualDesktop, activity)) {
            return &rule;
        }
    }
    return nullptr;
}

QUuid LayoutRegistry::exactContextRuleId(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    const PWR::WindowRule* rule = findExactContextRule(screenId, virtualDesktop, activity);
    return rule ? rule->id : QUuid();
}

void LayoutRegistry::upsertAssignmentRule(const QString& screenId, int virtualDesktop, const QString& activity,
                                          const AssignmentEntry& entry)
{
    const bool autotile = (entry.mode == AssignmentEntry::Autotile);
    PWR::WindowRule rule = PWR::ContextRuleBridge::makeAssignmentRule(
        contextRuleName(screenId, virtualDesktop, activity), screenId, virtualDesktop, activity, autotile,
        entry.snappingLayout, entry.tilingAlgorithm);

    const PWR::WindowRule* existing = findExactContextRule(screenId, virtualDesktop, activity);
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
    QList<PWR::WindowRule> kept;
    bool changed = false;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        const bool referencesDeleted =
            std::any_of(rule.actions.cbegin(), rule.actions.cend(), [&layoutId](const PWR::RuleAction& action) {
                return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                    && action.params.value(QLatin1String("layoutId")).toString() == layoutId;
            });
        if (!referencesDeleted) {
            kept.append(rule);
            continue;
        }
        changed = true;

        if (isContextAssignmentRule(rule)) {
            // Shape 1: rebuild the lossless context-action set with the dead
            // snapping reference cleared; mode + tilingAlgorithm survive.
            const AssignmentEntry entry = entryFromRuleMatchActions(rule);
            const bool autotile = (entry.mode == AssignmentEntry::Autotile);
            if (!autotile && entry.tilingAlgorithm.isEmpty()) {
                // Nothing meaningful remains — a bare Snapping engine-mode is
                // the default. Drop the whole rule.
                qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: dropped context rule" << rule.id.toString()
                                    << "— only a default Snapping mode remained after clearing the deleted layout";
                continue;
            }
            PWR::WindowRule rebuilt = rule;
            rebuilt.actions = PWR::ContextRuleBridge::makeAssignmentActions(autotile, QString(), entry.tilingAlgorithm);
            kept.append(rebuilt);
            qCDebug(lcZonesLib) << "purgeSnappingLayoutFromAssignments: rebuilt context rule" << rule.id.toString()
                                << "— cleared deleted snapping layout, preserved mode/tilingAlgorithm";
            continue;
        }

        // Shape 2: a window-property (or otherwise non-context) rule. Remove
        // only the SetSnappingLayout actions referencing the deleted layout;
        // every other action is preserved verbatim.
        PWR::WindowRule trimmed = rule;
        trimmed.actions.erase(std::remove_if(trimmed.actions.begin(), trimmed.actions.end(),
                                             [&layoutId](const PWR::RuleAction& action) {
                                                 return action.type == QLatin1String(PWR::ActionType::SetSnappingLayout)
                                                     && action.params.value(QLatin1String("layoutId")).toString()
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
        if (const PWR::WindowRule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
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
        if (const PWR::WindowRule* existing = findExactContextRule(screenId, virtualDesktop, activity)) {
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

// ── Partial-update / promote write helpers ─────────────────────────────────

namespace {
// Decide the AssignmentEntry to start from when a single-field write
// arrives at (screenId, virtualDesktop, activity). The KCM "Assignments"
// pages (per ADR-discussion-497) want the edit to record a preference at
// that exact slot WITHOUT flipping the rendered mode.
//
// If a local entry exists at the exact slot we always use it as the seed
// — even when its `activeLayoutId()` happens to be empty (e.g. an entry
// with {mode=Snapping, snap="", tile="cluster"} that holds a stored
// tile preference). Falling back to the cascade in that case would
// clobber the opposite-field value the user had explicitly recorded at
// this slot.
//
// Only when the slot has no local entry at all do we seed from the
// cascade-resolved ambient state, so the new value lands in the right
// mode and survives the cascade visitor's `activeLayoutId().isEmpty()`
// reject filter.
AssignmentEntry seedForPartialUpdate(const LayoutRegistry& self,
                                     const QHash<LayoutAssignmentKey, AssignmentEntry>& assignments,
                                     const QString& screenId, int virtualDesktop, const QString& activity)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};
    auto it = assignments.constFind(key);
    if (it != assignments.constEnd()) {
        return *it;
    }
    return self.assignmentEntryForScreen(screenId, virtualDesktop, activity);
}
} // namespace

namespace {
// Shared write path for partial-update / promote methods. Applies @p mutator
// to the seed entry (taken from the slot itself or from the cascade-resolved
// ambient state — see seedForPartialUpdate), then persists. When both fields
// end up empty the slot is removed rather than left as a cascade-skipped
// no-op record.
//
// Returns true when the persisted assignments actually changed; callers
// should skip the `layoutAssigned` emit + `saveAssignments` call when
// false (a clear that targeted an already-empty slot).
//
// @p caller is the calling method name; used only for log trace context.
template<typename Mutator>
bool applyPartialOrPromote(LayoutRegistry& self, QHash<LayoutAssignmentKey, AssignmentEntry>& assignments,
                           const QString& screenId, int virtualDesktop, const QString& activity, bool seedFromCascade,
                           const char* caller, Mutator&& mutator)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    // Seed entry: cascade-resolved fallback (preserve-mode path) or
    // local-only (promote path — shadows are cleared separately, so
    // pulling cascade data would defeat the point).
    AssignmentEntry entry;
    if (seedFromCascade) {
        entry = seedForPartialUpdate(self, assignments, screenId, virtualDesktop, activity);
    } else if (auto it = assignments.constFind(key); it != assignments.constEnd()) {
        entry = *it;
    }

    mutator(entry);

    if (entry.snappingLayout.isEmpty() && entry.tilingAlgorithm.isEmpty()) {
        bool hadAssignment = assignments.remove(key);
        if (hadAssignment) {
            qCDebug(lcZonesLib) << caller << ": removed empty entry screen=" << screenId << "desktop=" << virtualDesktop
                                << "activity=" << activity;
        }
        return hadAssignment;
    }
    assignments[key] = entry;
    qCDebug(lcZonesLib) << caller << ": screen=" << screenId << "desktop=" << virtualDesktop << "activity=" << activity
                        << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm;
    return true;
}
} // namespace

void LayoutRegistry::setSnappingLayoutPreservingMode(const QString& screenId, int virtualDesktop,
                                                     const QString& activity, const QString& layoutId)
{
    const bool changed =
        applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/true,
                              "setSnappingLayoutPreservingMode", [&](AssignmentEntry& entry) {
                                  entry.snappingLayout = layoutId;
                              });
    if (!changed) {
        return;
    }
    PhosphorZones::Layout* layout = layoutId.isEmpty() ? nullptr : layoutById(QUuid::fromString(layoutId));
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

void LayoutRegistry::clearShadowsForSlot(const QString& screenId, int virtualDesktop, const QString& activity)
{
    // Resolve the physical screen so VS variants of the same physical
    // also get treated as "same screen" — the cascade walks VS → phys
    // at level 6, so leaving VS entries intact would re-shadow the slot
    // we're about to promote.
    const QString targetPhysId = PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
        : screenId;
    const LayoutAssignmentKey targetKey{screenId, virtualDesktop, activity};

    auto sameScreen = [&](const QString& s) -> bool {
        if (s == screenId)
            return true;
        if (s == targetPhysId)
            return true;
        if (PhosphorIdentity::VirtualScreenId::isVirtual(s)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(s) == targetPhysId)
            return true;
        return false;
    };

    // Determine which keys on the same screen would win at a higher (or
    // equal) cascade level than the target slot for some context the
    // target slot is meant to cover. Reading bottom-up by slot shape:
    //   - Monitor row (vd=0, activity=""): the slot is the universal
    //     fallback for the screen; every other entry on the screen
    //     shadows it for some context → clear them all.
    //   - Per-desktop (vd>0, activity=""): the slot is the fallback
    //     for (this screen, this desktop, *). Two key shapes shadow it
    //     for context (vd, activity=X):
    //       L1 (vd, X)           — same-desktop+activity entries
    //       L2 (0, X)            — per-activity entries (cascade walks
    //                              activity before per-desktop, see
    //                              walkCascade level 2)
    //     Clear BOTH; leaving per-activity entries intact would let any
    //     activity-keyed entry continue to win over the promoted slot
    //     whenever that activity is current.
    //   - Per-activity (vd=0, activity!=""): the slot is the fallback
    //     for (this screen, *, this activity). Per-desktop+activity
    //     entries with the same activity (anyVd>0, activity) shadow
    //     at L1 → clear them.
    //   - Specific (vd>0, activity!=""): no other entry shadows it
    //     in the cascade, so nothing to clear.
    auto shouldClear = [&](const LayoutAssignmentKey& key) -> bool {
        if (key == targetKey)
            return false;
        if (!sameScreen(key.screenId))
            return false;

        if (virtualDesktop == 0 && activity.isEmpty())
            return true; // Monitor row: clear everything else on screen
        if (virtualDesktop > 0 && activity.isEmpty()) {
            // Per-desktop scope: same-desktop entries (L1 shadows) AND
            // per-activity entries (L2 shadows for contexts with matching
            // activity). Leaving the latter would let an activity-keyed
            // entry mask the promoted per-desktop slot when that activity
            // is current.
            return key.virtualDesktop == virtualDesktop || (key.virtualDesktop == 0 && !key.activity.isEmpty());
        }
        if (virtualDesktop == 0 && !activity.isEmpty())
            return key.activity == activity; // Per-activity scope
        return false; // Specific (vd, activity): no shadow to clear
    };

    QList<LayoutAssignmentKey> toRemove;
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        if (shouldClear(it.key()))
            toRemove.append(it.key());
    }
    for (const auto& key : std::as_const(toRemove)) {
        m_assignments.remove(key);
        qCDebug(lcZonesLib) << "clearShadowsForSlot: removed shadow screen=" << key.screenId
                            << "desktop=" << key.virtualDesktop << "activity=" << key.activity;
    }
}

void LayoutRegistry::setSnappingLayoutPromoting(const QString& screenId, int virtualDesktop, const QString& activity,
                                                const QString& layoutId)
{
    // clearShadowsForSlot itself emits no signal — its mutation is rolled
    // into the layoutAssigned emit below. The applyPartialOrPromote
    // return is intentionally discarded (cast to void to make intent
    // explicit): even when the helper reports "unchanged" for the
    // target slot, clearShadows may have removed shadow entries that
    // must still be persisted and observed.
    clearShadowsForSlot(screenId, virtualDesktop, activity);

    (void)applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/false,
                                "setSnappingLayoutPromoting", [&](AssignmentEntry& entry) {
                                    entry.mode = AssignmentEntry::Snapping;
                                    entry.snappingLayout = layoutId;
                                });

    PhosphorZones::Layout* layout = layoutId.isEmpty() ? nullptr : layoutById(QUuid::fromString(layoutId));
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

void LayoutRegistry::setTilingAlgorithmPromoting(const QString& screenId, int virtualDesktop, const QString& activity,
                                                 const QString& algorithmId)
{
    // See note in setSnappingLayoutPromoting about always emitting.
    clearShadowsForSlot(screenId, virtualDesktop, activity);

    (void)applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/false,
                                "setTilingAlgorithmPromoting", [&](AssignmentEntry& entry) {
                                    entry.mode = AssignmentEntry::Autotile;
                                    entry.tilingAlgorithm = algorithmId;
                                });

    Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    saveAssignments();
}

void LayoutRegistry::setTilingAlgorithmPreservingMode(const QString& screenId, int virtualDesktop,
                                                      const QString& activity, const QString& algorithmId)
{
    const bool changed =
        applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/true,
                              "setTilingAlgorithmPreservingMode", [&](AssignmentEntry& entry) {
                                  entry.tilingAlgorithm = algorithmId;
                              });
    if (!changed) {
        return;
    }
    // Emit with nullptr — tiling changes don't have a snap Layout* to
    // pass through; daemon-side recalc paths key off the screenId.
    Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    saveAssignments();
}

// ── Queries ─────────────────────────────────────────────────────────────────
//
// layoutForScreen, assignmentIdForScreen, and assignmentEntryForScreen share the
// same fallback cascade, implemented once in walkCascade() above. Each method
// supplies a visitor that decides whether to accept or cascade past each entry.

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

    if (const auto result = tryResolve(screenId)) {
        return *result ? *result : defaultLayout();
    }
    // Connector-name fallback.
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result ? *result : defaultLayout();
            }
        }
    }
    // Virtual-screen fallback — inherit the physical screen's assignment.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result ? *result : defaultLayout();
        }
    }

    // No explicit assignment in the cascade — defer to the registry-wide
    // default. layoutForScreen returns a snap Layout* and has no autotile
    // counterpart; autotile-mode resolution is the autotile engine's job.
    return defaultLayout();
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

    if (const auto result = tryResolve(screenId)) {
        return *result;
    }
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result;
            }
        }
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result;
        }
    }

    // No stored entry in the cascade — fall through to the level-1 global
    // default via the injected providers.
    const AssignmentEntry def = resolveDefaultAssignmentEntry();
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

    if (const auto result = tryResolve(screenId)) {
        return *result;
    }
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            if (const auto result = tryResolve(resolved)) {
                return *result;
            }
        }
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (const auto result = tryResolve(physId)) {
            return *result;
        }
    }

    // Cascade miss — synthesize from the level-1 global default.
    return resolveDefaultAssignmentEntry();
}

AssignmentEntry::Mode LayoutRegistry::modeForScreen(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).mode;
}

QString LayoutRegistry::snappingLayoutForScreen(const QString& screenId, int virtualDesktop,
                                                const QString& activity) const
{
    // Per-field cascade: accept the first entry whose snap field is set.
    // Cannot route through `assignmentEntryForScreen` because its visitor
    // rejects entries whose `activeLayoutId()` is empty — which happens
    // for preserve-mode entries shaped like {mode=Snapping, snap="",
    // tile="cluster"} after a snap-clear that preserved the tile
    // preference (see setSnappingLayoutPreservingMode). Reading via the
    // mode-resolved cascade would hide the stored tile field from the
    // tile-row in the KCM Assignments page and vice versa.
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<QString> {
                                  if (entry.snappingLayout.isEmpty())
                                      return std::nullopt;
                                  return entry.snappingLayout;
                              });
    if (result) {
        return *result;
    }
    // Cascade miss — consult the snap-side global default provider
    // directly. Cannot route through `resolveDefaultAssignmentEntry`
    // because that helper synthesizes a single mode-resolved entry and
    // returns Autotile-only state with snap="" when the user has
    // autotile preferred (and vice versa for the tile reader). This is
    // a per-field reader: the snap default exists independently of
    // which mode the cascade winner would render.
    return m_defaultLayoutIdProvider ? m_defaultLayoutIdProvider() : QString();
}

QString LayoutRegistry::tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop,
                                                 const QString& activity) const
{
    // Per-field cascade — see snappingLayoutForScreen above.
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<QString> {
                                  if (entry.tilingAlgorithm.isEmpty())
                                      return std::nullopt;
                                  return entry.tilingAlgorithm;
                              });
    if (result) {
        return *result;
    }
    // Symmetric per-field default: consult the autotile-side global
    // default provider directly so the KCM Tiling Assignments default
    // row shows the user's configured default autotile algorithm even
    // when the snapping-preferred provider would steer
    // `resolveDefaultAssignmentEntry` into a snap-mode entry with an
    // empty tile field.
    return m_defaultAutotileAlgorithmProvider ? m_defaultAutotileAlgorithmProvider() : QString();
}

void LayoutRegistry::clearAutotileAssignments()
{
    // Flip every Autotile assignment rule to Snapping (preserving both layout
    // fields) and drop autotile quick-layout slots.
    QList<PWR::WindowRule> updated = m_ruleStore->ruleSet().rules();
    QList<QPair<QString, int>> affected; // (screenId, virtualDesktop) for signals
    bool changed = false;

    for (PWR::WindowRule& rule : updated) {
        // Only pure context-assignment rules are flipped — a window-property
        // rule that legitimately carries a SetEngineMode action must not be
        // rebuilt (makeAssignmentActions would drop its other actions).
        if (!isContextAssignmentRule(rule)) {
            continue;
        }
        const AssignmentEntry entry = entryFromRuleMatchActions(rule);
        if (entry.mode != AssignmentEntry::Autotile) {
            continue;
        }
        // Flip to Snapping — preserve both layout fields so re-enabling
        // autotile can restore the previous algorithm.
        rule.actions =
            PWR::ContextRuleBridge::makeAssignmentActions(false, entry.snappingLayout, entry.tilingAlgorithm);
        changed = true;

        // Recover (screen, desktop) for the layoutAssigned signal.
        const ContextDims dims = decodeDims(rule.match);
        affected.append(qMakePair(dims.screenId, dims.virtualDesktop));
    }

    // Drop autotile quick-layout slots.
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end();) {
        if (PhosphorLayout::LayoutId::isAutotile(it.value())) {
            it = m_quickLayoutShortcuts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        m_ruleStore->setAllRules(updated);
        writeQuickLayouts();
        for (const auto& [sid, desk] : std::as_const(affected)) {
            qCDebug(lcZonesLib) << "clearAutotileAssignments: flipped to Snapping for screen=" << sid
                                << "desktop=" << desk;
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
        qCInfo(lcZonesLib) << "Cleared all autotile assignments";
    }
}

// ── Batch setters ───────────────────────────────────────────────────────────

namespace {

// The decoded (screen, desktop, activity) cascade context a single
// batch-assignment hash key pins — each batch setter supplies a key→context
// decoder, the shared driver below works in terms of this tuple.
struct BatchContext
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
};

} // anonymous namespace

// Shared driver for the three per-family batch setters — the numbered steps
// below replace the ~150 near-identical lines the setters used to inline.
// @p decode maps a hash key to its cascade context; @p valid rejects an
// ill-formed context (a desktop entry must pin desktop > 0, an activity
// entry a non-empty activity, else a wrong-family base rule is built);
// @p familyMatches selects which existing rules to drop; @p emitDesktop /
// @p emitActivity are the context the closing layoutAssigned is computed
// under; @p label names the family in log output.
template<typename KeyT, typename DecodeFn, typename ValidFn, typename FamilyFn>
void LayoutRegistry::applyBatchAssignments(const QHash<KeyT, QString>& assignments, DecodeFn decode, ValidFn valid,
                                           FamilyFn familyMatches, int emitDesktop, const QString& emitActivity,
                                           const char* label)
{
    // Step 1 — snapshot existing exact-context entries for every incoming key.
    QHash<KeyT, AssignmentEntry> oldEntries;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        if (const PWR::WindowRule* existing = findExactContextRule(ctx.screenId, ctx.virtualDesktop, ctx.activity)) {
            oldEntries.insert(it.key(), entryFromRuleMatchActions(*existing));
        }
    }

    // Step 2 — drop every rule belonging to this family; keep the rest.
    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (hasEngineModeAction(rule) && familyMatches(rule.match)) {
            continue;
        }
        kept.append(rule);
    }

    // Step 3 — rebuild the family from the incoming assignments.
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        const QString& layoutId = it.value();
        if (!valid(ctx)) {
            qCWarning(lcZonesLib) << "Skipping invalid" << label << "assignment:" << ctx.screenId << ctx.virtualDesktop
                                  << ctx.activity;
            continue;
        }
        const QString logContext = contextRuleName(ctx.screenId, ctx.virtualDesktop, ctx.activity);
        if (shouldSkipLayoutAssignment(layoutId, logContext)) {
            continue;
        }
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldEntries.value(it.key()));
        kept.append(PWR::ContextRuleBridge::makeAssignmentRule(logContext, ctx.screenId, ctx.virtualDesktop,
                                                               ctx.activity, entry.mode == AssignmentEntry::Autotile,
                                                               entry.snappingLayout, entry.tilingAlgorithm));
        storedScreens.insert(ctx.screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    // Step 4 — one commit, then signal per stored screen.
    m_ruleStore->setAllRules(kept);
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, emitDesktop, assignmentIdForScreen(screenId, emitDesktop, emitActivity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << label << "assignments";
}

void LayoutRegistry::setAllScreenAssignments(const QHash<QString, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QString& screenId) {
            return BatchContext{screenId, 0, QString()};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty();
        },
        matchIsExactContextBase,
        /*emitDesktop=*/0, /*emitActivity=*/QString(), "screen");
}

void LayoutRegistry::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QPair<QString, int>& key) {
            return BatchContext{key.first, key.second, QString()};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty() && ctx.virtualDesktop >= 1;
        },
        matchIsExactContextDesktop, m_currentVirtualDesktop, m_currentActivity, "desktop");
}

void LayoutRegistry::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QPair<QString, QString>& key) {
            return BatchContext{key.first, 0, key.second};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty() && !ctx.activity.isEmpty();
        },
        matchIsExactContextActivity, /*emitDesktop=*/0, /*emitActivity=*/QString(), "activity");
}

QHash<QPair<QString, int>, QString> LayoutRegistry::desktopAssignments() const
{
    QHash<QPair<QString, int>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the same per-desktop family classifier the batch setter uses,
        // so a window-property rule carrying an engine-mode action plus an
        // incidental VirtualDesktop== predicate cannot leak in.
        if (!hasEngineModeAction(rule) || !matchIsExactContextDesktop(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.virtualDesktop)] = entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

QHash<QPair<QString, QString>, QString> LayoutRegistry::activityAssignments() const
{
    QHash<QPair<QString, QString>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the same per-activity family classifier the batch setter uses,
        // so a window-property rule carrying an engine-mode action plus an
        // incidental Activity== predicate cannot leak in.
        if (!hasEngineModeAction(rule) || !matchIsExactContextActivity(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.activity)] = entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

} // namespace PhosphorZones
