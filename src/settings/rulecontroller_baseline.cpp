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

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/Rule.h>

#include <QList>
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
