// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorWindowRule/WindowRuleSet.h>

#include <QObject>
#include <QString>
#include <QUuid>

namespace PlasmaZones {

/**
 * @brief Daemon-side persistent store for the unified WindowRule set.
 *
 * Owns an in-memory @ref PhosphorWindowRule::WindowRuleSet and persists it to
 * @c ~/.config/plasmazones/windowrules.json (schema v4). The daemon is the
 * **sole writer** — settings / KCM mutate the store over D-Bus, never the
 * file directly — so a single file is contention-free (same model as the
 * settings-architecture-refactor's daemon-sole-writer rule).
 *
 * The store is the runtime counterpart of the @c migrateV3ToV4 migration: the
 * migration writes the initial @c windowrules.json, the store reads it on
 * daemon start and owns every subsequent mutation.
 *
 * Loader robustness follows the @ref PhosphorWindowRule::WindowRuleSet
 * contract: a missing file yields an empty set (a fresh install or a config
 * that never had any rules), malformed individual rules are dropped with a
 * logged diagnostic, and every save canonicalizes (temp-file + atomic rename).
 */
class PLASMAZONES_EXPORT WindowRuleStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @param filePath Absolute path to @c windowrules.json. Defaults to
     *                 @ref ConfigDefaults::windowRulesFilePath when empty.
     * @param parent   Qt parent.
     */
    explicit WindowRuleStore(const QString& filePath = QString(), QObject* parent = nullptr);
    ~WindowRuleStore() override = default;

    /// The backing file path.
    QString filePath() const
    {
        return m_filePath;
    }

    /// Read-only access to the live rule set.
    const PhosphorWindowRule::WindowRuleSet& ruleSet() const
    {
        return m_ruleSet;
    }

    /// Number of rules in the store.
    int count() const
    {
        return m_ruleSet.count();
    }

    /// True if a rule with @p id is present.
    bool contains(const QUuid& id) const
    {
        return m_ruleSet.ruleById(id).has_value();
    }

    // ─── Persistence ──────────────────────────────────────────────────────

    /// (Re)load from disk. A missing file is not an error — the store ends
    /// up empty. Replaces the in-memory set; emits @ref rulesChanged.
    void load();

    /// Persist the in-memory set to disk (atomic temp-write + rename).
    /// Returns false on an I/O failure. Does not emit @ref rulesChanged
    /// (the in-memory state did not change).
    bool save();

    // ─── Mutation — every mutator persists then emits rulesChanged ────────

    /// Replace the entire rule list. Invalid rules are dropped. Persists +
    /// emits.
    void setAllRules(const QList<PhosphorWindowRule::WindowRule>& rules);

    /// Append a rule. Returns false (no persist, no emit) if the rule is
    /// invalid or its id collides with an existing rule.
    bool addRule(const PhosphorWindowRule::WindowRule& rule);

    /// Replace the rule with the same id. Returns false if no such rule
    /// exists or the replacement is invalid.
    bool updateRule(const PhosphorWindowRule::WindowRule& rule);

    /// Remove the rule with @p id. Returns false if no such rule exists.
    bool removeRule(const QUuid& id);

    /// Set the enabled flag of the rule with @p id. Returns false if no
    /// such rule exists. A no-op change (already at @p enabled) returns
    /// true without persisting or emitting.
    bool setRuleEnabled(const QUuid& id, bool enabled);

    /// Set the priority of the rule with @p id. Returns false if no such
    /// rule exists. A no-op change returns true without persisting/emitting.
    bool setRulePriority(const QUuid& id, int priority);

Q_SIGNALS:
    /// Emitted whenever the in-memory rule set changes (load or any mutator).
    void rulesChanged();

private:
    QString m_filePath;
    PhosphorWindowRule::WindowRuleSet m_ruleSet;
};

} // namespace PlasmaZones
