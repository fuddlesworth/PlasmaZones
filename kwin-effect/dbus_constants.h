// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1StringView>

namespace PlasmaZones {

/**
 * @brief D-Bus service constants for KWin effect
 *
 * Centralized D-Bus interface names to avoid magic strings
 * throughout the effect code.
 */
namespace DBus {
inline constexpr QLatin1StringView ServiceName("org.plasmazones");
inline constexpr QLatin1StringView ObjectPath("/PlasmaZones");

namespace Interface {
inline constexpr QLatin1StringView Settings("org.plasmazones.Settings");
inline constexpr QLatin1StringView WindowDrag("org.plasmazones.WindowDrag");
inline constexpr QLatin1StringView WindowTracking("org.plasmazones.WindowTracking");
inline constexpr QLatin1StringView Overlay("org.plasmazones.Overlay");
inline constexpr QLatin1StringView Autotile("org.plasmazones.Autotile");
inline constexpr QLatin1StringView LayoutManager("org.plasmazones.LayoutManager");
inline constexpr QLatin1StringView Screen("org.plasmazones.Screen");
inline constexpr QLatin1StringView ZoneDetection("org.plasmazones.ZoneDetection");
}
}

} // namespace PlasmaZones
