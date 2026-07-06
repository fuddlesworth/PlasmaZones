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
inline constexpr QLatin1String Rules("org.plasmazones.Rules");
}

/// D-Bus error names returned via `QDBusMessage::createErrorReply`. Centralised
/// so adaptors emit identical strings and consumers can match on them without
/// relying on free-form text.
namespace Error {
inline constexpr QLatin1String Busy("org.plasmazones.Error.Busy");
inline constexpr QLatin1String Shutdown("org.plasmazones.Error.Shutdown");
}

/// Property names exposed by the Settings interface's runtime D-Bus surface
/// (distinct from the persisted-config keys served by `ConfigDefaults` —
/// these names live on the wire only). Centralised so the daemon's
/// `SettingsAdaptor` getter/setter map and remote consumers
/// (`ClientHelpers::loadSettingAsync`) reference the same constant rather
/// than duplicating string literals on both ends of the bus.
namespace SettingProperty {
inline constexpr QLatin1String ShaderProfileTree("shaderProfileTree");
inline constexpr QLatin1String MotionProfileTree("motionProfileTree");
inline constexpr QLatin1String AnimationShaderSearchPaths("animationShaderSearchPaths");
}

/// Keys for the extended-window-property QVariantMap (the trailing a{sv} argument
/// of setWindowMetadata). The kwin-effect packs the live KWin-property / geometry
/// snapshot under these keys; the daemon unpacks them into WindowMetadata so its
/// window-rule resolvers match the same fields the effect path resolves. A key is
/// PRESENT only when the value is known — absence leaves the WindowQuery field
/// disengaged, mirroring window_query.cpp's engage-only-when-known contract.
namespace WindowMetadataKey {
inline constexpr QLatin1String IsMinimized("isMinimized");
inline constexpr QLatin1String IsFullscreen("isFullscreen");
inline constexpr QLatin1String IsSticky("isSticky");
inline constexpr QLatin1String IsMaximized("isMaximized");
inline constexpr QLatin1String IsFocused("isFocused");
inline constexpr QLatin1String IsTransient("isTransient");
inline constexpr QLatin1String IsNotification("isNotification");
inline constexpr QLatin1String KeepAbove("keepAbove");
inline constexpr QLatin1String KeepBelow("keepBelow");
inline constexpr QLatin1String SkipTaskbar("skipTaskbar");
inline constexpr QLatin1String SkipPager("skipPager");
inline constexpr QLatin1String SkipSwitcher("skipSwitcher");
inline constexpr QLatin1String IsModal("isModal");
inline constexpr QLatin1String HasDecoration("hasDecoration");
inline constexpr QLatin1String IsResizable("isResizable");
inline constexpr QLatin1String IsMovable("isMovable");
inline constexpr QLatin1String IsMaximizable("isMaximizable");
inline constexpr QLatin1String Width("width");
inline constexpr QLatin1String Height("height");
inline constexpr QLatin1String PositionX("positionX");
inline constexpr QLatin1String PositionY("positionY");
inline constexpr QLatin1String CaptionNormal("captionNormal");
}

/// Single-instance app identities. Each Phosphor sub-process (settings,
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
//   v1: original protocol (Phosphor v3.0–v3.x)
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
//   v4: setWindowMetadata widened from 4 args (instanceId, appId,
//       desktopFile, title) to 10: adds windowRole, pid, virtualDesktop,
//       activity, windowType, plus a trailing a{sv} (QVariantMap) carrying the
//       extended window-property snapshot (state flags, geometry, accessory
//       flags, captionNormal — see WindowMetadataKey) so the daemon's
//       window-rule resolvers match the same KWin-property fields the effect
//       path resolves live. A stale effect sending an older form would fail
//       marshalling, so the bridge handshake rejects mismatched peers up front.
//
inline constexpr int ApiVersion = 4;
inline constexpr int MinPeerApiVersion = 4;

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

} // namespace PhosphorProtocol::Service
