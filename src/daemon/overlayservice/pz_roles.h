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

/// OSD base shape used by the unified notification surface (and any
/// future fullscreen-overlay-style surface). FullscreenOverlay primitive:
/// AnchorAll so the compositor ignores x/y hints and honours the margins
/// we write dynamically via the transport handle (see
/// `centerLayerWindowOnScreen` in osd.cpp) to centre the OSD on-screen.
/// No keyboard, click-through. Pre-warmed per screen at daemon start.
inline const PhosphorLayer::Role OsdBase = PhosphorLayer::Roles::FullscreenOverlay;

/// Unified notification surface — single per-screen wl_surface that hosts
/// both layout-OSD and navigation-OSD content via NotificationOverlay.qml's
/// mode-driven Loader. The two OSD modes share OsdBase (FullscreenOverlay,
/// AnchorAll, no keyboard, click-through) and are never simultaneously
/// visible, so a single Surface backs both. createNotificationWindow
/// extends this prefix per-screen-and-generation; the SurfaceAnimator
/// uses longest-prefix matching to apply the OSD config across all
/// derived prefixes.
inline const PhosphorLayer::Role Notification = OsdBase.withScopePrefix(QStringLiteral("plasmazones-notification"));

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
