// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/RuleEvaluator.h>

#include <algorithm>
#include <vector>

#include "windowrulelogging.h"

namespace PhosphorWindowRule {

// ── ResolvedActions ─────────────────────────────────────────────────────

std::optional<RuleAction> ResolvedActions::slot(const QString& slot) const
{
    const auto it = m_slots.constFind(slot);
    if (it == m_slots.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

bool ResolvedActions::fillSlot(const QString& slot, const RuleAction& action)
{
    // First-action-per-slot wins. A slot is "filled" the moment any action
    // lands in it — even an action with empty params. The presence of the
    // key (not the emptiness of the value) is the filled/unfilled signal.
    if (m_slots.contains(slot)) {
        return false;
    }
    m_slots.insert(slot, action);
    return true;
}

// ── RuleEvaluator ───────────────────────────────────────────────────────

RuleEvaluator::RuleEvaluator(const WindowRuleSet& ruleSet)
    : m_ruleSet(ruleSet)
{
}

const QList<int>& RuleEvaluator::priorityOrder() const
{
    const quint64 revision = m_ruleSet.revision();
    const QList<WindowRule>& rules = m_ruleSet.rules();
    // Cache validity is keyed on revision AND rule count: a revision collision
    // (extremely unlikely under a monotonic quint64, but defensive coding
    // matters) against a smaller rule list would otherwise let `resolve()`
    // index past the end of `rules` via a stale cached entry — UB on
    // `rules.at(stale_index)`.
    if (m_priorityOrderValid && m_priorityOrderRevision == revision && m_priorityOrderRulesSize == rules.size()) {
        return m_priorityOrder;
    }

    // Walk in descending priority; ties break by original list order. A
    // stable sort over an index vector preserves that tie-break without
    // copying the rules. The result is cached per revision — the sort runs
    // once per rule-set edit, not once per resolve().
    QList<int> order;
    order.reserve(rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [&rules](int a, int b) {
        return rules.at(a).priority > rules.at(b).priority;
    });

    m_priorityOrder = std::move(order);
    m_priorityOrderRevision = revision;
    m_priorityOrderRulesSize = rules.size();
    m_priorityOrderValid = true;
    return m_priorityOrder;
}

ResolvedActions RuleEvaluator::resolve(const WindowQuery& query) const
{
    ResolvedActions result;

    const QList<WindowRule>& rules = m_ruleSet.rules();
    qCDebug(lcRuleEval) << "resolve(): rules:" << rules.size() << "revision:" << m_ruleSet.revision()
                        << "screen:" << query.screenId << "appId:" << query.appId.value_or(QString());
    if (rules.isEmpty()) {
        return result;
    }

    const QList<int>& order = priorityOrder();
    const ActionRegistry& registry = ActionRegistry::instance();

    for (int index : order) {
        const WindowRule& rule = rules.at(index);
        if (!rule.enabled) {
            continue;
        }
        if (!rule.match.evaluate(query)) {
            continue;
        }
        // A matching rule's actions accumulate per slot. A terminal Exclude
        // action stops the entire walk.
        bool terminate = false;
        for (const RuleAction& action : rule.actions) {
            if (registry.isTerminal(action)) {
                result.markExcluded();
                terminate = true;
                break;
            }
            const QString slot = registry.slotFor(action);
            if (slot.isEmpty()) {
                // A registered action that resolves to no slot cannot
                // contribute — skip it (validation already rejects these on
                // load, so this only guards a programmatically built rule).
                continue;
            }
            result.fillSlot(slot, action);
        }
        if (terminate) {
            break;
        }
    }
    return result;
}

bool RuleEvaluator::hasAnyMatch(const WindowQuery& query) const
{
    for (const WindowRule& rule : m_ruleSet.rules()) {
        if (rule.enabled && rule.match.evaluate(query)) {
            return true;
        }
    }
    return false;
}

bool RuleEvaluator::hasMatchTargetingFields(const WindowQuery& query, const QSet<Field>& fields) const
{
    for (const WindowRule& rule : m_ruleSet.rules()) {
        // Structural `referencesAnyField` is a cheap tree walk with no regex
        // dispatch — gate the (possibly regex-bearing) `evaluate` behind it so
        // a rule that never mentions one of `fields` is rejected for free.
        if (rule.enabled && rule.match.referencesAnyField(fields) && rule.match.evaluate(query)) {
            return true;
        }
    }
    return false;
}

const WindowRule* RuleEvaluator::highestPriorityMatch(const WindowQuery& query,
                                                      const std::function<bool(const WindowRule&)>& filter) const
{
    // Descending priority; ties broken by original list order (first wins).
    // The cached priority order encodes exactly that — walk it and return the
    // first qualifying rule so the tie-break matches resolve()'s walk.
    const QList<WindowRule>& rules = m_ruleSet.rules();
    for (int index : priorityOrder()) {
        const WindowRule& rule = rules.at(index);
        if (!rule.enabled) {
            continue;
        }
        if (filter && !filter(rule)) {
            continue;
        }
        if (!rule.match.evaluate(query)) {
            continue;
        }
        return &rule;
    }
    return nullptr;
}

void RuleEvaluator::evictCache(quint64 currentRevision) const
{
    // Pass 1 — drop every stale-revision entry. A rule-set edit retires the
    // entire previous generation, so this alone reclaims most of the cache
    // whenever rules change.
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->revision != currentRevision) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }

    // Pass 2 — if the live (current-revision) set still exceeds the cap,
    // evict oldest-inserted first until it fits. This bounds the cache even
    // when the revision never changes but new window ids keep arriving (the
    // evaluator never sees window-close events).
    //
    // A repeated "linear-scan for the single oldest, erase, repeat" loop is
    // O(n²) when many entries must drop at once. Instead find the insertSeq
    // cutoff once with nth_element (O(n) average), then erase every entry at
    // or below it in a single sweep (O(n)).
    const int overflow = m_cache.size() - kMaxCacheEntries;
    if (overflow <= 0) {
        return;
    }
    std::vector<quint64> seqs;
    seqs.reserve(m_cache.size());
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        seqs.push_back(it->insertSeq);
    }
    // The element at index (overflow - 1) is the largest insertSeq among the
    // `overflow` oldest entries — every entry with insertSeq <= cutoff must go.
    std::nth_element(seqs.begin(), seqs.begin() + (overflow - 1), seqs.end());
    const quint64 cutoff = seqs[overflow - 1];

    // insertSeq is monotonically unique, so `<= cutoff` selects exactly the
    // `overflow` oldest entries — no tie can over- or under-evict.
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->insertSeq <= cutoff) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

ResolvedActions RuleEvaluator::resolveCached(const QString& windowId, const WindowQuery& query) const
{
    const quint64 revision = m_ruleSet.revision();

    const auto it = m_cache.constFind(windowId);
    if (it != m_cache.constEnd() && it->revision == revision) {
        return it->actions;
    }

    ResolvedActions result = resolve(query);
    m_cache.insert(windowId, CacheEntry{revision, m_cacheInsertSeq++, result});
    evictCache(revision);
    return result;
}

std::optional<ResolvedActions> RuleEvaluator::resolveCachedIfPresent(const QString& windowId) const
{
    const auto it = m_cache.constFind(windowId);
    if (it != m_cache.constEnd() && it->revision == m_ruleSet.revision()) {
        return it->actions;
    }
    return std::nullopt;
}

void RuleEvaluator::clearCache() const
{
    m_cache.clear();
}

} // namespace PhosphorWindowRule
