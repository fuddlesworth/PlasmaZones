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

#include "../config/settings.h"

#include <PhosphorIdentity/VirtualScreenId.h>

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

// ── Per-screen gap overrides (config-backed) ─────────────────────────────
// A per-monitor gap override is the gap-dimension sub-domain of the per-screen
// autotile config store (unified — one value per monitor drives both snap and
// tile). The Gaps card's monitor scope chip drives has/clear through these; the
// gap controls themselves read/write via the WindowAppearanceController's
// gapValue/writeGap invokables. The Q_INVOKABLE names keep the "…GapRule"
// spelling the QML scope chip already binds — they are config-backed now.

bool SettingsController::hasPerScreenGapRule(const QString& screenName) const
{
    return m_settings.hasPerScreenGapOverride(screenName);
}

void SettingsController::clearPerScreenGapRule(const QString& screenName)
{
    // clearPerScreenGapOverride emits perScreenAutotileSettingsChanged →
    // perScreenOverridesChanged (wired in settingscontroller.cpp), so no manual
    // emit is needed here.
    m_settings.clearPerScreenGapOverride(screenName);
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
