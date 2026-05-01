// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/ProfilePaths.h>

#include <QStringList>

namespace PlasmaZones {

/// Single source of truth for "which event paths actually run a per-event
/// transition shader". Both the daemon (overlay service consumes these
/// paths via @c OverlayService::applyShaderProfilesToAnimator) and the
/// settings UI (animation event cards gate the shader picker on this
/// list) read from this function so the two cannot drift.
///
/// A path appears here ONLY when there is a registered
/// @c PhosphorAnimationLayer::SurfaceAnimator::Config that propagates
/// @c showShaderEffectId / @c hideShaderEffectId to a real surface. See
/// @c src/daemon/overlayservice.cpp's @c buildOsdConfig /
/// @c buildLayoutPickerConfig / @c buildZoneSelectorConfig /
/// @c buildSnapAssistConfig for the canonical wiring.
///
/// When a future surface adds a shader leg, append its leg paths here AND
/// add the corresponding @c resolveShaderEffect call site in
/// overlayservice.cpp. The settings UI will start showing the shader
/// picker on the new path automatically.
inline QStringList shaderSupportedEventPaths()
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return QStringList{
        // Genuine OSDs (notification surface, both LayoutOsd and NavigationOsd modes).
        PP::OsdShow,
        PP::OsdHide,
        // Popup family — leg-leaf paths for each popup surface that runs a shader.
        PP::PanelPopupLayoutPickerShow,
        PP::PanelPopupLayoutPickerHide,
        PP::PanelPopupZoneSelectorShow,
        PP::PanelPopupZoneSelectorHide,
        PP::PanelPopupSnapAssistShow,
        // SnapAssist's hide leg is intentionally absent — the surface
        // is destroy-on-hide and never paints a hide frame, so a shader
        // assignment there would be runtime no-op.
    };
}

/// Convenience predicate used by the settings UI (Q_INVOKABLE-bridged)
/// and the daemon's optional verification path.
inline bool eventPathSupportsShaderLeg(const QString& path)
{
    return shaderSupportedEventPaths().contains(path);
}

} // namespace PlasmaZones
