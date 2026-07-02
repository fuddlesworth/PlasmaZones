// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-page dirty split + managed-baseline reset/discard for RuleController.
// Split out of rulecontroller.cpp to keep that TU under the project's 800-line
// cap (see CLAUDE.md); the controller's class definition spans both TUs.
//
// Covers: the value-based per-group dirty split against the last daemon-synced
// snapshot (baselinesDirty / overlayBaselineDirty / generalMinSizeBaselineDirty
// / animationMinSizeBaselineDirty / userRulesDirty), the staged per-page
// reset/discard for each managed-baseline group (appearance, overlay, general
// min-size, animation min-size), and the fire-and-forget global daemon reset
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
// (Window Appearance page), the overlay baseline (Overlay Appearance page),
// the general min-size baselines (General page), and the animation min-size
// baselines (Animations → General page). Kept separate so each page's dirty /
// reset / discard covers ONLY its own baseline(s); a shared "all managed" bit
// would cross-attribute overlay edits to the Window Appearance page.
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
const QSet<QUuid>& animationMinSizeBaselineIds()
{
    static const QSet<QUuid> ids{ConfigDefaults::animationMinWidthRuleId(), ConfigDefaults::animationMinHeightRuleId()};
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

bool RuleController::animationMinSizeBaselineDirty() const
{
    return subsetByIds(m_model.rules(), animationMinSizeBaselineIds())
        != subsetByIds(m_savedRules, animationMinSizeBaselineIds());
}

bool RuleController::userRulesDirty() const
{
    return userSubset(m_model.rules()) != userSubset(m_savedRules);
}

void RuleController::recomputeDirtyFromSnapshot()
{
    setDirty(baselinesDirty() || overlayBaselineDirty() || generalMinSizeBaselineDirty()
             || animationMinSizeBaselineDirty() || userRulesDirty());
}

void RuleController::upsertRule(const PhosphorRules::Rule& rule)
{
    if (m_model.contains(rule.id))
        m_model.updateRule(rule);
    else
        m_model.addRule(rule);
}

// The shared tail of every per-group baseline reset/discard: upsert-all +
// recompute + baselinesChanged. Only the rule list differs (factory
// definitions for a reset, the saved-snapshot subset for a discard). The
// public per-group methods below stay as the named API surface — each is one
// call into this helper — so the page wiring reads by intent while the
// mechanics live once.
void RuleController::applyBaselineGroup(const QList<PhosphorRules::Rule>& rules)
{
    for (const PhosphorRules::Rule& r : rules)
        upsertRule(r);
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

void RuleController::resetBaselines()
{
    // Rewrite the three managed appearance baselines to their factory
    // definitions. Managed rules stay in the System section, so updateRule
    // replaces them in place (no priority renormalize). Staged — the global
    // Save flushes it.
    applyBaselineGroup({makeBaselineBorderRule(), makeBaselineTitleBarRule(), makeBaselineGapRule()});
}

void RuleController::resetOverlayBaseline()
{
    // Rewrite the managed overlay baseline to its factory definition (staged).
    applyBaselineGroup({makeBaselineOverlayRule()});
}

void RuleController::resetGeneralMinSizeBaseline()
{
    // Rewrite both managed general min-size baselines to their factory definitions
    // (on-by-default Width/Height thresholds). Staged.
    applyBaselineGroup({makeBaselineGeneralMinWidthRule(), makeBaselineGeneralMinHeightRule()});
}

void RuleController::resetAnimationMinSizeBaseline()
{
    // Rewrite both managed animation min-size baselines to their factory
    // definitions (off-by-default 0 thresholds). Staged.
    applyBaselineGroup({makeBaselineAnimationMinWidthRule(), makeBaselineAnimationMinHeightRule()});
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
    // leaving user rules and the other baseline groups untouched.
    applyBaselineGroup(subsetByIds(m_savedRules, appearanceBaselineIds()));
}

void RuleController::discardOverlayBaseline()
{
    // Restore the overlay baseline from the last synced snapshot, leaving the
    // other baseline groups and user rules untouched.
    applyBaselineGroup(subsetByIds(m_savedRules, overlayBaselineIds()));
}

void RuleController::discardGeneralMinSizeBaseline()
{
    // Restore both general min-size baselines from the last synced snapshot,
    // leaving the other baseline groups and user rules untouched.
    applyBaselineGroup(subsetByIds(m_savedRules, generalMinSizeBaselineIds()));
}

void RuleController::discardAnimationMinSizeBaseline()
{
    // Restore both animation min-size baselines from the last synced snapshot,
    // leaving the other baseline groups and user rules untouched.
    applyBaselineGroup(subsetByIds(m_savedRules, animationMinSizeBaselineIds()));
}

} // namespace PlasmaZones
