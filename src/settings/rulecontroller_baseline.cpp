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

#include "../config/configdefaults.h"
#include "../core/baselinerules.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/Rule.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QUuid>

namespace PlasmaZones {

namespace {
// The managed baselines split by owning page: the three appearance baselines
// (Window Appearance page) and the overlay baseline (Overlay Appearance page).
// Kept separate so each page's dirty / reset / discard covers ONLY its own
// baseline(s); a shared "all managed" bit would cross-attribute overlay edits to
// the Window Appearance page.
const QSet<QUuid>& appearanceBaselineIds()
{
    static const QSet<QUuid> ids{ConfigDefaults::baselineBorderRuleId(), ConfigDefaults::baselineTitleBarRuleId(),
                                 ConfigDefaults::baselineGapRuleId()};
    return ids;
}
const QSet<QUuid>& overlayBaselineIds()
{
    static const QSet<QUuid> ids{ConfigDefaults::baselineOverlayRuleId()};
    return ids;
}
const QSet<QUuid>& generalMinSizeBaselineIds()
{
    static const QSet<QUuid> ids{ConfigDefaults::generalMinWidthRuleId(), ConfigDefaults::generalMinHeightRuleId()};
    return ids;
}
// Rules whose id is in @p ids, order preserved.
QList<PhosphorRules::Rule> subsetByIds(const QList<PhosphorRules::Rule>& rules, const QSet<QUuid>& ids)
{
    QList<PhosphorRules::Rule> out;
    for (const PhosphorRules::Rule& r : rules) {
        if (ids.contains(r.id))
            out.append(r);
    }
    return out;
}
// User rules: every non-managed rule (order preserved so a reorder is caught).
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
    return subsetByIds(m_model.rules(), appearanceBaselineIds()) != subsetByIds(m_savedRules, appearanceBaselineIds());
}

bool RuleController::overlayBaselineDirty() const
{
    return subsetByIds(m_model.rules(), overlayBaselineIds()) != subsetByIds(m_savedRules, overlayBaselineIds());
}

bool RuleController::generalMinSizeBaselineDirty() const
{
    return subsetByIds(m_model.rules(), generalMinSizeBaselineIds())
        != subsetByIds(m_savedRules, generalMinSizeBaselineIds());
}

bool RuleController::userRulesDirty() const
{
    return userSubset(m_model.rules()) != userSubset(m_savedRules);
}

void RuleController::recomputeDirtyFromSnapshot()
{
    setDirty(baselinesDirty() || overlayBaselineDirty() || generalMinSizeBaselineDirty() || userRulesDirty());
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

void RuleController::resetOverlayBaseline()
{
    // Rewrite the managed overlay baseline to its factory definition (staged).
    upsertRule(makeBaselineOverlayRule());
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

void RuleController::resetGeneralMinSizeBaseline()
{
    // Rewrite both managed general min-size baselines to their factory definitions
    // (on-by-default Width/Height thresholds). Staged.
    upsertRule(makeBaselineGeneralMinWidthRule());
    upsertRule(makeBaselineGeneralMinHeightRule());
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
    // Restore the three APPEARANCE baselines from the last synced snapshot,
    // leaving user rules and the overlay baseline untouched.
    const QList<PhosphorRules::Rule> savedBaselines = subsetByIds(m_savedRules, appearanceBaselineIds());
    for (const PhosphorRules::Rule& saved : savedBaselines)
        upsertRule(saved);
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

void RuleController::discardOverlayBaseline()
{
    // Restore the overlay baseline from the last synced snapshot, leaving the
    // appearance baselines and user rules untouched.
    const QList<PhosphorRules::Rule> saved = subsetByIds(m_savedRules, overlayBaselineIds());
    for (const PhosphorRules::Rule& r : saved)
        upsertRule(r);
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

void RuleController::discardGeneralMinSizeBaseline()
{
    // Restore both general min-size baselines from the last synced snapshot, leaving
    // the appearance / overlay baselines and user rules untouched.
    const QList<PhosphorRules::Rule> saved = subsetByIds(m_savedRules, generalMinSizeBaselineIds());
    for (const PhosphorRules::Rule& r : saved)
        upsertRule(r);
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

} // namespace PlasmaZones
