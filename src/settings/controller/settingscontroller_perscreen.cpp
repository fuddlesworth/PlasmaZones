// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-screen override accessors for SettingsController: the autotile,
// scrolling, zone-selector and strip-selector maps plus the gap-override pair. The actual
// storage lives in Settings; this file's Q_INVOKABLE wrappers thin-forward to
// it. There is no snapping wrapper here — that one is a Q_INVOKABLE on
// Settings itself, which QML reaches directly.
// Dirty tracking and the perScreenOverridesChanged() refresh are NOT done
// here: Settings emits perScreen{Autotile,Scrolling,ZoneSelector,ScrollingZoneSelector}SettingsChanged
// only when an override actually changes, and the controller constructor
// wires those signals to onValueBlindSettingsChanged() (dirty, with the
// value-blind latch — per-screen values are manifest-invisible, so the
// value-based reconcile is suspended rather than run) and
// perScreenOverridesChanged() (UI refresh). Emitting from these wrappers
// would mark the page dirty even for no-op or rejected writes.
//
// All methods here are members of PlasmaZones::SettingsController and use its
// private state. Same class, separate translation unit, no API change.

#include "settingscontroller.h"

#include "config/settings.h"

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

bool SettingsController::hasPerScreenAutotileAlgorithmSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenAutotileAlgorithmSettings(screenName);
}

void SettingsController::clearPerScreenAutotileAlgorithmSettings(const QString& screenName)
{
    m_settings.clearPerScreenAutotileAlgorithmSettings(screenName);
}

// ── Per-screen scrolling overrides ───────────────────────────────────────

QVariantMap SettingsController::getPerScreenScrollingSettings(const QString& screenName) const
{
    return m_settings.getPerScreenScrollingSettings(screenName);
}

void SettingsController::setPerScreenScrollingSetting(const QString& screenName, const QString& key,
                                                      const QVariant& value)
{
    m_settings.setPerScreenScrollingSetting(screenName, key, value);
}

void SettingsController::clearPerScreenScrollingSettings(const QString& screenName)
{
    m_settings.clearPerScreenScrollingSettings(screenName);
}

bool SettingsController::hasPerScreenScrollingSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenScrollingSettings(screenName);
}

// ── Per-screen gap overrides (config-backed) ─────────────────────────────
// A per-monitor gap override is the gap-dimension sub-domain of the per-screen
// autotile config store (unified — one value per monitor drives both snap and
// tile). The Gaps card's monitor scope chip drives has/clear through these; the
// gap controls themselves read/write via the WindowAppearanceController's
// gapValue/writeGap invokables.

bool SettingsController::hasPerScreenGapOverride(const QString& screenName) const
{
    return m_settings.hasPerScreenGapOverride(screenName);
}

void SettingsController::clearPerScreenGapOverride(const QString& screenName)
{
    // Settings::clearPerScreenGapOverride emits perScreenAutotileSettingsChanged →
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

bool SettingsController::hasPerScreenZoneSelectorPositionSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorPositionSettings(screenName);
}

void SettingsController::clearPerScreenZoneSelectorPositionSettings(const QString& screenName)
{
    m_settings.clearPerScreenZoneSelectorPositionSettings(screenName);
}

bool SettingsController::hasPerScreenZoneSelectorArrangementSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorArrangementSettings(screenName);
}

void SettingsController::clearPerScreenZoneSelectorArrangementSettings(const QString& screenName)
{
    m_settings.clearPerScreenZoneSelectorArrangementSettings(screenName);
}

bool SettingsController::hasPerScreenZoneSelectorSizeSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorSizeSettings(screenName);
}

void SettingsController::clearPerScreenZoneSelectorSizeSettings(const QString& screenName)
{
    m_settings.clearPerScreenZoneSelectorSizeSettings(screenName);
}

// ── Per-screen strip selector overrides ──────────────────────────────────

QVariantMap SettingsController::getPerScreenScrollingZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.getPerScreenScrollingZoneSelectorSettings(screenName);
}

void SettingsController::setPerScreenScrollingZoneSelectorSetting(const QString& screenName, const QString& key,
                                                                  const QVariant& value)
{
    m_settings.setPerScreenScrollingZoneSelectorSetting(screenName, key, value);
}

void SettingsController::clearPerScreenScrollingZoneSelectorSettings(const QString& screenName)
{
    m_settings.clearPerScreenScrollingZoneSelectorSettings(screenName);
}

bool SettingsController::hasPerScreenScrollingZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenScrollingZoneSelectorSettings(screenName);
}

bool SettingsController::hasPerScreenScrollingZoneSelectorPositionSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenScrollingZoneSelectorPositionSettings(screenName);
}

void SettingsController::clearPerScreenScrollingZoneSelectorPositionSettings(const QString& screenName)
{
    m_settings.clearPerScreenScrollingZoneSelectorPositionSettings(screenName);
}

bool SettingsController::hasPerScreenScrollingZoneSelectorSizeSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenScrollingZoneSelectorSizeSettings(screenName);
}

void SettingsController::clearPerScreenScrollingZoneSelectorSizeSettings(const QString& screenName)
{
    m_settings.clearPerScreenScrollingZoneSelectorSizeSettings(screenName);
}

} // namespace PlasmaZones
