// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

/// Zone selector: Top layer so pointer events route to it ahead of the
/// main overlay; per-corner anchors are set per-instance via overrides.
inline const PhosphorLayer::Role ZoneSelector = PhosphorLayer::Role{PhosphorLayer::Layer::Top,
                                                                    PhosphorLayer::AnchorNone,
                                                                    -1,
                                                                    PhosphorLayer::KeyboardInteractivity::None,
                                                                    QMargins(),
                                                                    QStringLiteral("plasmazones-zone-selector")};

/// Layout OSD (transient visual preview on layout switch). Overlay layer,
/// AnchorAll so the compositor ignores x/y hints and honours the margins
/// we write dynamically via the transport handle (see
/// centerLayerWindowOnScreen in osd.cpp) to centre the OSD on-screen.
/// No keyboard. Pre-warmed per screen at daemon start. Derived from
/// FullscreenOverlay so every PZ preset shares the library-primitive
/// construction pattern — only the scope distinguishes the two OSDs.
inline const PhosphorLayer::Role LayoutOsd =
    PhosphorLayer::Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("plasmazones-layout-osd"));

/// Navigation OSD (keyboard-nav feedback). Same shape as LayoutOsd —
/// different content + lifecycle.
inline const PhosphorLayer::Role NavigationOsd =
    PhosphorLayer::Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("plasmazones-navigation-osd"));

/// Snap assist (post-snap window picker). Top layer, exclusive keyboard
/// so Escape reliably dismisses. Singleton — one instance, re-targeted
/// to whichever screen the snap happened on.
inline const PhosphorLayer::Role SnapAssist =
    PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-snap-assist"));

/// Layout picker (interactive layout browser). Singleton modal with
/// exclusive keyboard. Shape matches SnapAssist but distinct scope so
/// the compositor keeps them independent.
inline const PhosphorLayer::Role LayoutPicker =
    PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-picker"));

/// Shader preview (editor Shader Settings dialog). Floating Overlay
/// layer, no anchors, no keyboard. Singleton. Positioned programmatically
/// by the caller.
inline const PhosphorLayer::Role ShaderPreview =
    PhosphorLayer::Roles::FloatingOverlay.withScopePrefix(QStringLiteral("plasmazones-shader-preview"));

} // namespace PzRoles

} // namespace PlasmaZones
