// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

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

} // namespace DecorationDefaults
} // namespace PhosphorCompositor
