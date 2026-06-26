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

// Snapping per-screen gaps are rule-backed (per-monitor gap WindowRules) and
// edited through the Window Appearance page's Gaps card, so there is no
// per-screen snapping reader or writer surface on the controller.

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
