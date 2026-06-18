// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>

namespace PhosphorWindowRules {
class WindowRuleStore;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the unified WindowRule store.
 *
 * Provides D-Bus interface: @c org.plasmazones.WindowRules
 *
 * Hand-written (no @c .xml codegen) like every other adaptor in this
 * codebase. The settings app / KCM talks to this adaptor; the daemon's
 * @ref WindowRuleStore stays the sole writer of @c windowrules.json.
 *
 * Rules cross the wire as JSON strings — a single `WindowRule` serializes to
 * the same @c { id, name, enabled, priority, match, actions } object the
 * store persists, and the whole set is a JSON object @c { _version, rules }.
 * JSON keeps the nested match-expression / action shapes intact without a
 * bespoke D-Bus type registration.
 */
class PLASMAZONES_EXPORT WindowRuleAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.WindowRules")

public:
    explicit WindowRuleAdaptor(PhosphorWindowRules::WindowRuleStore* store, QObject* parent = nullptr);
    ~WindowRuleAdaptor() override = default;

    /// Null the borrowed store pointer and sever the rulesChanged wiring.
    /// Called from Daemon::stop() before the owning unique_ptr destroys the
    /// store, so a late D-Bus call degrades to a null-safe no-op.
    void detach();

public Q_SLOTS:
    /// The whole rule set as a JSON object string (@c { _version, rules }).
    QString getAllRules();

    /// Replace the entire rule set. @p rulesJson is a JSON object string in
    /// the @c { _version, rules } shape (or a bare @c rules array). Invalid
    /// rules are dropped. Returns true if the payload parsed.
    bool setAllRules(const QString& rulesJson);

    /// Append one rule from its JSON object string. Returns false if the
    /// JSON is malformed, the rule is invalid, or its id collides.
    bool addRule(const QString& ruleJson);

    /// Replace the rule with the same id from its JSON object string.
    /// Returns false if the JSON is malformed or no such rule exists.
    bool updateRule(const QString& ruleJson);

    /// Remove the rule with @p ruleId (a QUuid string). Returns false if no
    /// such rule exists.
    bool removeRule(const QString& ruleId);

    /// Set the enabled flag of the rule with @p ruleId. Returns false if no
    /// such rule exists.
    bool setRuleEnabled(const QString& ruleId, bool enabled);

    /// Set the priority of the rule with @p ruleId. Returns false if no such
    /// rule exists.
    bool setRulePriority(const QString& ruleId, int priority);

Q_SIGNALS:
    /// Emitted whenever the store's rule set changes. @p persisted forwards
    /// the upstream contract: true means the change is on disk, false means
    /// the in-memory mutation succeeded but the persist did not.
    void rulesChanged(bool persisted);

private:
    PhosphorWindowRules::WindowRuleStore* m_store;
};

} // namespace PlasmaZones
