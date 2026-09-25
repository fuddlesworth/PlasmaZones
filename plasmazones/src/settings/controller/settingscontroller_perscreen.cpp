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

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/interfaces/settings_interfaces.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QScreen>

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

void SettingsController::clearPerScreenScrollingSizingSettings(const QString& screenName)
{
    m_settings.clearPerScreenScrollingSizingSettings(screenName);
}

bool SettingsController::hasPerScreenScrollingSizingSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenScrollingSizingSettings(screenName);
}

void SettingsController::clearPerScreenScrollingAxisSettings(const QString& screenName)
{
    m_settings.clearPerScreenScrollingAxisSettings(screenName);
}

bool SettingsController::hasPerScreenScrollingAxisSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenScrollingAxisSettings(screenName);
}

bool SettingsController::scrollingStripVerticalForScreen(const QString& screenName) const
{
    // The SETTINGS ladder the engine's seed walks (per-screen override, then
    // the global value, then the Auto rule). Two documented approximations
    // keep this a preview rather than the engine's own answer: the Auto arm
    // measures the screen rect rather than the gap-adjusted work area (the
    // settings app does not resolve per-screen gaps, and the two disagree
    // only when reserved panels are deep enough to flip the longer side),
    // and the SetScrollStripAxis RULE tier is not consulted at all — this
    // process has no rule-resolution seam, so a rule-flipped axis previews
    // the settings answer. EditorController's template preview shares both
    // limits.
    //
    // ONE store read: getPerScreenScrollingSettings itself resolves every
    // config spelling (id and connector, any /vs:N suffix preserved) through
    // perScreenKeyVariants, so a spellings loop here re-asked the same two.
    // The virtual→physical retry mirrors the daemon's seed
    // (Daemon::updateScrollingScreens) byte for byte: a virtual sub-screen
    // with NO map of its own inherits its physical parent's wholesale.
    QVariantMap overrides = m_settings.getPerScreenScrollingSettings(screenName);
    if (overrides.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenName)) {
        overrides =
            m_settings.getPerScreenScrollingSettings(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenName));
    }
    const QVariant axisOverride = overrides.value(QLatin1String(PerScreenScrollingKey::StripAxis));
    const int configured = axisOverride.isValid() ? axisOverride.toInt() : m_settings.scrollingStripAxis();

    if (configured == ConfigDefaults::scrollingStripAxisVertical())
        return true;
    if (configured == ConfigDefaults::scrollingStripAxisHorizontal())
        return false;

    // Auto, and any out-of-range value a hand-edited config could carry.
    QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenName);
    if (!screen)
        return false;
    const QSize size = screen->geometry().size();
    return PhosphorProtocol::autoScrollAxisFor(size.width(), size.height()) == PhosphorProtocol::ScrollAxis::Vertical;
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
