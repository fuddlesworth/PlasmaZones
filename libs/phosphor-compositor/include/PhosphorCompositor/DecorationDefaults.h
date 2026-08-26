// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>
#include <QtGlobal>

namespace PhosphorCompositor {

/**
 * @brief Single source of truth for window-decoration appearance defaults.
 *
 * Consumed by BOTH sides of the D-Bus boundary: the daemon's ConfigDefaults
 * (persisted setting defaults) and the compositor plugin's BorderState
 * (pre-settings-load rendering state). Sharing the symbols makes drift
 * between the two structurally impossible — the effect renders with the
 * same values the daemon would persist until the async settings load lands.
 *
 * These are WINDOW decoration constants (tiled/snapped window borders and
 * title bars), not the zone-overlay border constants in ZoneDefaults.
 */
namespace DecorationDefaults {

inline constexpr bool HideTitleBars = false;
inline constexpr bool ShowBorder = false;

inline constexpr int BorderWidth = 2;
inline constexpr int BorderWidthMin = 0;
inline constexpr int BorderWidthMax = 10;

inline constexpr int BorderRadius = 8;
inline constexpr int BorderRadiusMin = 0;
inline constexpr int BorderRadiusMax = 20;

// How long uSurfaceFocused ramps between 0 and 1 when a window gains or
// loses focus (ms) — the cross-fade every decoration pack that mixes by
// focus (focus-fade wash, border dims, glow, shadow) renders with. A
// standalone decoration setting, deliberately independent of the window
// animation system: 0 means switch instantly.
inline constexpr int FocusFadeMs = 160;
inline constexpr int FocusFadeMsMin = 0;
inline constexpr int FocusFadeMsMax = 1000;

// Multiplier on the bufferScale a surface pack's metadata declares for its
// buffer passes (the blur pyramid density). The effect folds it into every
// consumer of a pack's metadata scale and clamps the PRODUCT into the
// allocator band, so these bounds describe the persisted setting, not the
// final texture size. Shared here so the effect's pre-settings-load seed and
// its D-Bus boundary clamp cannot drift from the daemon's schema clamp.
inline constexpr qreal BlurScaleMultiplier = 1.0;
inline constexpr qreal BlurScaleMultiplierMin = 0.25;
inline constexpr qreal BlurScaleMultiplierMax = 2.0;

// A retuned default outside its own declared slider range would clamp
// differently on every consumer — make that a compile error instead.
static_assert(BorderWidth >= BorderWidthMin && BorderWidth <= BorderWidthMax,
              "BorderWidth default must lie within its declared bounds");
static_assert(BorderRadius >= BorderRadiusMin && BorderRadius <= BorderRadiusMax,
              "BorderRadius default must lie within its declared bounds");
static_assert(FocusFadeMs >= FocusFadeMsMin && FocusFadeMs <= FocusFadeMsMax,
              "FocusFadeMs default must lie within its declared bounds");
static_assert(BlurScaleMultiplier >= BlurScaleMultiplierMin && BlurScaleMultiplier <= BlurScaleMultiplierMax,
              "BlurScaleMultiplier default must lie within its declared bounds");

// Cold-start colour fallbacks for the effect's decoration path: what a
// border/glow/shadow renders with before the daemon's first colour replies
// land (Breeze accent blue and the Breeze inactive grey, both opaque).
// Single-sourced here so the effect's call sites cannot drift apart.
inline constexpr QLatin1StringView FallbackAccentHex{"#ff3daee9"};
inline constexpr QLatin1StringView FallbackInactiveHex{"#ff5c6370"};

} // namespace DecorationDefaults

/**
 * @brief The window-appearance "Apply to" scope tokens — the single source of
 * truth shared by the config store (ConfigDefaults / the schema validator), the
 * settings page (WindowAppearanceController → QML), and the effect's scope
 * predicate. A config-default border / hidden title bar applies to a window only
 * when it matches the chosen scope:
 *   - Tiled:  the window is snapped into a zone OR autotile-managed.
 *   - Normal: its window type is Normal AND it is not transient.
 *   - All:    every window.
 * These are on-disk / wire tokens, so they must never be localized or renamed
 * without a migration.
 */
namespace WindowAppearanceScope {

inline constexpr QLatin1StringView Tiled{"tiled"};
inline constexpr QLatin1StringView Normal{"normal"};
inline constexpr QLatin1StringView All{"all"};

} // namespace WindowAppearanceScope
} // namespace PhosphorCompositor
