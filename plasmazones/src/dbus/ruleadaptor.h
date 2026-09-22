// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>

namespace PhosphorRules {
class RuleStore;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the unified Rule store.
 *
 * Provides D-Bus interface: @c org.plasmazones.Rules
 *
 * Hand-written (no @c .xml codegen) like every other adaptor in this
 * codebase. The settings app / KCM talks to this adaptor for the daemon's
 * live view of the rules. That store is NOT the only writer of
 * @c rules.json — the settings app writes the file in its own process on a
 * reset, on a per-mode engine disable, and on a config import. Every such
 * out-of-process write therefore has to be followed by @ref reloadRules, or
 * the daemon keeps serving (and re-persisting) its pre-write set.
 *
 * Rules cross the wire as JSON strings — a single `Rule` serializes to
 * the same @c { id, name, enabled, priority, match, actions } object the
 * store persists, and the whole set is a JSON object @c { _version, rules }.
 * JSON keeps the nested match-expression / action shapes intact without a
 * bespoke D-Bus type registration.
 */
class PLASMAZONES_EXPORT RuleAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Rules")

public:
    explicit RuleAdaptor(PhosphorRules::RuleStore* store, QObject* parent = nullptr);
    ~RuleAdaptor() override = default;

    /// Null the borrowed store pointer and sever the rulesChanged wiring.
    /// Called from Daemon::stop() before the owning unique_ptr destroys the
    /// store, so a late D-Bus call degrades to a null-safe no-op.
    void detach();

public Q_SLOTS:
    /// The whole rule set as a JSON object string (@c { _version, rules }).
    QString getAllRules();

    /// Replace the entire rule set. @p rulesJson is a JSON object string in
    /// the @c { _version, rules } shape (or a bare object whose @c rules array
    /// carries the list). Individually invalid rules are dropped, with the
    /// counts logged.
    ///
    /// Returns true only when the accepted set was both committed and
    /// persisted. A payload identical to the persisted set is a no-op that
    /// still returns true. False covers a payload above the ~1M-character cap, malformed JSON
    /// or a non-object document, a @c _version that is present and disagrees
    /// with the schema this build reads, a missing @c rules array, a non-empty
    /// payload whose every rule was dropped (a rejected payload, not a clear),
    /// and an in-memory replace whose file write failed.
    bool setAllRules(const QString& rulesJson);

    /// Append one rule from its JSON object string. Returns false if the
    /// payload is over the ~1M-character cap, the JSON is malformed, the rule is
    /// invalid, its id collides, or the file write failed.
    bool addRule(const QString& ruleJson);

    /// Replace the rule with the same id from its JSON object string. Returns
    /// false if the payload is over the ~1M-character cap, the JSON is malformed, the
    /// rule is invalid, no such rule exists, or the file write failed.
    bool updateRule(const QString& ruleJson);

    /// Remove the rule with @p ruleId (a QUuid string). Returns false if the
    /// id is malformed, no such rule exists, or the file write failed.
    bool removeRule(const QString& ruleId);

    /// Set the enabled flag of the rule with @p ruleId. A true return means
    /// the rule now has the requested state, which includes the case where it
    /// already had it — it does not mean anything changed. Returns false if
    /// the id is malformed, no such rule exists, or the file write failed.
    bool setRuleEnabled(const QString& ruleId, bool enabled);

    /// Set the priority of the rule with @p ruleId. Like @ref setRuleEnabled,
    /// a true return means the rule now carries the requested priority, not
    /// that it changed. Returns false if the id is malformed, no such rule
    /// exists, or the file write failed.
    bool setRulePriority(const QString& ruleId, int priority);

    /// Global Restore Defaults hook for the rule store. Window appearance / gap
    /// defaults live in the config store now, so this only strips any stale
    /// managed appearance baseline rules an older build may have left in
    /// rules.json, preserving every user-authored rule. Reloads the store first
    /// (to pick up an out-of-process write such as Settings::reset()'s
    /// disable-rule drop), then persists once if anything was removed. May emit
    /// rulesChanged up to twice — once from the reload if the on-disk set changed,
    /// once from the strip.
    ///
    /// Returns void by design, so a failed write of the stripped set is warned
    /// about in the daemon log and nowhere else. The caller is the global
    /// Restore Defaults, which is fire-and-forget and has nothing to do with a
    /// failure anyway; the store's in-memory set is still stripped, and the
    /// following rulesChanged carries @c persisted = false for a consumer that
    /// wants to know the disk copy diverged.
    void resetManagedDefaults();

    /// Re-read rules.json from disk.
    ///
    /// The daemon's store is borrowed by whoever owns it, and reloadSettings()
    /// deliberately does not reload a borrowed store. An out-of-process rewrite of
    /// rules.json — the settings app's config import is the one that matters —
    /// therefore leaves this store serving the pre-write set until something asks
    /// for this. load() is idempotent and emits rulesChanged only when the on-disk
    /// content actually differs.
    void reloadRules();

Q_SIGNALS:
    /// Emitted whenever the store's rule set changes. @p persisted forwards
    /// the upstream contract: true means the change is on disk, false means
    /// the in-memory mutation succeeded but the persist did not.
    void rulesChanged(bool persisted);

private:
    PhosphorRules::RuleStore* m_store;
};

} // namespace PlasmaZones
