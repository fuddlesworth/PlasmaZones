// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <algorithm>
#include <functional>
#include <optional>

#include "RuleAction.h"
#include "WindowQuery.h"
#include "RuleSet.h"
#include "phosphorrules_export.h"

namespace PhosphorRules {

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
class PHOSPHORRULES_EXPORT ResolvedActions
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
    ///
    /// Returned sorted so tests can compare against a stable list; without
    /// the sort, `QHash::keys()` order is unspecified and varies across
    /// runs (Qt hash seeds are randomised per-process). Callers are not
    /// expected to be on a hot path.
    QStringList filledSlots() const
    {
        QStringList keys = m_slots.keys();
        std::sort(keys.begin(), keys.end());
        return keys;
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
 *        RuleSet.
 *
 * `resolve()` walks the rule set in **descending priority** (ties broken by
 * list order via a stable sort), accumulating the first action that fills
 * each slot. A matching rule with a terminal `Exclude` action stops the walk
 * and the result is marked excluded. The descending-priority index is
 * computed once per rule-set revision and reused, so back-to-back resolves
 * against an unchanged set do not re-sort.
 *
 * `resolveCached()` adds a match cache keyed `(windowId, ruleSetRevision)`.
 * The cache is automatically bypassed/invalidated when the bound rule set's
 * revision changes; `clearCache()` forces invalidation for metadata-driven
 * changes that the revision does not capture (a window changing screen).
 *
 * The cache is **bounded**: at most @ref kMaxCacheEntries live entries. Two
 * eviction passes keep it from growing without limit even though the
 * evaluator never observes window-close events — every `resolveCached` call
 * first drops every entry whose revision is stale (a rule-set edit retires
 * the whole previous generation), and if the live set still exceeds the cap
 * the oldest-inserted entries are evicted until it fits.
 *
 * The evaluator holds a reference to the rule set — the caller owns the set's
 * lifetime and must outlive the evaluator.
 *
 * Thread-safety: `hasAnyMatch()` reads the bound rule set in list order and
 * touches no internal `RuleEvaluator` cache or index. It is *only* safe to
 * call concurrently with *other* `hasAnyMatch()` calls when **(a)** the bound
 * `RuleSet` is not concurrently mutated **and (b)** no rule in the set
 * carries a `Regex` leaf — `MatchExpression::evaluate()` dispatches a regex
 * leaf through a shared `QRegularExpression` whose `match()` is not
 * thread-safe for concurrent calls (see the `MatchExpression` thread-safety
 * note). A rule set containing any `Regex` predicate must have all
 * `hasAnyMatch()` calls serialized. `resolve()`, `resolveCached()` and
 * `highestPriorityMatch()` mutate the lazily-built `mutable` priority-order
 * index (and, for `resolveCached()`, the `mutable` match cache);
 * `clearCache()` mutates only the match cache. All of these latter calls must
 * be externally serialized — both against each other and against
 * `hasAnyMatch()` (since concurrent rule-set mutation invalidates its read).
 */
class PHOSPHORRULES_EXPORT RuleEvaluator
{
public:
    explicit RuleEvaluator(const RuleSet& ruleSet);

    /// Resolve @p query against the bound rule set. Always recomputes.
    ResolvedActions resolve(const WindowQuery& query) const;

    /// Resolve with a `(windowId, revision)` cache. @p windowId is the
    /// caller's stable per-window key (the `appId|instanceId` composite id).
    /// A cache entry from a stale revision is discarded on access. The cache
    /// is bounded — see the class doc for the eviction policy.
    ResolvedActions resolveCached(const QString& windowId, const WindowQuery& query) const;

    /// Peek the match cache without resolving: returns the cached verdict for
    /// @p windowId iff one exists at the CURRENT rule-set revision, else nullopt.
    /// Lets a hot-path caller (per-frame paint resolvers) skip building the
    /// WindowQuery entirely on a cache hit — the cached verdict already reflects
    /// whatever query produced it, and the query is ignored by resolveCached on a
    /// hit anyway. A stale-revision entry reads as a miss (nullopt) here; it is
    /// pruned lazily on the next resolveCached call, not by this read-only peek.
    std::optional<ResolvedActions> resolveCachedIfPresent(const QString& windowId) const;

    /// True if at least one enabled rule matches @p query — an existence
    /// test that does not allocate a ResolvedActions. Used by hot paths that
    /// only need a yes/no ("does any rule re-enable this class"). Iterates in
    /// rule-set list order, **not** priority order: priority is irrelevant to
    /// a pure existence check, so no sort is performed.
    ///
    /// Not concurrency-safe against a rule set containing a `Regex` predicate
    /// — see the class-level thread-safety note.
    bool hasAnyMatch(const WindowQuery& query) const;

    /// True if at least one enabled rule **both** matches @p query **and**
    /// references one of @p fields in its match expression. The cheap
    /// structural `referencesAnyField` filter runs before the (potentially
    /// regex-bearing) `evaluate`, so a rule that does not mention any of the
    /// fields never pays the match cost.
    ///
    /// Used by the animation window-filter to distinguish a rule that
    /// deliberately targets a window type (so it should override the global
    /// "ignore transient / ignore notifications-OSD" exclusion) from one that
    /// merely matches a transient window by class as a side effect. Iterates
    /// in rule-set list order — priority is irrelevant to an existence check.
    ///
    /// Same concurrency constraints as `hasAnyMatch` (regex leaves).
    bool hasMatchTargetingFields(const WindowQuery& query, const QSet<Field>& fields) const;

    /// The single highest-priority **enabled** rule that matches @p query and
    /// passes the optional @p filter, or nullptr if none qualifies.
    ///
    /// This is the same descending-priority, tie-break-by-list-order walk
    /// @ref resolve performs, exposed as a whole-rule lookup. Callers that
    /// need the winning rule itself — not its accumulated action slots —
    /// use this so the priority-cascade semantics live in exactly one place.
    /// @p filter, when set, narrows the candidate set (e.g. "only context
    /// assignment rules"); an empty @p filter considers every enabled rule.
    /// The returned pointer aliases into the bound rule set and is valid only
    /// until the next rule-set mutation.
    const Rule* highestPriorityMatch(const WindowQuery& query,
                                     const std::function<bool(const Rule&)>& filter = {}) const;

    /// Drop the entire match cache. Call on a window-metadata change that the
    /// rule-set revision does not reflect.
    void clearCache() const;

    /// Number of live cache entries — for tests / benchmarks.
    int cacheSize() const
    {
        return m_cache.size();
    }

    const RuleSet& ruleSet() const
    {
        return m_ruleSet;
    }

    /// Upper bound on live `resolveCached` entries. Past this, the
    /// oldest-inserted entries are evicted. Sized generously — far more than
    /// any realistic concurrent window count — so the cap is a safety net,
    /// not a routine hot-path constraint.
    static constexpr int kMaxCacheEntries = 4096;

private:
    const RuleSet& m_ruleSet;

    struct CacheEntry
    {
        quint64 revision = 0;
        quint64 insertSeq = 0; ///< monotonic insert order — drives oldest-first eviction
        ResolvedActions actions;
    };
    mutable QHash<QString, CacheEntry> m_cache;
    mutable quint64 m_cacheInsertSeq = 0; ///< next insertSeq to hand out

    /// Drops every cache entry whose revision != @p currentRevision, then —
    /// if the survivors still exceed @ref kMaxCacheEntries — evicts the
    /// oldest-inserted entries until the cap is met.
    void evictCache(quint64 currentRevision) const;

    /// The rule indices in descending-priority / list-order tie-break order,
    /// computed once per rule-set revision and reused. Rebuilt lazily when
    /// the bound set's revision moves. The cache key includes the rule count
    /// as a defensive cross-check: a revision collision against a smaller rule
    /// list would otherwise let `resolve()` dereference a stale index that no
    /// longer exists (UB on `rules.at(...)`).
    mutable QList<int> m_priorityOrder;
    mutable quint64 m_priorityOrderRevision = 0;
    mutable qsizetype m_priorityOrderRulesSize = 0;
    mutable bool m_priorityOrderValid = false;
    const QList<int>& priorityOrder() const;
};

} // namespace PhosphorRules
