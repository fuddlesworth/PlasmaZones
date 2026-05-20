// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>

namespace PhosphorProtocol::Service {

/**
 * @brief D-Bus service constants shared by all compositor plugins
 *
 * Centralized D-Bus interface names to avoid magic strings.
 * Used by KWin effect, Wayfire plugin, and any future compositor integration.
 */
inline constexpr QLatin1String Name("org.plasmazones");
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
inline constexpr QLatin1String Snap("org.plasmazones.Snap");
inline constexpr QLatin1String Scroll("org.plasmazones.Scroll");
}

/// D-Bus error names returned via `QDBusMessage::createErrorReply`. Centralised
/// so adaptors emit identical strings and consumers can match on them without
/// relying on free-form text.
namespace Error {
inline constexpr QLatin1String Busy("org.plasmazones.Error.Busy");
inline constexpr QLatin1String Shutdown("org.plasmazones.Error.Shutdown");
}

/// Single-instance app identities. Each PlasmaZones sub-process (settings,
/// editor) advertises its own service name and a small controller object so
/// the launcher can detect "already running" without scanning the bus.
namespace Apps {
namespace Settings {
inline constexpr QLatin1String ServiceName("org.plasmazones.Settings.App");
inline constexpr QLatin1String ObjectPath("/SettingsApp");
inline constexpr QLatin1String Interface("org.plasmazones.SettingsController");
}
namespace Editor {
inline constexpr QLatin1String ServiceName("org.plasmazones.Editor.App");
inline constexpr QLatin1String ObjectPath("/EditorApp");
inline constexpr QLatin1String Interface("org.plasmazones.EditorController");
}
}

// Protocol version. Bumped when the D-Bus method/signal schema changes in a
// backwards-incompatible way (e.g. dragStopped out-params changed, new
// required signal). Both sides check the peer's version at bridge registration
// and reject if below their minimum. The version is a simple integer string
// ("1", "2", …) to keep comparison trivial.
//
//   v1: original protocol (PlasmaZones v3.0–v3.x)
//   v2: split dragStopped + snapAssistReady signal (Phase C);
//       WindowGeometryEntry gained `screenId` (a(siiiis)) so the compositor
//       can seed its tracked-screen cache from the daemon's authoritative
//       answer instead of re-deriving from geometry (which races with VS
//       reconfig).
//   v3: setSnapAssistThumbnail signature changed from (s, s data:URL) to
//       (s, i, i, ay raw ARGB32) returning b. Thumbnail capture moved
//       out of the daemon and into the kwin-effect (OffscreenQuickScene
//       + WindowThumbnail through KWin's live compositor texture);
//       daemon ScreenShot2 D-Bus dependency and the matching
//       X-KDE-DBUS-Restricted-Interfaces gate are dropped. Mismatched
//       peers fail the bridge handshake instead of producing
//       method-not-found at first thumbnail post.
//
inline constexpr int ApiVersion = 3;
inline constexpr int MinPeerApiVersion = 3;

// Hard cap on blocking synchronous D-Bus calls from the editor/settings
// apps to the daemon. Qt's default is 25 seconds, long enough to freeze
// the UI for tens of seconds if the daemon event loop is busy. Daemon-side
// settings/shader handlers are all in-memory hash lookups (sub-millisecond
// in the healthy case), so 500 ms is generous while still degrading
// gracefully to caller-side defaults when the daemon is unresponsive.
inline constexpr int SyncCallTimeoutMs = 500;

// Timeout for the kwin-effect's daemon-readiness probe (an Introspect call
// fired against the org.plasmazones service to detect "daemon up but the
// daemonReady signal was emitted before the effect connected"). 3 s gives
// the daemon ample time to answer once its event loop is responsive while
// still keeping the effect from hanging on a wedged daemon.
inline constexpr int DaemonReadyProbeTimeoutMs = 3000;

// Timeout for the kwin-effect's snap-assist thumbnail post (carries an
// ARGB32 pixel payload). 2 s is "definitely something is wrong, drop the
// watcher" rather than expected latency. Without it, the effect would
// otherwise leak a watcher per snap-assist candidate per show until Qt's
// default 25 s timeout expires, which under daemon stress turns a
// transient hang into accumulated compositor-process state.
inline constexpr int SnapAssistThumbnailPostTimeoutMs = 2000;

// Shared cap for the snap-assist thumbnail LRU. The daemon sizes its
// QCache<QString, QImage> against this; the kwin-effect mirrors it for the
// "skip recently-posted handle" dedup window. Keeping the literal here
// (rather than two unrelated `static constexpr int`s in the daemon and the
// effect) means a future tuning bump moves both sides atomically — there
// is no longer a window where the effect believes the daemon holds entries
// the daemon has already evicted. 24 × 256² ARGB32 ≈ 6 MB on the daemon.
inline constexpr int SnapAssistThumbnailCacheCapacity = 24;

// Upper bound on entries accepted by the per-engine `windowsOpenedBatch`
// D-Bus methods (org.plasmazones.Scroll, …Autotile, …Snap). The session bus
// is unauthenticated within the user session, so a hostile or runaway peer
// could otherwise submit a multi-MB array. 4096 entries comfortably covers
// any realistic window count (Plasma sessions rarely exceed a few hundred);
// a batch larger than this is rejected outright. Centralised here so a
// future tuning bump moves every engine atomically.
inline constexpr int MaxWindowsOpenedBatchEntries = 4096;

} // namespace PhosphorProtocol::Service
