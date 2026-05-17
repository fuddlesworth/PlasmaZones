// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/RuleEvaluator.h>

#include <algorithm>

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

ResolvedActions RuleEvaluator::resolve(const WindowQuery& query) const
{
    ResolvedActions result;

    const QList<WindowRule>& rules = m_ruleSet.rules();
    if (rules.isEmpty()) {
        return result;
    }

    // Walk in descending priority; ties break by original list order. A
    // stable sort over an index vector preserves that tie-break without
    // copying the rules.
    QList<int> order;
    order.reserve(rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [&rules](int a, int b) {
        return rules.at(a).priority > rules.at(b).priority;
    });

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

ResolvedActions RuleEvaluator::resolveCached(const QString& windowId, const WindowQuery& query) const
{
    const quint64 revision = m_ruleSet.revision();

    const auto it = m_cache.constFind(windowId);
    if (it != m_cache.constEnd() && it->revision == revision) {
        return it->actions;
    }

    ResolvedActions result = resolve(query);
    m_cache.insert(windowId, CacheEntry{revision, result});
    return result;
}

void RuleEvaluator::clearCache() const
{
    m_cache.clear();
}

} // namespace PhosphorWindowRule
