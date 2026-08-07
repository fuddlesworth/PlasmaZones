// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorLayer/Role.h>
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorShellPatterns/Patterns.h>

#include <QString>
#include <QStringView>

namespace PlasmaZones {

/// Role presets for PlasmaZones' layer-shell surfaces.
///
/// Each preset maps one of OverlayService's consumer types to the
/// protocol-level configuration (layer, anchors, keyboard, exclusive
/// zone, scope prefix) it wants. Built on top of the
/// @ref PhosphorShellPatterns axis-2 UI-pattern vocabulary.
namespace PhosphorRoles {

/// Zone overlay: the full-screen layer that paints zone rectangles and
/// hosts the snap-assist/zone-selector slots. Hud pattern (Overlay layer,
/// click-through, no exclusive zone). AnchorAll for physical screens;
/// virtual-screen surfaces override to AnchorTop|AnchorLeft + margins
/// via SurfaceConfig overrides.
inline const PhosphorLayer::Role ZoneOverlay =
    PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("plasmazones-zone-overlay"));

/// Zone selector: animator-config-only role (matches the OSD /
/// SnapAssist / LayoutPicker pattern). Post-shell-migration the zone
/// selector is a slot inside the per-screen PassiveShell - the surface
/// anchoring lives on the shell's PassiveShell role, and this role only
/// names an animator scope (`plasmazones-zone-selector`) for the slot's
/// show/hide leg. The Top layer + AnchorNone fields are placeholders
/// preserved for the (now unreachable) standalone-window path.
inline const PhosphorLayer::Role ZoneSelector = PhosphorLayer::Role{PhosphorLayer::Layer::Top,
                                                                    PhosphorLayer::AnchorNone,
                                                                    -1,
                                                                    PhosphorLayer::KeyboardInteractivity::None,
                                                                    QMargins(),
                                                                    QStringLiteral("plasmazones-zone-selector")};

/// OSD config-only role. Used by both LayoutOsd (which layout is active)
/// and NavigationOsd (focus-change indicators); a single animator config
/// drives both since they share the same fade/scale motion. The
/// wl_surface lifetime moved to the unified PassiveShell post-shell-
/// migration; this role is preserved purely for SurfaceAnimator config-
/// lookup (registerConfigForRole keys on the scope prefix, and the
/// role-override beginShow/beginHide overloads resolve per-content
/// motion + shader profiles via this role's prefix even though the
/// shell's actual surface uses PassiveShell).
inline const PhosphorLayer::Role Osd = PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("plasmazones-osd"));

/// Passive overlay shell - single per-screen wlr-layer-shell host that
/// groups every kbd-None overlay (OSD, zone-selector, main zone overlay,
/// and post-migration snap-assist + layout picker + cheatsheet) onto one
/// wl_surface per screen. FullscreenOverlay primitive (AnchorAll, no keyboard,
/// click-through). Hiding ONE slot never unmaps anything: the per-content
/// slots toggle visibility within the shared scene graph, and the shell's
/// own show/hide is driven off the anyVisible aggregate, so the wl_surface
/// only moves when the LAST visible slot goes.
///
/// At that point keepMappedOnHide decides, and it is NOT moot: it is
/// effects-gated in createWarmedOsdSurface. With shaders or animations
/// enabled the shell stays mapped and keeps its warmed Vulkan swapchain;
/// with BOTH disabled it is false and the wl_surface is unmapped
/// synchronously, so the next slot to show pays a full remap. Repeatedly
/// arming and releasing a hold-mode trigger during one drag toggles the
/// drop indicator, which on that configuration means an unmap/remap per
/// cycle. That is the deliberate trade (an idle daemon should not keep a
/// composited surface alive when it is drawing nothing), and it applies
/// equally to every consumer of this shell, but it is a real cost rather
/// than the no-op the previous wording claimed.
///
/// Layer downgrade from Overlay to Top is deliberate (issue #516). A
/// fullscreen wlr Overlay-layer surface above the active toplevel masks
/// KWin's Translucency-while-moving effect, and on hybrid Intel+NVIDIA
/// systems forces a slower compositional path that produces visible drag
/// artifacts and post-snap flicker. Top still draws the zone preview
/// above normal toplevels (it is what KDE's own panel uses) but lets
/// KWin keep the translucency render path and the fast composition
/// route. Fullscreen apps on Overlay still draw above the zone preview,
/// which is the correct behaviour anyway.
///
/// Each per-content slot is animated by the SurfaceAnimator keyed on
/// (PassiveShell surface, slot QQuickItem). Per-content motion / shader
/// configs are resolved via the role-override `beginShow`/`beginHide`
/// overloads - the surface's own role is PassiveShell but the animation
/// config role is the per-content role (Osd, ZoneSelector, …) so
/// per-content profiles still drive each slot's transitions.
///
/// See `PassiveOverlayShell.qml` for the QML side and the unified-shell
/// migration commits for the per-consumer rewrite.
inline const PhosphorLayer::Role PassiveShell = PhosphorShellPatterns::Hud()
                                                    .withLayer(PhosphorLayer::Layer::Top)
                                                    .withScopePrefix(QStringLiteral("plasmazones-passive-shell"));

/// Snap-assist config-only role. The wl_surface lifetime moved to the
/// unified PassiveShell post-shell-migration; this role is preserved
/// purely for SurfaceAnimator config-lookup (registerConfigForRole keys
/// on the scope prefix, and the role-override beginShow/beginHide
/// overloads resolve per-content motion + shader profiles via this
/// role's prefix even though the shell's actual surface uses
/// PassiveShell). Escape-to-dismiss is wired via the daemon's
/// `cancel_overlay_during_drag` global accelerator (see start.cpp's
/// snapAssistShown handler) since the shell is kbd-None.
///
/// Singleton at the daemon level - m_snapAssistScreenId tracks which
/// screen's slot is active and re-targets across screens.
inline const PhosphorLayer::Role SnapAssist =
    PhosphorShellPatterns::Modal().withScopePrefix(QStringLiteral("plasmazones-snap-assist"));

/// Layout-picker config-only role. Same migration story as SnapAssist -
/// the picker now lives as an Item slot inside the per-screen passive
/// shell, and this role exists purely as the SurfaceAnimator config
/// lookup key. Picker keyboard navigation (arrow keys + Return/Enter
/// + Escape) is routed via KGlobalAccel ad-hoc shortcuts registered
/// by `WindowDragAdaptor::ensureLayoutPickerNavShortcutsRegistered`
/// on show and released on dismiss.
inline const PhosphorLayer::Role LayoutPicker =
    PhosphorShellPatterns::Modal().withScopePrefix(QStringLiteral("plasmazones-layout-picker"));

/// Shortcut-cheatsheet config-only role. Same shape as LayoutPicker — the
/// sheet is an Item slot inside the per-screen passive shell and this role
/// exists purely as the SurfaceAnimator config lookup key. Escape-to-dismiss
/// rides a dedicated KGlobalAccel ad-hoc grab registered on show and
/// released on dismiss (the shell is kbd-None). Singleton at the daemon
/// level — m_cheatsheetScreenId tracks which screen's slot is active.
inline const PhosphorLayer::Role Cheatsheet =
    PhosphorShellPatterns::Modal().withScopePrefix(QStringLiteral("plasmazones-cheatsheet"));

/// Scroll tab-strip config-only role. The tab indicators for tabbed
/// scrolling columns live as a per-screen Item slot (NOT a singleton —
/// every scrolling screen can carry strips at once); this role exists
/// purely as the SurfaceAnimator role lookup key.
/// DELIBERATELY has no registered per-role config: the animation-profile
/// taxonomy defines no popup.scrollTabs domain, so both legs use the
/// library-default 150 ms motion and user motion/shader profiles do not
/// apply (see setupSurfaceAnimator's no-config list). Display-only and
/// click-through everywhere outside the indicator rects, which the tab
/// shell's own input region gives it.
inline const PhosphorLayer::Role ScrollTabs =
    PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("plasmazones-scroll-tabs"));

/// Scroll tab-indicator shell — the per-screen wl_surface the tab
/// indicators get to themselves, hosting nothing but the ScrollTabs slot.
/// Same FullscreenOverlay primitive and Top layer as @ref PassiveShell,
/// and for the same reasons (see that role's note on the Overlay→Top
/// downgrade); it differs only in being EXCLUSIVE to the indicators.
///
/// The exclusivity is the entire point, and it is not about size. The
/// compositor slides this surface with the scrolling strip, applying the
/// same view offset it applies to the columns, so the indicators travel
/// with the windows they label instead of being hidden for the length of
/// every scroll. A surface translates as a whole, so the indicators cannot
/// share one with anything that must hold still — and the passive shell
/// carries the navigation OSD, which fires on the very action that
/// scrolls.
///
/// ONE surface per screen, not one per indicator: every column takes the
/// same offset, so per-indicator surfaces would add a create, a destroy, a
/// commit and an input region per column scrolling in or out, all applying
/// an identical translation. Screen-sized for the same reason the passive
/// shell is: indicators spread across the whole strip, and re-anchoring a
/// bounding box on every relayout would be pure churn.
///
/// The prefix is the SurfaceAnimator's longest-prefix config lookup key and
/// nothing else. The effect cannot read a layer-shell scope at all, so it
/// tells this surface apart by the wl_surface object id the daemon announces
/// over D-Bus (announceScrollTabSurface, matched in
/// PlasmaZonesEffect::isScrollTabIndicatorSurface). The load-bearing contract
/// is therefore the announce and retract pairing, not this string.
inline const PhosphorLayer::Role ScrollTabShell = PhosphorShellPatterns::Hud()
                                                      .withLayer(PhosphorLayer::Layer::Top)
                                                      .withScopePrefix(QStringLiteral("plasmazones-scroll-tab-shell"));

/// Scroll drag drop-indicator config-only role. Paints the slot a dragged
/// window would land in while a scrolling drag re-insert is armed, so the
/// drop target is visible the way autotile's live restructure makes it
/// visible. Per-screen like ScrollTabs (a drag can cross screens and the
/// indicator follows), and like ScrollTabs it registers no per-role config:
/// the taxonomy defines no domain for it, so both legs use the library
/// default motion. Display-only and click-through — it must never take
/// input, since it is painted underneath a cursor that is mid-drag.
inline const PhosphorLayer::Role ScrollDropIndicator =
    PhosphorShellPatterns::Hud().withScopePrefix(QStringLiteral("plasmazones-scroll-drop-indicator"));

/// Shader preview (editor Shader Settings dialog). Floating Overlay
/// layer, no anchors, no keyboard. Singleton. Positioned programmatically
/// by the caller.
inline const PhosphorLayer::Role ShaderPreview =
    PhosphorShellPatterns::Floating().withScopePrefix(QStringLiteral("plasmazones-shader-preview"));

/// Build a per-instance Role from one of the base roles above by appending
/// `-{screenId}-{generation}` to its base scope prefix. Single-source for
/// the policy "per-instance scope prefix-matches the base role's prefix" so
/// the SurfaceAnimator's longest-prefix lookup always resolves the
/// registered config (see `setupSurfaceAnimator`).
///
/// Pre-existing failure modes this prevents:
///  - Build the per-instance literal from scratch (e.g. typo
///    "plasmazones-notif-..."), or
///  - Pass `OsdBase` instead of the named family role and re-type the
///    literal, then later rename the family role in this header.
/// Either case made the longest-prefix match silently miss and the
/// surface fell back to the library's empty default config.
///
/// @param base       Named base role (e.g. PhosphorRoles::Osd).
/// @param screenId   Effective screen id (physical or virtual).
/// @param generation Monotonic per-process counter, e.g. from
///                   `SurfaceManager::nextScopeGeneration()`.
[[nodiscard]] inline PhosphorLayer::Role makePerInstanceRole(const PhosphorLayer::Role& base, QStringView screenId,
                                                             quint64 generation)
{
    // Delegates to PhosphorOverlay::makePerInstanceRole - the
    // scope-prefix-construction policy lives in the lib so any consumer
    // (not just PZ) gets the same SurfaceAnimator longest-prefix lookup
    // guarantee. PZ keeps this thin wrapper because every existing call
    // site uses the PhosphorRoles:: namespace; the wrapper is the migration
    // bridge, not a fork.
    return PhosphorOverlay::makePerInstanceRole(base, screenId, generation);
}

} // namespace PhosphorRoles

} // namespace PlasmaZones
