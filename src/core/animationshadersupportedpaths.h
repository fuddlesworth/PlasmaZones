// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimationShaders/ShaderProfileTree.h>

#include <QSet>
#include <QString>
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

/// Drop every per-path override from @p src whose path is NOT in
/// @c shaderSupportedEventPaths(). The baseline is preserved verbatim.
///
/// Why this exists: the settings UI gates its shader picker on
/// @c eventPathSupportsShaderLeg, but earlier UI revisions exposed the
/// picker on every event row and persisted overrides for paths the
/// daemon never consumed. Those entries now SHADOW user-intended parent
/// overrides at runtime (e.g. user sets `panel = slide` but a stale
/// `panel.popup.zoneSelector.show = pixelate` leaf wins via the deeper-
/// path-wins overlay merge), and the new UI hides the picker that would
/// let the user clear them — making the bug sticky.
///
/// Calling this pruner on every read AND every write at the Settings
/// layer means an affected config self-heals on the next save, and a
/// fresh write coming from a Q_INVOKABLE that bypasses the UI gate
/// (e.g. a future scripting hook) still cannot stamp unsupported-path
/// entries onto disk.
inline PhosphorAnimationShaders::ShaderProfileTree
pruneShaderProfileTreeToSupportedPaths(const PhosphorAnimationShaders::ShaderProfileTree& src)
{
    static const QSet<QString> kSupported = []() {
        const QStringList list = shaderSupportedEventPaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();

    PhosphorAnimationShaders::ShaderProfileTree pruned;
    pruned.setBaseline(src.baseline());
    const QStringList overriddenPaths = src.overriddenPaths();
    for (const QString& path : overriddenPaths) {
        if (kSupported.contains(path)) {
            pruned.setOverride(path, src.directOverride(path));
        }
    }
    return pruned;
}

} // namespace PlasmaZones
