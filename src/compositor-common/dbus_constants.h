// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1String>

namespace PlasmaZones {

/**
 * @brief D-Bus service constants shared by all compositor plugins
 *
 * Centralized D-Bus interface names to avoid magic strings.
 * Used by KWin effect, Wayfire plugin, and any future compositor integration.
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
inline constexpr QLatin1String LayoutRegistry("org.plasmazones.LayoutRegistry");
inline constexpr QLatin1String Screen("org.plasmazones.Screen");
inline constexpr QLatin1String ZoneDetection("org.plasmazones.ZoneDetection");
inline constexpr QLatin1String CompositorBridge("org.plasmazones.CompositorBridge");
}

// Protocol version. Bumped when the D-Bus method/signal schema changes in a
// backwards-incompatible way (e.g. dragStopped out-params changed, new
// required signal). Both sides check the peer's version at bridge registration
// and reject if below their minimum. The version is a simple integer string
// ("1", "2", …) to keep comparison trivial.
//
//   v1: original protocol (PlasmaZones v3.0–v3.x)
//   v2: split dragStopped + snapAssistReady signal (Phase C)
//
inline constexpr int ApiVersion = 2;
inline constexpr int MinPeerApiVersion = 2;

// Hard cap on blocking synchronous D-Bus calls from the editor/settings
// apps to the daemon. Qt's default is 25 seconds, long enough to freeze
// the UI for tens of seconds if the daemon event loop is busy. Daemon-side
// settings/shader handlers are all in-memory hash lookups (sub-millisecond
// in the healthy case), so 500 ms is generous while still degrading
// gracefully to caller-side defaults when the daemon is unresponsive.
inline constexpr int SyncCallTimeoutMs = 500;
}

} // namespace PlasmaZones
