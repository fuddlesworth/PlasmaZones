// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

#include "Rule.h"
#include "phosphorrules_export.h"

namespace PhosphorRules {

/**
 * @brief An ordered collection of Rules — the persistent rule store.
 *
 * Serializes to `rules.json` with `"_version": 4`. The set carries a
 * **monotonic revision counter** bumped on every mutation; the RuleEvaluator
 * keys its match cache on `(windowId, revision)` so an edit transparently
 * invalidates stale resolutions.
 *
 * `fromJson` **refuses** a non-4 `_version` — schema migration is the config
 * layer's job, never the library's.
 */
class PHOSPHORRULES_EXPORT RuleSet
{
public:
    /// The schema version this library reads and writes.
    static constexpr int SchemaVersion = 4;

    RuleSet() = default;

    // ── Accessors ──
    const QList<Rule>& rules() const
    {
        return m_rules;
    }
    int count() const
    {
        return m_rules.size();
    }
    bool isEmpty() const
    {
        return m_rules.isEmpty();
    }

    /// The monotonic revision — bumped on every mutating call. Starts at 0
    /// for an empty default-constructed set.
    quint64 revision() const
    {
        return m_revision;
    }

    /// The rule with @p id, or nullopt if not present.
    std::optional<Rule> ruleById(const QUuid& id) const;

    // ── Mutators — each bumps the revision ──

    /// Append a rule. Returns false (no revision bump) if the rule is invalid
    /// or its id collides with an existing rule.
    bool addRule(const Rule& rule);

    /// Replace the rule with the same id. Returns false if no such rule
    /// exists or the replacement is invalid.
    bool updateRule(const Rule& rule);

    /// Remove the rule with @p id. Returns false if no such rule exists.
    bool removeRule(const QUuid& id);

    /// Replace the entire ordered rule list. Invalid rules are dropped with a
    /// logged diagnostic. Bumps the revision on every call, even if the
    /// post-validation list is structurally identical to the current set —
    /// downstream consumers keyed on revision must rebuild on every set.
    /// The cache-invalidation pessimization is intentional; the round-trip
    /// `addRule(X)` → `setRules(originalList)` would otherwise let a stale
    /// cache survive a real edit. Returns the accepted count.
    int setRules(const QList<Rule>& rules);

    /// Drop every rule. Bumps the revision iff the set was non-empty (a no-op
    /// clear does not bump).
    void clear();

    // ── Serialization ──

    QJsonObject toJson() const;

    /// Strict loader. Refuses a non-4 `_version` (returns nullopt). Malformed
    /// individual rules are dropped with a logged diagnostic — the set still
    /// loads. The loaded set's revision is reset to 0.
    static std::optional<RuleSet> fromJson(const QJsonObject& obj);

    /// Load from @p path (an explicit file path — the library never derives
    /// config locations). Returns nullopt on a missing/unreadable file,
    /// malformed JSON, or a version mismatch.
    static std::optional<RuleSet> loadFromFile(const QString& path);

    /// Canonicalize-on-save: serialize and atomically write to @p path
    /// (temp-file + rename). Returns false on any I/O failure.
    bool saveToFile(const QString& path) const;

    /// Equality compares the rule LIST only; @ref revision is intentionally
    /// ignored. The store's no-op fast path uses this to skip an in-place
    /// `setRules(candidate.rules())` (and the revision bump it would
    /// imply) when the post-validation content has not changed — the
    /// stored revision stays monotonic because either both sides are
    /// equal (no bump) or the live `setRules` advances it past the prior
    /// peak.
    bool operator==(const RuleSet& other) const
    {
        return m_rules == other.m_rules;
    }
    bool operator!=(const RuleSet& other) const
    {
        return !(*this == other);
    }

private:
    QList<Rule> m_rules;
    quint64 m_revision = 0;
};

} // namespace PhosphorRules
