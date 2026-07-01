// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-page dirty split + managed-baseline reset/discard for RuleController.
// Split out of rulecontroller.cpp to keep that TU under the project's 800-line
// cap (see CLAUDE.md); the controller's class definition spans both TUs.
//
// Covers: the value-based baseline/user dirty split (baselinesDirty /
// userRulesDirty against the last daemon-synced snapshot), the staged per-page
// reset/discard of the three managed appearance baselines (resetBaselines /
// discardBaselineEdits), and the fire-and-forget global daemon reset
// (resetManagedDefaults).

#include "rulecontroller.h"

#include "../core/baselinerules.h"

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

void RuleController::upsertRule(const PhosphorRules::Rule& rule)
{
    if (m_model.contains(rule.id))
        m_model.updateRule(rule);
    else
        m_model.addRule(rule);
}

void RuleController::resetBaselines()
{
    // Rewrite the three managed baselines to their factory definitions. Managed
    // rules stay in the System section, so updateRule replaces them in place
    // (no priority renormalize). Staged — the global Save flushes it.
    upsertRule(makeBaselineBorderRule());
    upsertRule(makeBaselineTitleBarRule());
    upsertRule(makeBaselineGapRule());
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
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

void RuleController::discardBaselineEdits()
{
    // Restore each managed baseline from the last synced snapshot, leaving every
    // user rule untouched.
    const QList<PhosphorRules::Rule> savedBaselines = managedSubset(m_savedRules);
    for (const PhosphorRules::Rule& saved : savedBaselines)
        upsertRule(saved);
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

} // namespace PlasmaZones
