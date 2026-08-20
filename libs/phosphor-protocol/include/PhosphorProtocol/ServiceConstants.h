// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>
#include <QtGlobal>

namespace PhosphorProtocol::Service {

/**
 * @brief D-Bus service constants shared by all compositor plugins
 *
 * Centralized D-Bus interface names to avoid magic strings.
 * Used by KWin effect, Wayfire plugin, and any future compositor integration.
 */
inline constexpr QLatin1String Name("org.plasmazones");
inline constexpr QLatin1String ObjectPath("/PlasmaZones");

/// Quick-layout slots are numbered 1..QuickLayoutSlotCount. Protocol-level
/// because the daemon validates the bound on the D-Bus boundary while the
/// settings app walks it in its Quick Shortcuts reset loop, and the two live
/// in different trees. Kept here so those two cannot drift. Note the
/// phosphor-zones mirror, `LayoutRegistry::QuickSlotCount` (LayoutRegistry.h),
/// enforces the same 1..9 bound as its own named constant: that library
/// deliberately does not depend on phosphor-protocol, so the two are not
/// covered by one definition and must be updated in step by hand if the count
/// ever changes.
inline constexpr int QuickLayoutSlotCount = 9;

namespace Interface {
inline constexpr QLatin1String Settings("org.plasmazones.Settings");
inline constexpr QLatin1String WindowDrag("org.plasmazones.WindowDrag");
inline constexpr QLatin1String WindowTracking("org.plasmazones.WindowTracking");
inline constexpr QLatin1String Overlay("org.plasmazones.Overlay");
// Shared tiling-family engine pipeline (autotile + scrolling). Renamed
// from org.plasmazones.Autotile when the scrolling engine joined; the
// project ships daemon and effect together, so no wire compatibility
// alias is kept.
inline constexpr QLatin1String Tiling("org.plasmazones.Tiling");
// Engine-specific sibling interfaces (autotile verbs / scrolling screen
// set). Listed so this namespace stays a complete index of published
// interfaces even though the adaptors' Q_CLASSINFO must repeat the
// literal (macro argument).
inline constexpr QLatin1String Autotile("org.plasmazones.Autotile");
inline constexpr QLatin1String Scrolling("org.plasmazones.Scrolling");
inline constexpr QLatin1String LayoutRegistry("org.plasmazones.LayoutRegistry");
inline constexpr QLatin1String Screen("org.plasmazones.Screen");
inline constexpr QLatin1String ZoneDetection("org.plasmazones.ZoneDetection");
inline constexpr QLatin1String CompositorBridge("org.plasmazones.CompositorBridge");
inline constexpr QLatin1String Snap("org.plasmazones.Snap");
inline constexpr QLatin1String Rules("org.plasmazones.Rules");
inline constexpr QLatin1String Control("org.plasmazones.Control");
inline constexpr QLatin1String Shader("org.plasmazones.Shader");
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
inline constexpr QLatin1String DecorationProfileTree("decorationProfileTree");
}

/// Keys for the extended-window-property QVariantMap (the trailing a{sv} argument
/// of setWindowMetadata). The kwin-effect packs the live KWin-property / geometry
/// snapshot under these keys; the daemon unpacks them into WindowMetadata so its
/// window-rule resolvers match the same fields the effect path resolves. A key is
/// PRESENT only when the value is known — absence leaves the WindowQuery field
/// disengaged, mirroring window_query.cpp's engage-only-when-known contract.
namespace WindowMetadataKey {
inline constexpr QLatin1String IsMinimized("isMinimized");
/// Urgency (KWin's demandsAttention). Like IsMinimized, and unlike the
/// point-in-time fields, the effect re-pushes full metadata on every edge, so
/// a consumer may rely on it staying current.
inline constexpr QLatin1String IsDemandingAttention("isDemandingAttention");
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
/// Full 1-based desktop-number list for a window on SEVERAL (but not all)
/// virtual desktops. Sent only when the window spans more than one desktop;
/// the positional virtualDesktop arg stays the first entry for compatibility.
inline constexpr QLatin1String VirtualDesktops("virtualDesktops");
}

/// Keys of the two scrolling tab-indicator maps that cross the daemon → KWin
/// effect boundary on org.plasmazones.Tiling: the per-screen context-rule
/// PAINT override map (scrollTabPaintOverridesChanged / scrollTabPaintOverrides,
/// all six keys) and the per-window rule COLOUR map (scrollTabColors /
/// scrollTabColorsChanged, the three colour keys). The daemon produces them
/// (src/dbus/windowtrackingadaptor/internal.h's WindowPaintKeys / WindowColorKeys
/// forward to these) and the effect's TilingHandler reads them; this is the
/// ONE home for the spellings, so a rename is a compile error on both sides
/// rather than a silently dropped override. The XML DocStrings in
/// dbus/org.plasmazones.Tiling.xml (and a few C++ doc comments) repeat the
/// spellings as prose only.
namespace ScrollTabKey {
inline constexpr QLatin1String TabStyle("tabStyle");
inline constexpr QLatin1String GapsBetweenTabs("gapsBetweenTabs");
inline constexpr QLatin1String CornerRadius("cornerRadius");
inline constexpr QLatin1String ActiveColor("activeColor");
inline constexpr QLatin1String InactiveColor("inactiveColor");
inline constexpr QLatin1String UrgentColor("urgentColor");
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
//   v5: the org.plasmazones.Autotile lifecycle surface moved wholesale to
//       the engine-neutral org.plasmazones.Tiling interface (property
//       managedScreens, signal managedScreensChanged), with engine-specific
//       verbs split onto org.plasmazones.Autotile / org.plasmazones.Scrolling.
//       A v4 effect would pass the handshake and then silently receive
//       nothing on the renamed surface, so both sides must move together.
//   v6: TileRequestEntry gained a trailing scrollEdge field, widening the
//       windowsTileRequested signal from a(siiiissbbs) to a(siiiissbbss).
//       Qt's signal hooks are signature-matched before demarshalling, so a
//       v5 effect's slot would simply never fire on the widened payload —
//       ALL tiling silently dead until logout — which is exactly the
//       failure mode the handshake exists to surface up front.
//   v7: TileRequestEntry gained a trailing viewDeltaX field, widening the
//       windowsTileRequested signal from a(siiiissbbss) to a(siiiissbbssi).
//       It carries how far a scrolling strip's VIEW slid, so the effect can
//       spring that once per output and move the strip rigidly instead of
//       starting an independent per-window spring for each column. Same
//       signature-matching failure mode as v6: a v6 effect's slot would never
//       fire on the widened payload, killing all tiling until logout, so the
//       handshake has to reject the pairing up front.
//   v8: TileRequestEntry gained visualX / visualY / hasVisualPos, widening
//       windowsTileRequested from a(siiiissbbssi) to a(siiiissbbssiiib). A
//       parked scrolling column commits below the union of all outputs but has
//       to be SEEN travelling while the view slides, so the safe commit and the
//       paint position are now separate answers. Same signature-matched
//       failure mode as v6 and v7.
//   v9: Snap.resolveWindowRestore gained three in-args — isOpenPath,
//       minWidth, minHeight. The cross-screen tile reclaim hangs off this
//       slot, and two of its drivers are NOT opens (the unminimize of a
//       daemon-restart orphan and the pending-restores sweep); without the
//       flag the daemon could not tell them apart, and unminimizing a window
//       teleported it across monitors. The min sizes exist because a reclaim
//       ADOPTS the window into a strip/layout, and the adopting engine
//       evaluates its oversized/float verdict exactly once from them — the
//       tiling channel has always carried them, and passing 0,0 here left an
//       oversized window tiled for the session. Same signature-matched
//       failure mode as v6-v8: an old effect's four-arg call no longer
//       matches the widened slot.
//   v10: TileRequestEntry gained windowedFullscreen (after floating), widening
//       windowsTileRequested from a(siiiissbbssiiib) to a(siiiissbbbssiiib).
//       Scrolling windowed fullscreen: the effect flips KWin fullscreen state
//       on the client while committing the column rect. Mid-struct insertion,
//       so BOTH the signature and the field order change — same
//       signature-matched failure mode as v6 through v9.
//   v11: TileRequestEntry gained a trailing tabFrom field, widening
//       windowsTileRequested from a(siiiissbbbssiiib) to a(siiiissbbbssiiibs).
//       It names the tab an arriving tab is replacing in a tabbed column, so
//       the effect can cross-fade the two rather than hard-cut between two
//       different windows in one rect. The pair is not recoverable from the
//       rects, which is why it has to ride the wire. Same signature-matched
//       failure mode as v6 through v10.
//   v12: the scrolling strip's axis became per-screen configurable, so
//       TileRequestEntry::viewDeltaX became viewDelta (a signed scalar along
//       that screen's own strip axis) and scrollEdge's closed set widened from
//       {left,right} to {left,right,top,bottom}.
//
//       READ THIS BEFORE "SIMPLIFYING" THE BUMP AWAY. Unlike v6 through v11,
//       THIS CHANGE WIDENS NO SIGNATURE — the field is still an int and the
//       edge is still a string, so windowsTileRequested stays
//       a(siiiissbbbssiiibs). Every earlier bump had Qt's signature matching
//       as a second line of defence, and this one does not. A v11 effect
//       against a v12 daemon would demarshal PERFECTLY and then misbehave
//       twice over: it drops every vertical park at its own validationError
//       (unknown scrollEdge), losing those placements entirely, and it reads a
//       vertical viewDelta as a horizontal slide, flinging the strip sideways
//       for the length of every leg. The version handshake is the ONLY thing
//       rejecting that pairing.
//   v13: the scrolling tab indicators moved from a daemon-rendered layer
//       surface into the KWin effect's own paint pass. org.plasmazones.Scrolling
//       LOST scrollTabSurfaces and scrollTabSurfaceChanged, and
//       org.plasmazones.Tiling GAINED the required transport
//       (scrollTabStrips / scrollTabStripsChanged, scrollTabPaintOverrides /
//       scrollTabPaintOverridesChanged, scrollTabColors /
//       scrollTabColorsChanged). No existing signature widens, so as with v12
//       the handshake is the only thing refusing a mismatched pair: a v12
//       effect against a v13 daemon registers a surface the daemon no longer
//       knows and never hears a strip, and a v13 effect against a v12 daemon
//       queries three methods that do not exist — either way the pills are
//       silently absent rather than wrong.
//   v14: TileRequestEntry gained a trailing viewImmediate bool, widening
//       windowsTileRequested from a(siiiissbbbssiiibs) to a(siiiissbbbssiiibsb).
//       It marks a batch whose view travel is user-driven continuous motion
//       (the drag edge auto-scroll heartbeat): the effect applies the delta
//       outright instead of animating it, because a view leg retargeted every
//       16 ms never progresses on a stateless curve — the painted strip
//       stalls behind the committed geometry and glides once when the ticks
//       stop. Same signature-matched failure mode as v6 through v11 (v12 and
//       v13 widened none).
inline constexpr int ApiVersion = 14;
inline constexpr int MinPeerApiVersion = 14;

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

// Shared per-axis ceiling for snap-assist thumbnails. The daemon rejects
// anything larger at both the D-Bus boundary (OverlayAdaptor) and the service
// boundary (OverlayService, for direct C++ callers); the kwin-effect clamps
// its capture box against the same value so it can never produce a payload
// the daemon is guaranteed to refuse — an oversize refusal never marks the
// handle as recently-posted, which would otherwise re-capture on every show.
inline constexpr int SnapAssistThumbnailMaxDimension = 1024;

// Single accessor for the experimental zero-copy thumbnail gate. Read in
// FOUR places with different lifetimes (the effect's capture ctor, the
// daemon's D-Bus slot, the daemon's Vulkan device-extension wiring, and the
// effect's daemon-ready re-arm); routing every read through one helper keeps
// the spelling and the read discipline from drifting apart. The env var is
// process-constant, so callers may cache the result freely.
inline bool snapAssistDmabufThumbnailsEnabled()
{
    return qEnvironmentVariableIsSet("PLASMAZONES_DMABUF_THUMBNAILS");
}

} // namespace PhosphorProtocol::Service
