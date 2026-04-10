// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1String>

namespace PlasmaZones {

/**
 * @brief D-Bus service constants for KWin effect
 *
 * Centralized D-Bus interface names to avoid magic strings
 * throughout the effect code.
 */
namespace DBus {
inline constexpr QLatin1String ServiceName("org.plasmazones");
inline constexpr QLatin1String ObjectPath("/PlasmaZones");

namespace Interface {
inline constexpr QLatin1String Settings("org.plasmazones.Settings");
inline constexpr QLatin1String WindowDrag("org.plasmazones.WindowDrag");
inline constexpr QLatin1String WindowTracking("org.plasmazones.WindowTracking");
inline constexpr QLatin1String Overlay("org.plasmazones.Overlay");
inline constexpr QLatin1String Autotile("org.plasmazones.Autotile");
inline constexpr QLatin1String LayoutManager("org.plasmazones.LayoutManager");
inline constexpr QLatin1String Screen("org.plasmazones.Screen");
inline constexpr QLatin1String ZoneDetection("org.plasmazones.ZoneDetection");
}
}

} // namespace PlasmaZones
