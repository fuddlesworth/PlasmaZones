// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QByteArray>
#include <QLatin1String>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

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
/// all eleven paint keys) and the per-window rule COLOUR map (scrollTabColors /
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
// The tab label's font. Paint keys like TabStyle, GapsBetweenTabs and
// CornerRadius, carried on the per-screen override map only — a window rule
// cannot restyle one tab's text, only recolour it, so unlike the three colours
// above these have no counterpart on the colour map. There is no size key:
// the effect's painter fits the label to the pill thickness.
inline constexpr QLatin1String FontFamily("fontFamily");
inline constexpr QLatin1String FontWeight("fontWeight");
inline constexpr QLatin1String FontItalic("fontItalic");
inline constexpr QLatin1String FontUnderline("fontUnderline");
inline constexpr QLatin1String FontStrikeout("fontStrikeout");
}

/// Keys of the scrolling BEHAVIOUR map that crosses the daemon → KWin effect
/// boundary on org.plasmazones.Scrolling (scrollEffectBehaviour /
/// scrollEffectBehaviourChanged): three already-resolved screen-id lists, the
/// per-context focus-follows-mouse and crop-straddlers rule slots and the
/// vertical-axis MEMBERSHIP (a screen in the list runs its strip vertically;
/// absence means horizontal).
///
/// What is NOT here is the focus-follows-mouse scroll cap's blocked-window
/// list, which rides its own property beside this one. The split is about
/// UPDATE RATE, not about shape: these three answer settings and rules and
/// change when the user changes something, while the block list is a fact
/// about where each strip's view currently sits and is re-derived on every
/// relayout. Folded in here, a strip that merely scrolled made the effect
/// re-parse and re-compare three screen lists that had not moved.
///
/// The daemon's ScrollingAdaptor produces the map and the effect's
/// TilingHandler reads it; one home for the spellings, on
/// ScrollTabKey's terms, so a rename is a compile error on both sides rather
/// than a lookup that silently misses its diagnostic label. The XML DocString
/// in dbus/org.plasmazones.Scrolling.xml repeats them as prose only.
namespace ScrollBehaviourKey {
inline constexpr QLatin1String FocusFollowsMouse("focusFollowsMouse");
inline constexpr QLatin1String CropStraddlers("cropStraddlers");
inline constexpr QLatin1String VerticalAxis("verticalAxis");
}

/// Tab keys on the STRIP PREVIEW payload — the per-tile description of the
/// tab indicator its column draws, so a preview of the strip shows a tabbed
/// column as tabbed instead of as a plain window.
///
/// Three surfaces spell these and they must agree: the daemon's
/// visibleStripJson wire (scrollingadaptor.cpp), the daemon's own OSD strip
/// card, which builds its zone maps in-process without crossing the bus
/// (daemon/stripzones.h), and the settings app, which reshapes the wire into
/// the same zone maps (settingscontroller_session.cpp). One home for the
/// spellings is what keeps a rename from silently dropping the indicator on
/// one surface only.
///
/// TWO QML readers spell the keys as raw strings and cannot use these
/// constants: ZonePreview.qml, which draws the far end of all three, and
/// MonitorStatePage.qml, whose per-tile diff decides whether a strip read is
/// worth repainting. The second one fails QUIETLY under a rename — its diff
/// would compare undefined to undefined, conclude nothing changed, and stop
/// repainting on a tab switch — so it belongs in any rename's checklist even
/// though nothing about it looks like a reader.
///
/// Distinct from ScrollTabKey above: those are the effect's PAINT overrides
/// for the tab pills it draws on screen. These describe a strip being drawn
/// as a thumbnail, and no consumer of one reads the other.
///
/// Deliberately not ZoneJsonKeys: that namespace owns the zone/layout FILE
/// format, and a strip preview's synthetic zones are not layout zones (see
/// that header's note on payloads that merely share a spelling).
namespace StripPreviewKey {
/// How many tabs the tile's column shows, and absent or 0 when it draws no
/// indicator — the single "no indicator here" gate on this payload, as the
/// null rect is on the compositor's.
inline constexpr QLatin1String TabCount("tabCount");
/// The tile's own 0-based tab within that count. Note the strip-card payload
/// (src/common/stripcardserialize.cpp) spells an unrelated BOOLEAN the same
/// way for a different consumer; the two never meet, but a grep for the
/// spelling finds both.
inline constexpr QLatin1String ActiveTab("activeTab");
/// Which edge the indicator runs along, as a PhosphorScrollEngine::
/// TabIndicatorPosition underlying value (0 Left, 1 Right, 2 Top, 3 Bottom).
inline constexpr QLatin1String TabPosition("tabPosition");
/// How much of that edge the indicator covers, 0.0 to 1.0, centered on it.
/// No thickness counterpart by design: a few pixels of indicator is
/// sub-pixel once a screen is scaled into a thumbnail, so a preview floors
/// the thickness at whatever it can actually draw.
inline constexpr QLatin1String TabLength("tabLength");
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
//   v5: the v3.4 wire. Everything below landed in one unreleased cycle, so it
//       is a single step away from the v4 that last shipped. Do NOT split it
//       back into one bump per change: no released peer ever spoke an
//       intermediate version, and MinPeerApiVersion has always tracked
//       ApiVersion exactly, so those steps were unobservable outside the
//       branch. A new change AFTER v5 ships needs v6.
//
//       Interfaces. The org.plasmazones.Autotile lifecycle surface moved
//       wholesale to the engine-neutral org.plasmazones.Tiling interface
//       (property managedScreens, signal managedScreensChanged), with
//       engine-specific verbs split onto org.plasmazones.Autotile /
//       org.plasmazones.Scrolling. The scrolling tab indicators moved from a
//       daemon-rendered layer surface into the KWin effect's own paint pass,
//       so org.plasmazones.Scrolling LOST scrollTabSurfaces and
//       scrollTabSurfaceChanged and org.plasmazones.Tiling GAINED the
//       transport that replaces them (scrollTabStrips / scrollTabStripsChanged,
//       scrollTabPaintOverrides / scrollTabPaintOverridesChanged,
//       scrollTabColors / scrollTabColorsChanged).
//
//       windowsTileRequested. TileRequestEntry widened from a(siiiissbbs) to
//       a(siiiissbbbssiiibsb). The added fields, in wire order: scrollEdge
//       (which side of the strip a column departed towards, a closed set of
//       left / right / top / bottom because a strip's axis is per-screen);
//       windowedFullscreen, inserted after floating, which has the effect flip
//       KWin fullscreen state on the client while committing the column rect;
//       viewDelta, how far the strip's VIEW slid along that screen's own strip
//       axis, so the effect springs it once per output and moves the strip
//       rigidly instead of starting an independent per-window spring for each
//       column; visualX / visualY / hasVisualPos, because a parked column
//       commits below the union of all outputs but has to be SEEN travelling
//       while the view slides, so the safe commit and the paint position are
//       separate answers; tabFrom, naming the tab an arriving tab replaces in a
//       tabbed column so the effect can cross-fade the two rather than hard-cut
//       between two windows in one rect; and viewImmediate, marking a batch
//       whose view travel is user-driven continuous motion (the drag edge
//       auto-scroll heartbeat) so the effect applies the delta outright instead
//       of animating it, because a leg retargeted every 16 ms never progresses
//       on a stateless curve.
//
//       Snap.resolveWindowRestore gained three in-args: isOpenPath, minWidth,
//       minHeight. The cross-screen tile reclaim hangs off this slot and two of
//       its drivers are NOT opens (the unminimize of a daemon-restart orphan
//       and the pending-restores sweep); without the flag the daemon could not
//       tell them apart, and unminimizing a window teleported it across
//       monitors. The min sizes exist because a reclaim ADOPTS the window into
//       a strip or layout and the adopting engine evaluates its oversized /
//       float verdict exactly once from them, so passing 0,0 left an oversized
//       window tiled for the session.
//
//       Why the handshake and not signature matching. Most of the above widens
//       a signature, and Qt matches signal-hook signatures before demarshalling,
//       so a v4 effect's slot would simply never fire on the widened payload —
//       ALL tiling silently dead until logout. But parts of it widen NOTHING:
//       the interface moves above, and scrollEdge's closed set growing from
//       {left,right} to {left,right,top,bottom} while viewDelta stayed an int.
//       A peer mismatched on those demarshals PERFECTLY and then misbehaves,
//       dropping every vertical park at its own validationError and reading a
//       vertical view delta as a horizontal slide. The version handshake is the
//       ONLY thing that refuses such a pairing, which is why both sides must be
//       built and shipped from the same source.
//   v6: columnMaximized on TileRequestEntry, widening it from
//       a(siiiissbbbssiiibsb) to a(siiiissbbbbssiiibsb). The flag is inserted
//       after windowedFullscreen, its nearest sibling in both meaning and
//       handling: both are scrolling-only compositor states the effect imposes
//       on the client while the strip keeps owning the rect.
//
//       It exists because the effect intercepts a window's maximize request
//       and routes it to the scrolling engine's maximize-column verb, which
//       makes KWin's maximize bit a VIEW of engine state rather than state in
//       its own right. Something has to drive that bit when the verb is
//       reached any other way (the Meta+Alt+F shortcut, a width verb that
//       lands full width), or the titlebar button renders un-toggled and the
//       two entry points disagree about one window.
//
//       Carried as data rather than inferred from the committed rect, which
//       the effect already has. A tile's main extent is the column's main
//       extent LESS any within-column tab-indicator reservation, so a
//       maximized tabbed column measures under full width and would read as
//       not-maximized — and the engine's own definition (its pre-maximize
//       slot) is the only authority that survives that. Same doctrine as
//       tabFrom: the engine names what it did instead of leaving the
//       compositor to infer it from rect coincidence.
inline constexpr int ApiVersion = 6;
inline constexpr int MinPeerApiVersion = 6;

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

// How big the snap-assist card actually draws a thumbnail, in logical
// pixels, as a function of the zone it sits in and how many candidates
// share it. This is the C++ twin of the sizing bindings in
// src/ui/SnapAssistContent.qml (candidateFlow.iconSize: cardScaleBase,
// iconSizeRatio, minIconSize) and MUST be kept in step with them. The
// effect captures at this size times the output scale rather than at a
// fixed 256, so a burst of candidates ships small buffers and a lone
// candidate in a big zone on a HiDPI output is not upscaled. Drift between
// the two only costs sharpness or bytes, never correctness, and the
// headroom factor absorbs the rounding.
inline constexpr double SnapAssistCardScaleBase = 0.35;
inline constexpr double SnapAssistIconSizeRatio = 0.6;
inline constexpr int SnapAssistMinIconLogicalPx = 16; // Kirigami.Units.iconSizes.small
inline constexpr double SnapAssistThumbnailHeadroom = 1.25;

inline int snapAssistThumbnailBoxPx(int zoneMinAxisLogicalPx, int candidateCount, double outputScale)
{
    const double cardScale = SnapAssistCardScaleBase / std::max(1.0, std::sqrt(double(std::max(1, candidateCount))));
    const double iconLogical =
        std::max(double(SnapAssistMinIconLogicalPx),
                 double(std::max(0, zoneMinAxisLogicalPx)) * cardScale * SnapAssistIconSizeRatio);
    const double devicePx = iconLogical * std::max(1.0, outputScale) * SnapAssistThumbnailHeadroom;
    return std::clamp(int(std::ceil(devicePx)), SnapAssistMinIconLogicalPx, SnapAssistThumbnailMaxDimension);
}

// The default-OFF rule for a boolean environment switch: set to any value
// other than "0" enables, "0" or unset disables. Value-checked rather than
// presence-checked so an explicit opt-out is honoured, and not parsed as an
// integer so the presence-style spellings the switches have always been
// documented with ("=1", "=true", "=yes") keep working. "=0" is the only
// opt-out; anything else set is an opt-in.
//
// Switches in this header pick one of two rules: this one, or the default-ON
// envSwitchEnabledByDefault below. The "=0" spelling disables under both, so
// a kill switch reads the same to a user whichever twin backs it.
inline bool envSwitchEnabled(const char* name)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return false;
    }
    return qgetenv(name).trimmed() != "0";
}

// The opt-out twin of envSwitchEnabled, for a switch that defaults to on:
// unset enables, and "0" is the only spelling that disables. Any other value
// is read as an explicit opt-in and left enabled, so the documented "=1" /
// "=true" spellings keep meaning what they say.
inline bool envSwitchEnabledByDefault(const char* name)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return true;
    }
    return qgetenv(name).trimmed() != "0";
}

// Single accessor for the zero-copy thumbnail gate. Read in FOUR places with
// different lifetimes (the effect's capture ctor, the daemon's D-Bus slot,
// the daemon's Vulkan device-extension wiring, and the effect's daemon-ready
// re-arm); routing every read through one helper keeps the spelling and the
// read discipline from drifting apart. The env var is process-constant, so
// callers may cache the result freely.
//
// Default-on: the dma-buf path is the normal transport, and
// PLASMAZONES_DMABUF_THUMBNAILS=0 is the kill switch for a driver or session
// where it misbehaves. The raw-pixel path stays in place as the automatic
// fallback for every case the gate cannot predict — a software or otherwise
// unsupported RHI backend in the daemon, a cross-GPU import the driver
// refuses, a missing EGL/Vulkan extension, or a format/modifier combination
// the importer rejects. Both processes read the same variable, and the two
// sides do NOT have to agree: the daemon refusing the import simply routes
// the effect back to pixels.
inline bool snapAssistDmabufThumbnailsEnabled()
{
    return envSwitchEnabledByDefault("PLASMAZONES_DMABUF_THUMBNAILS");
}

// Diagnostic switch for the snap-assist thumbnail pipeline. When set, both
// the kwin-effect capture side and the daemon receive side log one line per
// thumbnail with the fitted size, per-stage timings (render, readback,
// convert, D-Bus round-trip, cache insert, QML fetch) and byte counts, under
// the *.snapassist.trace logging categories. Read-only instrumentation: it
// changes nothing about what is captured or how. Shared here for the same
// reason as the dma-buf gate above, so both processes agree on the spelling,
// and it follows the same "=0 disables" rule.
inline bool snapAssistThumbnailTraceEnabled()
{
    return envSwitchEnabled("PLASMAZONES_THUMBNAIL_TRACE");
}

} // namespace PhosphorProtocol::Service
