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
#include "windowrulecontroller.h"

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
    return m_settings.hasPerScreenAutotileAlgorithmSettings(screenName);
}

void SettingsController::clearPerScreenAutotileAlgorithmSettings(const QString& screenName)
{
    m_settings.clearPerScreenAutotileAlgorithmSettings(screenName);
}

// ── Per-screen gap overrides (rule-backed) ───────────────────────────────
// A per-monitor gap override is a screen-scoped gap WindowRule whose id is
// derived deterministically from the baseline appearance rule + the screen
// name (matching WindowAppearanceController::perScreenGapRuleId). The Gaps
// card's monitor scope chip drives has/clear through these; the gap controls
// themselves read/write the rule's actions via windowRulesPage. Mirrors the
// autotile per-screen has/clear shape so the shared MonitorScopeChip can drive
// it uniformly.

bool SettingsController::hasPerScreenGapRule(const QString& screenName) const
{
    if (screenName.isEmpty() || m_windowRulesPage == nullptr) {
        return false;
    }
    const QString id = QUuid::createUuidV5(ConfigDefaults::baselineAppearanceRuleId(), screenName.toUtf8()).toString();
    return !m_windowRulesPage->ruleJson(id).isEmpty();
}

void SettingsController::clearPerScreenGapRule(const QString& screenName)
{
    if (screenName.isEmpty() || m_windowRulesPage == nullptr) {
        return;
    }
    const QString id = QUuid::createUuidV5(ConfigDefaults::baselineAppearanceRuleId(), screenName.toUtf8()).toString();
    if (m_windowRulesPage->removeRule(id)) {
        Q_EMIT perScreenOverridesChanged();
    }
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
    m_settings.clearPerScreenZoneSelectorSettings(screenName);
}

bool SettingsController::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorSettings(screenName);
}

} // namespace PlasmaZones
