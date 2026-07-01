// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-screen override accessors for SettingsController. The actual storage
// lives in Settings; this file's Q_INVOKABLE wrappers thin-forward to it.
// Dirty tracking and the perScreenOverridesChanged() refresh are NOT done
// here: Settings emits perScreen{Autotile,Snapping,ZoneSelector}SettingsChanged
// only when an override actually changes, and the controller constructor
// wires those signals to onSettingsPropertyChanged() (dirty) and
// perScreenOverridesChanged() (UI refresh). Emitting from these wrappers
// would mark the page dirty even for no-op or rejected writes.
//
// Split out of settingscontroller_session.cpp to keep that file under the
// 800-line cap (see CLAUDE.md). All methods here are members of
// PlasmaZones::SettingsController and use its private state — same class,
// separate translation unit, no API change.

#include "settingscontroller.h"

#include "../config/configdefaults.h"
#include "rulecontroller.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QUuid>

namespace PlasmaZones {

// ── Per-monitor editing scope ─────────────────────────────────────────────

void SettingsController::setScopeScreenName(const QString& name)
{
    if (m_scopeScreenName == name)
        return;
    m_scopeScreenName = name;
    Q_EMIT scopeScreenNameChanged();
}

QString SettingsController::physicalScreenId(const QString& name) const
{
    return PhosphorIdentity::VirtualScreenId::extractPhysicalId(name);
}

// ── Per-screen autotile overrides ────────────────────────────────────────

QVariantMap SettingsController::getPerScreenAutotileSettings(const QString& screenName) const
{
    return m_settings.getPerScreenAutotileSettings(screenName);
}

void SettingsController::setPerScreenAutotileSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value)
{
    m_settings.setPerScreenAutotileSetting(screenName, key, value);
}

void SettingsController::clearPerScreenAutotileSettings(const QString& screenName)
{
    m_settings.clearPerScreenAutotileSettings(screenName);
}

bool SettingsController::hasPerScreenAutotileSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenAutotileSettings(screenName);
}

bool SettingsController::hasPerScreenAutotileGapsSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenAutotileGapsSettings(screenName);
}

void SettingsController::clearPerScreenAutotileGapsSettings(const QString& screenName)
{
    m_settings.clearPerScreenAutotileGapsSettings(screenName);
}

bool SettingsController::hasPerScreenAutotileAlgorithmSettings(const QString& screenName) const
{
    // The algorithm card spans the config-backed sub-domain (algorithm token,
    // behavioural flags) AND the rule-backed tiling geometry (split/master/max),
    // so its scope chip reports an override when EITHER carries one.
    return m_settings.hasPerScreenAutotileAlgorithmSettings(screenName) || hasPerScreenTilingRule(screenName);
}

void SettingsController::clearPerScreenAutotileAlgorithmSettings(const QString& screenName)
{
    // Reset the whole card: clear the config sub-domain AND remove the per-screen
    // tiling geometry rule.
    m_settings.clearPerScreenAutotileAlgorithmSettings(screenName);
    clearPerScreenTilingRule(screenName);
}

// ── Per-screen gap overrides (rule-backed) ───────────────────────────────
// A per-monitor gap override is a screen-scoped gap Rule whose id is
// derived deterministically from the baseline gap rule + the screen's STABLE
// EDID id (matching WindowAppearanceController::perScreenGapRuleId, the v4→v5
// migration, and Settings::perScreenGapRuleOverrides, all of which key by the
// canonical stable form). The Gaps card's monitor scope chip drives has/clear
// through these; the gap controls themselves read/write the rule's actions via
// rulesPage. Mirrors the autotile per-screen has/clear shape so the shared
// MonitorScopeChip can drive it uniformly.

bool SettingsController::hasPerScreenGapRule(const QString& screenName) const
{
    if (screenName.isEmpty() || m_rulesPage == nullptr || m_windowAppearancePage == nullptr) {
        return false;
    }
    // Single source of truth for the per-monitor gap rule id — delegate to the
    // controller that authors it rather than re-deriving the UUID here.
    const QString id = m_windowAppearancePage->perScreenGapRuleId(screenName);
    return !id.isEmpty() && !m_rulesPage->ruleJson(id).isEmpty();
}

void SettingsController::clearPerScreenGapRule(const QString& screenName)
{
    if (screenName.isEmpty() || m_rulesPage == nullptr || m_windowAppearancePage == nullptr) {
        return;
    }
    const QString id = m_windowAppearancePage->perScreenGapRuleId(screenName);
    if (id.isEmpty()) {
        return;
    }
    // removeRule drives RuleModel::countChanged → perScreenOverridesChanged
    // (wired in settingscontroller.cpp), so no manual emit is needed here.
    m_rulesPage->removeRule(id);
}

// ── Per-screen tiling geometry overrides (rule-backed) ───────────────────
// The split/master/max per-monitor override is a screen-scoped Rule whose id is
// derived from perScreenTilingRuleNamespaceId + the screen's STABLE EDID id (the
// same derivation Settings::perScreenTilingRuleOverrides and the v4→v5 migration
// use). The Tiling Algorithm card's controls find-or-create the rule's actions via
// rulesPage; its scope chip has/clear routes through hasPerScreenAutotileAlgorithm-
// Settings / clearPerScreenAutotileAlgorithmSettings above.

QString SettingsController::perScreenTilingRuleId(const QString& screenName) const
{
    if (screenName.isEmpty()) {
        return QString();
    }
    return QUuid::createUuidV5(ConfigDefaults::perScreenTilingRuleNamespaceId(),
                               Settings::canonicalPerScreenKey(screenName).toUtf8())
        .toString();
}

bool SettingsController::hasPerScreenTilingRule(const QString& screenName) const
{
    if (screenName.isEmpty() || m_rulesPage == nullptr) {
        return false;
    }
    const QString id = perScreenTilingRuleId(screenName);
    return !id.isEmpty() && !m_rulesPage->ruleJson(id).isEmpty();
}

void SettingsController::clearPerScreenTilingRule(const QString& screenName)
{
    if (screenName.isEmpty() || m_rulesPage == nullptr) {
        return;
    }
    const QString id = perScreenTilingRuleId(screenName);
    if (!id.isEmpty()) {
        m_rulesPage->removeRule(id);
    }
}

QString SettingsController::canonicalScreenId(const QString& screenName) const
{
    return Settings::canonicalPerScreenKey(screenName);
}

QString SettingsController::animationMinWidthRuleId() const
{
    return ConfigDefaults::animationMinWidthRuleId().toString();
}

QString SettingsController::animationMinHeightRuleId() const
{
    return ConfigDefaults::animationMinHeightRuleId().toString();
}

// ── Per-screen zone selector overrides ───────────────────────────────────

QVariantMap SettingsController::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.getPerScreenZoneSelectorSettings(screenName);
}

void SettingsController::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key,
                                                         const QVariant& value)
{
    m_settings.setPerScreenZoneSelectorSetting(screenName, key, value);
}

void SettingsController::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    // The per-monitor store is retired in favour of the rule; clear any residual
    // config value AND remove the rule so the scope chip's reset covers both.
    m_settings.clearPerScreenZoneSelectorSettings(screenName);
    clearPerScreenZoneSelectorRule(screenName);
}

bool SettingsController::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorSettings(screenName) || hasPerScreenZoneSelectorRule(screenName);
}

// ── Per-screen zone selector override rule (rule-backed) ──────────────────
// The whole per-monitor zone-selector store folds onto a screen-scoped Rule
// carrying generic SetZoneSelectorProperty actions (one per overridden property),
// keyed by perScreenZoneSelectorRuleNamespaceId + the screen's stable EDID id.

QString SettingsController::perScreenZoneSelectorRuleId(const QString& screenName) const
{
    if (screenName.isEmpty()) {
        return QString();
    }
    return QUuid::createUuidV5(ConfigDefaults::perScreenZoneSelectorRuleNamespaceId(),
                               Settings::canonicalPerScreenKey(screenName).toUtf8())
        .toString();
}

bool SettingsController::hasPerScreenZoneSelectorRule(const QString& screenName) const
{
    if (screenName.isEmpty() || m_rulesPage == nullptr) {
        return false;
    }
    const QString id = perScreenZoneSelectorRuleId(screenName);
    return !id.isEmpty() && !m_rulesPage->ruleJson(id).isEmpty();
}

void SettingsController::clearPerScreenZoneSelectorRule(const QString& screenName)
{
    if (screenName.isEmpty() || m_rulesPage == nullptr) {
        return;
    }
    const QString id = perScreenZoneSelectorRuleId(screenName);
    if (!id.isEmpty()) {
        m_rulesPage->removeRule(id);
    }
}

} // namespace PlasmaZones
