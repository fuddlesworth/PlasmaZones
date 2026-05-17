// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QString>

#include <optional>

#include "RuleAction.h"
#include "WindowQuery.h"
#include "WindowRuleSet.h"
#include "phosphorwindowrule_export.h"

namespace PhosphorWindowRule {

/**
 * @brief The output of a resolution — the first action filling each slot.
 *
 * Conflict resolution is **first-matching-rule-wins per slot**: as the
 * evaluator walks rules in descending priority, the first action it sees for
 * a given slot fills that slot and later actions for the same slot are
 * ignored. Actions in different slots stack.
 *
 * Critically, a slot is recorded as **filled** the moment an action lands in
 * it — even if that action's `params` are empty. `slot(...)` returns a
 * present-but-empty value, distinguishable from a never-filled slot
 * (`std::nullopt`). The animation engaged-empty `effectId` sentinel ("a rule
 * matched and deliberately blocks the shader" vs "no rule matched") depends
 * on exactly this distinction.
 */
class PHOSPHORWINDOWRULE_EXPORT ResolvedActions
{
public:
    /// True if @p slot was filled by some matching rule.
    bool hasSlot(const QString& slot) const
    {
        return m_slots.contains(slot);
    }

    /// The action that filled @p slot, or nullopt if the slot is unfilled.
    /// A filled slot may carry an action with empty params — that is NOT the
    /// same as an unfilled slot.
    std::optional<RuleAction> slot(const QString& slot) const;

    /// True if a terminal Exclude action matched — the window is unmanaged.
    bool isExcluded() const
    {
        return m_excluded;
    }

    /// True if no slot was filled and no terminal action matched.
    bool isEmpty() const
    {
        return m_slots.isEmpty() && !m_excluded;
    }

    /// All filled slot ids — for tests / introspection.
    QStringList filledSlots() const
    {
        return m_slots.keys();
    }

    // ── Accumulation API — used by RuleEvaluator ──

    /// Records @p action into @p slot iff the slot is currently unfilled.
    /// Returns true if it filled the slot, false if the slot was already filled.
    bool fillSlot(const QString& slot, const RuleAction& action);

    /// Marks the result excluded (a terminal Exclude action matched).
    void markExcluded()
    {
        m_excluded = true;
    }

    bool operator==(const ResolvedActions& other) const
    {
        return m_slots == other.m_slots && m_excluded == other.m_excluded;
    }

private:
    QHash<QString, RuleAction> m_slots;
    bool m_excluded = false;
};

/**
 * @brief The single evaluation model — resolves a WindowQuery against a
 *        WindowRuleSet.
 *
 * `resolve()` walks the rule set in **descending priority** (ties broken by
 * list order via a stable sort), accumulating the first action that fills
 * each slot. A matching rule with a terminal `Exclude` action stops the walk
 * and the result is marked excluded.
 *
 * `resolveCached()` adds a match cache keyed `(windowId, ruleSetRevision)`.
 * The cache is automatically bypassed/invalidated when the bound rule set's
 * revision changes; `clearCache()` forces invalidation for metadata-driven
 * changes that the revision does not capture (a window changing screen).
 *
 * The evaluator holds a reference to the rule set — the caller owns the set's
 * lifetime and must outlive the evaluator.
 */
class PHOSPHORWINDOWRULE_EXPORT RuleEvaluator
{
public:
    explicit RuleEvaluator(const WindowRuleSet& ruleSet);

    /// Resolve @p query against the bound rule set. Always recomputes.
    ResolvedActions resolve(const WindowQuery& query) const;

    /// Resolve with a `(windowId, revision)` cache. @p windowId is the
    /// caller's stable per-window key (the `appId|instanceId` composite id).
    /// A cache entry from a stale revision is discarded on access.
    ResolvedActions resolveCached(const QString& windowId, const WindowQuery& query) const;

    /// True if at least one enabled rule matches @p query — an event-agnostic
    /// query that does not allocate a ResolvedActions. Used by hot paths
    /// that only need a yes/no ("does any rule re-enable this class").
    bool hasAnyMatch(const WindowQuery& query) const;

    /// Drop the entire match cache. Call on a window-metadata change that the
    /// rule-set revision does not reflect.
    void clearCache() const;

    /// Number of live cache entries — for tests / benchmarks.
    int cacheSize() const
    {
        return m_cache.size();
    }

    const WindowRuleSet& ruleSet() const
    {
        return m_ruleSet;
    }

private:
    const WindowRuleSet& m_ruleSet;

    struct CacheEntry
    {
        quint64 revision = 0;
        ResolvedActions actions;
    };
    mutable QHash<QString, CacheEntry> m_cache;
};

} // namespace PhosphorWindowRule
