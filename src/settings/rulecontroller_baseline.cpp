// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-page dirty split + global managed reset for RuleController.
// Split out of rulecontroller.cpp to keep that TU under the project's 800-line
// cap (see CLAUDE.md); the controller's class definition spans both TUs.
//
// Covers: the value-based managed/user dirty split (baselinesDirty /
// userRulesDirty against the last daemon-synced snapshot) and the
// fire-and-forget global daemon reset (resetManagedDefaults). Window appearance
// defaults moved to the config store, so the per-page appearance-baseline
// reset/discard that used to live here is gone — the Rules page carries only
// user-authored rules now.

#include "rulecontroller.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/Rule.h>

#include <QList>
#include <QString>

namespace PlasmaZones {

namespace {
// Partition helpers: managed rules are the appearance baselines; the rest are
// user rules. Order is preserved so the user-rules comparison catches reorders.
QList<PhosphorRules::Rule> managedSubset(const QList<PhosphorRules::Rule>& rules)
{
    QList<PhosphorRules::Rule> out;
    for (const PhosphorRules::Rule& r : rules) {
        if (r.managed)
            out.append(r);
    }
    return out;
}
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

bool RuleController::baselinesDirty() const
{
    return managedSubset(m_model.rules()) != managedSubset(m_savedRules);
}

bool RuleController::userRulesDirty() const
{
    return userSubset(m_model.rules()) != userSubset(m_savedRules);
}

void RuleController::recomputeDirtyFromSnapshot()
{
    setDirty(baselinesDirty() || userRulesDirty());
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
