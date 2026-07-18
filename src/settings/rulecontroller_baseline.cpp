// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-page dirty split + global managed reset for RuleController.
// The controller's class definition spans this TU and rulecontroller.cpp.
//
// Covers: the value-based user-rule dirty check (userRulesDirty against the last
// daemon-synced snapshot) and the fire-and-forget global daemon reset
// (resetManagedDefaults). Window appearance defaults moved to the config store,
// so the per-page appearance-baseline reset/discard that used to live here is
// gone — the Rules page carries only user-authored rules now.

#include "rulecontroller.h"

#include "../core/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/Rule.h>

#include <QList>
#include <QSet>
#include <QString>

namespace PlasmaZones {

namespace {
// The user rules (order preserved so the comparison catches reorders). No managed
// rules are seeded now that appearance is config-backed, so every rule is a user
// rule; this filter stays for the value-based dirty comparison.
QList<PhosphorRules::Rule> userSubset(const QList<PhosphorRules::Rule>& rules)
{
    QList<PhosphorRules::Rule> out;
    for (const PhosphorRules::Rule& r : rules) {
        if (!r.managed)
            out.append(r);
    }
    return out;
}
} // namespace

void RuleController::captureSavedSnapshot()
{
    m_savedRules = m_model.rules();
}

bool RuleController::userRulesDirty() const
{
    return userSubset(m_model.rules()) != userSubset(m_savedRules);
}

void RuleController::stageUserRules(const QList<PhosphorRules::Rule>& userRules)
{
    // Replace the user subset with the profile's resolved rules, keeping any
    // managed rules the store owns. The incoming list already carries the
    // desired order; renormalizePriorities re-stamps priority from list order
    // (and pins managed rules to lowest precedence), exactly as the CRUD path.
    QSet<QUuid> managedIds;
    for (const PhosphorRules::Rule& r : m_model.rules()) {
        if (r.managed)
            managedIds.insert(r.id);
    }

    QList<PhosphorRules::Rule> next;
    next.reserve(userRules.size() + m_model.rules().size());
    for (const PhosphorRules::Rule& r : userRules) {
        // A hand-edited profile file could carry a rule whose id collides with
        // a managed rule; appending it would put two rules with the same UUID
        // in the set. Managed rules are daemon-owned, so the managed one wins.
        if (managedIds.contains(r.id)) {
            qCWarning(lcConfig) << "stageUserRules: dropping profile rule" << r.id
                                << "— its id collides with a managed rule";
            continue;
        }
        PhosphorRules::Rule copy = r;
        copy.managed = false; // defensive: a profile only ever carries user rules
        next.append(copy);
    }
    for (const PhosphorRules::Rule& r : m_model.rules()) {
        if (r.managed)
            next.append(r);
    }
    m_model.setRules(next);
    renormalizePriorities();

    // Deliberately NOT captureSavedSnapshot(): the staged rules must read dirty
    // so the Rules page badges and the global Save commits them. setRules fires
    // modelReset, which SettingsController does NOT wire to the dirty reconcile
    // (see the comment there), so emit dirtyChanged to drive
    // reconcileRuleBackedDirty and update the footer.
    Q_EMIT dirtyChanged();
}

void RuleController::resetManagedDefaults()
{
    // Fire-and-forget: no out-args, and the model refresh comes from the daemon's
    // rulesChanged broadcast + the revert() the caller issues. Ordered before any
    // subsequent getAllRules on this connection, so a paired revert() sees the
    // reset set.
    PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::Rules),
                                               QStringLiteral("resetManagedDefaults"));
}

} // namespace PlasmaZones
