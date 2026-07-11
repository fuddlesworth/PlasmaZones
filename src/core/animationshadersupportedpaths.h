// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QSet>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// Leaf event paths the daemon's overlay service AND the KWin effect
/// actually resolve a shader effect for. Each appears as a
/// @c resolveShaderEffect(tree, ...) call inside one of
/// @c OverlayService::buildOsdConfig / @c buildLayoutPickerConfig /
/// @c buildZoneSelectorConfig / @c buildSnapAssistConfig, OR as a
/// @c tryBeginShaderForEvent(...) call in @c kwin-effect/plasmazoneseffect.cpp
/// — when a future surface adds a shader leg, append its leg paths
/// here in lockstep.
inline QStringList shaderConsumedLeafEventPaths()
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return QStringList{
        // Genuine OSDs (notification surface, both LayoutOsd and NavigationOsd modes).
        PP::OsdShow,
        PP::OsdHide,
        // Popup family — leg-leaf paths for each popup surface that runs a shader.
        PP::PopupLayoutPickerShow,
        PP::PopupLayoutPickerHide,
        PP::PopupZoneSelectorShow,
        PP::PopupZoneSelectorHide,
        PP::PopupSnapAssistShow,
        PP::PopupSnapAssistHide,
        //
        // Window family — driven by the KWin OffscreenEffect at
        // kwin-effect/plasmazoneseffect.cpp via tryBeginShaderForEvent
        // on each window-lifecycle hook (windowAdded/windowClosed/
        // windowStartUserMovedResized for the held move/maximized/
        // minimized/focusChanged).
        // The effect resolves m_shaderProfileTree.resolve(path) per
        // event, drives a per-window iTime AnimatedValue, and runs the
        // shader on the OffscreenEffect's redirected texture quad.
        PP::WindowOpen,
        PP::WindowClose,
        PP::WindowMinimize,
        PP::WindowMaximize,
        PP::WindowMove,
        PP::WindowFocus,
        // Snap-into-zone window animations driven by the kwin-effect's
        // applyWindowGeometry / daemon_apply chokepoints. Each routes
        // through tryBeginShaderForEvent so the user can pick a
        // distinct shader per snap event.
        //
        // There are NO resize legs: `window.movement.resize` (the
        // interactive edge-drag) and the never-routed
        // `window.movement.snapResize` were dropped from the taxonomy
        // entirely — a held resize has no discrete before/after for a
        // crossfade and no sim support for physics packs, and discrete
        // resizes are covered by the snap / layoutSwitch / maximize
        // events (the resize-only branch of applyWindowGeometry inherits
        // the snap-in shader). Stale config overrides on those paths are
        // pruned by pruneShaderProfileTreeToSupportedPaths below.
        PP::WindowSnapIn,
        PP::WindowSnapOut,
        PP::WindowLayoutSwitch,
        // Full-screen virtual-desktop switch — consumed by the kwin-effect's
        // DesktopTransitionManager (resolveShaderWithDefault(tree,
        // DesktopSwitch) in the desktopChanged handler, lifecycle.cpp), NOT a
        // per-window tryBeginShaderForEvent leg. Its shaders are the two-texture
        // desktop class (appliesTo ["desktop"]).
        PP::DesktopSwitch,
    };
}

/// Single source of truth for "which event paths can run a per-event
/// transition shader". Includes every consumed leaf AND every ancestor
/// of a consumed leaf — setting the shader on an ancestor cascades to
/// its descendants via @c ShaderProfileTree::resolve's chain walk
/// (deeper-leaf-wins overlay merge), so users get the cascading
/// inheritance the tree is designed for: e.g. `popup = slide` applies
/// to every popup show/hide, and
/// `popup.layoutPicker.show = pixelate` overrides only that one leg.
///
/// Paths that are NOT ancestors of any consumed leaf (e.g.
/// `panel.slideIn`, `osd.pop`, `widget.fadeIn`, `editor.snapIn`) are
/// excluded — there is no resolver path that walks through them, so
/// any assignment would be runtime-dead and silently shadow what the
/// user thought they set on a sibling. The settings UI hides the
/// shader picker on those rows; @c Settings::shaderProfileTree's prune
/// drops any persisted entry on those paths to self-heal configs from
/// earlier app revisions.
inline QStringList shaderSupportedEventPaths()
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    QStringList out;
    QSet<QString> seen;
    const QStringList leaves = shaderConsumedLeafEventPaths();
    for (const QString& leaf : leaves) {
        QString cursor = leaf;
        while (!cursor.isEmpty()) {
            if (!seen.contains(cursor)) {
                seen.insert(cursor);
                out.append(cursor);
            }
            cursor = PP::parentPath(cursor);
        }
    }
    return out;
}

/// Convenience predicate used by the settings UI (Q_INVOKABLE-bridged)
/// and the daemon's optional verification path.
inline bool eventPathSupportsShaderLeg(const QString& path)
{
    static const QSet<QString> kSupported = []() {
        const QStringList list = shaderSupportedEventPaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return kSupported.contains(path);
}

/// Drop every per-path override from @p src whose path is NOT in
/// @c shaderSupportedEventPaths(). The baseline is preserved verbatim.
///
/// Why this exists: the settings UI gates its shader picker on
/// @c eventPathSupportsShaderLeg, but earlier UI revisions exposed the
/// picker on every event row and persisted overrides for paths the
/// daemon never consumed. Those entries now SHADOW user-intended parent
/// overrides at runtime (e.g. user sets `panel = slide` but a stale
/// `popup.zoneSelector.show = pixelate` leaf wins via the deeper-
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
