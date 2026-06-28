// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QUuid>

#include "RuleSet.h"
#include "phosphorrules_export.h"

namespace PhosphorRules {

/**
 * @brief Persistent store for the unified Rule set.
 *
 * Owns an in-memory @ref RuleSet and persists it to a caller-supplied
 * file path (the daemon points it at @c ~/.config/plasmazones/rules.json,
 * schema v4). The daemon is the **sole writer** — settings / KCM mutate the
 * store over D-Bus, never the file directly — so a single file is
 * contention-free.
 *
 * The store is the runtime counterpart of the config migration: @c migrateV3ToV4
 * writes the initial store as @c windowrules.json, then @c migrateV4ToV5 renames
 * it to @c rules.json and folds the per-mode appearance / gap overrides in. The
 * store reads that file on daemon start and owns every subsequent mutation.
 *
 * Loader robustness follows the @ref RuleSet contract: a missing file
 * yields an empty set (a fresh install or a config that never had any rules),
 * malformed individual rules are dropped with a logged diagnostic, and every
 * save canonicalizes (temp-file + atomic rename).
 *
 * This class lives in the LGPL @c phosphor-rules library so that both the
 * LGPL @c phosphor-zones registry and the GPL daemon can own/borrow it without
 * a GPL→LGPL dependency inversion. It is a plain @c QObject (Qt6::Core only) —
 * it never derives a config location itself; the caller passes the path.
 *
 * Separate-process consumers that own their own store and have no D-Bus path to
 * the daemon (standalone @c plasmazones-settings, @c plasmazones-editor) can
 * opt into cross-process auto-reload via @ref RuleStoreWatcher, which
 * watches the file and drives @ref load() on external writes. The store's
 * idempotent @ref load() (it emits only on a real content change) makes that
 * watcher's self-write events harmless without any extra bookkeeping here.
 */
class PHOSPHORRULES_EXPORT RuleStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @param filePath Absolute path to @c rules.json. Required — the
     *                 library never derives config locations; an empty path
     *                 is a developer error and is asserted against.
     * @param parent   Qt parent.
     */
    explicit RuleStore(const QString& filePath, QObject* parent = nullptr);
    ~RuleStore() override = default;

    /// The backing file path.
    QString filePath() const
    {
        return m_filePath;
    }

    /// Read-only access to the live rule set.
    const RuleSet& ruleSet() const
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
    /// up empty. Replaces the in-memory set and emits @ref rulesChanged only
    /// when the loaded content actually differs from the in-memory set; an
    /// idempotent re-load of an unchanged file does not emit (downstream
    /// cache flushes would otherwise fire for nothing).
    void load();

    /// Persist the in-memory set to disk (atomic temp-write + rename).
    /// Returns false on an I/O failure. Does not emit @ref rulesChanged
    /// (the in-memory state did not change).
    bool save();

    // ─── Mutation — every mutator persists then emits rulesChanged ────────
    //
    // Every mutator keeps the in-memory set and the on-disk file consistent:
    // the in-memory set is updated first, then persisted. A failed `save()`
    // makes the mutator return false — the in-memory set still reflects the
    // requested change, but the caller knows the file is now stale.

    /// Replace the entire rule list. Invalid rules are dropped. A no-op
    /// replacement (the incoming list equals the current set) skips the
    /// persist and the emit and returns true. Returns false only on an I/O
    /// failure while persisting an actual change.
    bool setAllRules(const QList<Rule>& rules);

    /// Append a rule. Returns false if the rule is invalid, its id collides
    /// with an existing rule (no persist, no emit), or the persist failed.
    bool addRule(const Rule& rule);

    /// Replace the rule with the same id. Returns false if no such rule
    /// exists, the replacement is invalid, or the persist failed.
    bool updateRule(const Rule& rule);

    /// Remove the rule with @p id. Returns false if no such rule exists or
    /// the persist failed.
    bool removeRule(const QUuid& id);

    /// Set the enabled flag of the rule with @p id. Returns false if no
    /// such rule exists or the persist failed. A no-op change (already at
    /// @p enabled) returns true without persisting or emitting.
    bool setRuleEnabled(const QUuid& id, bool enabled);

    /// Set the priority of the rule with @p id. Returns false if no such
    /// rule exists or the persist failed. A no-op change returns true
    /// without persisting/emitting.
    bool setRulePriority(const QUuid& id, int priority);

Q_SIGNALS:
    /// Emitted whenever the in-memory rule set changes (load or any mutator).
    ///
    /// @p persisted reports whether the change was written to disk
    /// successfully. @c true is the normal case (and is always true for a
    /// load — the disk is the source). @c false signals "in-memory state
    /// changed but the persist failed" — consumers that need to differentiate
    /// "user saved" from "user edited but the rule store is now divergent
    /// from disk" can branch on this flag.
    void rulesChanged(bool persisted);

private:
    QString m_filePath;
    RuleSet m_ruleSet;
};

} // namespace PhosphorRules
