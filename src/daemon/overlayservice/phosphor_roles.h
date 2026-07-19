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
/// and post-migration snap-assist + layout picker) onto one wl_surface
/// per screen. FullscreenOverlay primitive (AnchorAll, no keyboard,
/// click-through). Permanently mapped after first show - keepMappedOnHide
/// is moot since per-content slots toggle visibility within the shared
/// scene graph rather than the wl_surface itself unmapping.
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
