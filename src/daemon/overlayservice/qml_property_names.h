// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1String>

/**
 * @file qml_property_names.h
 * @brief Centralised QML dynamic-property name constants for the daemon
 *        <-> overlay QML wiring.
 *
 * The daemon writes per-overlay state to QML windows via
 * `writeQmlProperty(window, "<name>", value)` and the QML side mirrors
 * those names in property bindings. C++ readers (settings hot-reload,
 * shader filtering, idle handling, etc.) ALSO look these up by string
 * via `window->property("<name>")`. A typo on either side silently
 * fails — the QML side gets a dynamic null property, the C++ side reads
 * the default-constructed QVariant — with no compile-time check.
 *
 * Hosting them here gives both sides a single source of truth: a typo
 * in either the writer or a reader trips the build (or at least a
 * failing test) instead of producing a runtime no-op.
 */
namespace PlasmaZones {
namespace OverlayQmlPropertyNames {

/// Marks a window as a shader-effect overlay rather than a plain
/// drawing overlay. Set on overlay creation; read by the shader-
/// settings hot-reload path to know whether to apply shader-only
/// settings, and by overlay teardown / shader-toggle to recreate the
/// window when the user flips the shader-enabled global.
inline constexpr QLatin1String IsShaderOverlay{"isShaderOverlay"};

/// True when the overlay should be hidden during a drag-pause
/// (autotile drag-from-floating sequence). The QML root toggles
/// opacity / visibility off this property.
inline constexpr QLatin1String Idled{"_idled"};

/// Per-overlay audio spectrum bins (QVariantList of floats in [0,1]).
/// Pushed every 16 ms while audio-reactive shaders are active; QML
/// shader bindings forward to the `iAudioSpectrum` UBO field.
inline constexpr QLatin1String AudioSpectrum{"audioSpectrum"};

/// Daemon-internal marker (leading underscore): true when a decoration
/// slot's current chain carries an audio-reactive surface pack. Set by
/// applyDecoration via setProperty and read by visibleAudioDecorationSlots
/// + syncCavaState to gate CAVA and the audio-spectrum push. Not a QML
/// binding — purely a C++-side flag on the slot QObject.
inline constexpr QLatin1String WantsAudioDecoration{"_wantsAudioDecoration"};

/// Per-overlay zone-rectangle list as QVariantList of QVariantMap
/// snapshots (see ZoneSnapshotKeys in zoneshaderitem.cpp for the
/// per-zone payload format).
inline constexpr QLatin1String Zones{"zones"};

/// Total zone count for the current layout. Mirrors the size of the
/// Zones list so the QML side can avoid recomputing.
inline constexpr QLatin1String ZoneCount{"zoneCount"};

/// Number of highlighted zones in the current snapshot. Drives a
/// "any zone selected" gate in QML for global glow / sound cues.
inline constexpr QLatin1String HighlightedCount{"highlightedCount"};

/// QUuid string of the single hovered zone, or empty when no zone
/// is hovered. Read by per-zone hover-only update path (single-zone
/// optimisation when only the hover state changed).
inline constexpr QLatin1String HighlightedZoneId{"highlightedZoneId"};

/// QVariantList of zone-id strings for batch highlight scenarios
/// (e.g. zone-selector multi-select preview).
inline constexpr QLatin1String HighlightedZoneIds{"highlightedZoneIds"};

/// Sparse ZoneLabelTexture (glyph-tile) payload for the rendered zone labels.
/// Bound to the shader's `iLabelsTexture` sampler. Redundant-rebuild dedupe is
/// done C++-side via PerScreenOverlayState::labelsTextureHash, not on the QML side.
inline constexpr QLatin1String LabelsTexture{"labelsTexture"};

} // namespace OverlayQmlPropertyNames
} // namespace PlasmaZones
