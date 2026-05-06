// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringView>

#include <PhosphorLayer/Role.h>

namespace PlasmaZones {

/// Role presets for PlasmaZones' layer-shell surfaces.
///
/// Each preset maps one of OverlayService's consumer types to the
/// protocol-level configuration (layer, anchors, keyboard, exclusive
/// zone, scope prefix) it wants. Built on top of PhosphorLayer's
/// library-provided @ref PhosphorLayer::Roles vocabulary.
namespace PzRoles {

/// Main zone overlay: fullscreen Overlay layer, click-through, no
/// exclusive zone (stays on top of panels). The overlay uses
/// AnchorAll for physical screens; virtual-screen surfaces override
/// to AnchorTop|AnchorLeft + margins via SurfaceConfig overrides.
inline const PhosphorLayer::Role Overlay =
    PhosphorLayer::Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("plasmazones-overlay"));

/// PhosphorZones::Zone selector: Top layer so pointer events route to it ahead of the
/// main overlay.
///
/// **Anchors are consumer-mandatory.** The preset ships `AnchorNone` but
/// `selector.cpp::createZoneSelectorWindow` ALWAYS supplies an
/// `anchorsOverride` (via `layerPlacementForVs`). The preset's anchors are
/// therefore unreachable in production — the None value is a placeholder
/// that signals "caller must override" rather than a real default. A
/// future refactor that starts honouring the preset anchors without also
/// updating the selector's create path would silently lose anchoring.
inline const PhosphorLayer::Role ZoneSelector = PhosphorLayer::Role{PhosphorLayer::Layer::Top,
                                                                    PhosphorLayer::AnchorNone,
                                                                    -1,
                                                                    PhosphorLayer::KeyboardInteractivity::None,
                                                                    QMargins(),
                                                                    QStringLiteral("plasmazones-zone-selector")};

/// OSD config-only role. The wl_surface lifetime moved to the unified
/// PassiveShell post-shell-migration; this role is preserved purely for
/// SurfaceAnimator config-lookup (registerConfigForRole keys on the
/// scope prefix, and the role-override beginShow/beginHide overloads
/// resolve per-content motion + shader profiles via this role's prefix
/// even though the shell's actual surface uses PassiveShell).
inline const PhosphorLayer::Role Notification =
    PhosphorLayer::Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("plasmazones-notification"));

/// Passive overlay shell — single per-screen wlr-layer-shell host that
/// groups every kbd-None overlay (OSD, zone-selector, main zone overlay,
/// and post-migration snap-assist + layout picker) onto one wl_surface
/// per screen. FullscreenOverlay primitive (AnchorAll, no keyboard,
/// click-through). Permanently mapped after first show — keepMappedOnHide
/// is moot since per-content slots toggle visibility within the shared
/// scene graph rather than the wl_surface itself unmapping.
///
/// Each per-content slot is animated by the SurfaceAnimator keyed on
/// (PassiveShell surface, slot QQuickItem). Per-content motion / shader
/// configs are resolved via the role-override `beginShow`/`beginHide`
/// overloads — the surface's own role is PassiveShell but the animation
/// config role is the per-content role (Notification, ZoneSelector, …)
/// so per-content profiles still drive each slot's transitions.
///
/// See `PassiveOverlayShell.qml` for the QML side and the unified-shell
/// migration commits for the per-consumer rewrite.
inline const PhosphorLayer::Role PassiveShell =
    PhosphorLayer::Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("plasmazones-passive-shell"));

/// Snap assist (post-snap window picker). Top layer, exclusive keyboard
/// so Escape reliably dismisses. Singleton — one instance, re-targeted
/// to whichever screen the snap happened on.
inline const PhosphorLayer::Role SnapAssist =
    PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-snap-assist"));

/// PhosphorZones::Layout picker (interactive layout browser). Singleton modal with
/// exclusive keyboard. Shape matches SnapAssist but distinct scope so
/// the compositor keeps them independent.
inline const PhosphorLayer::Role LayoutPicker =
    PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-picker"));

/// Shader preview (editor Shader Settings dialog). Floating Overlay
/// layer, no anchors, no keyboard. Singleton. Positioned programmatically
/// by the caller.
inline const PhosphorLayer::Role ShaderPreview =
    PhosphorLayer::Roles::FloatingOverlay.withScopePrefix(QStringLiteral("plasmazones-shader-preview"));

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
/// @param base       Named base role (e.g. PzRoles::Notification).
/// @param screenId   Effective screen id (physical or virtual).
/// @param generation Monotonic per-process counter, e.g. from
///                   `SurfaceManager::nextScopeGeneration()`.
[[nodiscard]] inline PhosphorLayer::Role makePerInstanceRole(const PhosphorLayer::Role& base, QStringView screenId,
                                                             quint64 generation)
{
    QString prefix;
    prefix.reserve(base.scopePrefix.size() + 1 + screenId.size() + 1 + 20);
    prefix.append(base.scopePrefix);
    prefix.append(QLatin1Char('-'));
    prefix.append(screenId);
    prefix.append(QLatin1Char('-'));
    prefix.append(QString::number(generation));
    return base.withScopePrefix(std::move(prefix));
}

} // namespace PzRoles

} // namespace PlasmaZones
