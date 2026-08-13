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
/// actually resolve a shader effect for, by one of three mechanisms:
/// a @c resolveShaderLeg(tree, ...) call inside one of
/// the @c build*Config factories in @c src/daemon/overlayservice/animation_config.cpp
/// (@c buildOsdConfig / @c buildLayoutPickerConfig /
/// @c buildZoneSelectorConfig / @c buildSnapAssistConfig /
/// @c buildCheatsheetConfig; a
/// @c tryBeginShaderForEvent(...) call (window_lifecycle for the open, close
/// and focus legs, window_connections for move and maximize, daemon_apply for
/// minimize — all under @c kwin-effect/plasmazoneseffect/ — and
/// @c kwin-effect/tilinghandler/tiling.cpp for the tab-switch leg); or a
/// @c resolveShaderWithDefault(tree, ...) call, which drives the
/// scrolling strip's view pass from the tiling batch path
/// (@c kwin-effect/tilinghandler/tiling.cpp), the
/// screen-level desktop legs from
/// @c kwin-effect/plasmazoneseffect/lifecycle_wiring.cpp, the snap geometry
/// legs through @c applyWindowGeometry in
/// @c kwin-effect/plasmazoneseffect/drag_snap.cpp, and the read-only
/// @c packOwnsEvent predicate inside @c syncStockEffectSuppression
/// (@c kwin-effect/plasmazoneseffect/lifecycle.cpp), which resolves the
/// DesktopPeek / WindowMinimize / WindowMaximize paths already listed
/// below. When a future surface adds a shader leg, append its leg paths
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
        PP::PopupCheatsheetShow,
        PP::PopupCheatsheetHide,
        //
        // Window family — driven by the KWin OffscreenEffect under
        // kwin-effect/plasmazoneseffect/ via tryBeginShaderForEvent
        // on each window-lifecycle hook: windowAdded/windowClosed for
        // open/close, windowStartUserMovedResized for the held move,
        // and windowMaximizedStateChanged/minimizedChanged/
        // windowActivated for maximize/minimize/focus.
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
        // applyWindowGeometry chokepoint (drag_snap.cpp), which resolves
        // through resolveShaderWithDefault rather than
        // tryBeginShaderForEvent, applying the same rule-then-tree cascade
        // so the user can pick a distinct shader per snap event.
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
        // DesktopSwitch) in the desktopChanged handler, lifecycle_wiring.cpp), NOT a
        // per-window tryBeginShaderForEvent leg. Its shaders are the two-texture
        // desktop class (appliesTo ["desktop"]).
        PP::DesktopSwitch,
        // Strip scroll — consumed by the kwin-effect's StripTransitionManager:
        // the tiling batch path resolves resolveShaderWithDefault(tree,
        // ScrollingView) beside its motion-profile resolve (tilinghandler/
        // tiling.cpp) and arms a per-output post-process pass over the live
        // scene capture while the view spring is in flight. Its shaders are
        // the one-scene strip class (appliesTo ["strip"]).
        PP::ScrollingView,
        // Tab swap inside a tabbed column — a per-window leg, unlike its
        // scrolling sibling above: the tiling batch path installs it through
        // tryBeginShaderForEvent on the ARRIVING tab and seeds uOldWindow from
        // a capture of the outgoing one (tilinghandler/tiling.cpp), so the two
        // tabs cross-fade instead of hard-cutting. Its shaders are the opt-in
        // two-texture tab class (appliesTo ["tab"]), which resolves in
        // isolation — see shaderPathResolvesInIsolation.
        PP::ScrollingTabSwitch,
        // Show-desktop peek — DesktopTransitionManager again (the entry two
        // above; the strip block in between has its own manager), resolved in
        // the showingDesktopChanged handler (lifecycle_wiring.cpp). One node drives both
        // legs: the hide leg blends the windows scene into the bare desktop,
        // and the show-back leg replays that same blend with time reversed
        // (its bare-desktop endpoint comes from the hide leg's cache).
        PP::DesktopPeek,
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
/// drops any persisted entry on those paths, so a config from an earlier app
/// revision can never SERVE one (the entry may still sit in the file until an
/// unrelated edit rewrites it).
inline const QStringList& shaderSupportedEventPaths()
{
    // Function-local static, mirroring supportedShaderPathSet() below: the
    // taxonomy is process-constant, and the pruner calls this on EVERY
    // Settings tree read and write (several per mutation at slider-drag
    // rate), so rebuilding the list plus the dedup set per call was pure
    // waste. Range-for callers bind to the reference unchanged.
    static const QStringList kPaths = []() {
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
    }();
    return kPaths;
}

/// The supported-path set as a QSet, built once. Backs the predicate below;
/// built from the same SSOT list the pruner iterates, so the two membership
/// sources cannot drift.
inline const QSet<QString>& supportedShaderPathSet()
{
    static const QSet<QString> kSupported = []() {
        const QStringList list = shaderSupportedEventPaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return kSupported;
}

/// Convenience predicate used by the settings UI (Q_INVOKABLE-bridged).
/// The only callers today are the two in
/// `animationspagecontroller_shaders.cpp`.
inline bool eventPathSupportsShaderLeg(const QString& path)
{
    return supportedShaderPathSet().contains(path);
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
/// layer means an affected config can never SERVE a stale entry (the read prunes
/// it), though the entry itself lingers in the file until an unrelated edit
/// forces a write — the write-side compare happens after pruning on both
/// sides, so a prune-only delta is not itself a reason to write. And a
/// fresh write coming from a Q_INVOKABLE that bypasses the UI gate
/// (e.g. a future scripting hook) still cannot stamp unsupported-path
/// entries onto disk.
inline PhosphorAnimationShaders::ShaderProfileTree
pruneShaderProfileTreeToSupportedPaths(const PhosphorAnimationShaders::ShaderProfileTree& src)
{
    // No membership SET needed: iterating the (memoised) SSOT list is itself
    // the filter, and `supportedShaderPathSet()` is built from that same
    // list, so the two cannot disagree.
    PhosphorAnimationShaders::ShaderProfileTree pruned;
    // The baseline is a single global ShaderProfile (the "global" root default),
    // not one of the path-keyed overrides this filter operates on, so it is
    // copied through verbatim by design. The supported-path prune decides which
    // OVERRIDE PATHS survive; the root default always applies and has no path to
    // filter against.
    pruned.setBaseline(src.baseline());
    // Emitted in the SSOT's own order, not the source tree's insertion order, so
    // this pruner CANONICALISES as well as filters.
    //
    // `ShaderProfileTree::operator==` compares insertion order, but order carries
    // no meaning for this property — `resolve()` ignores it entirely. Preserving
    // the caller's order therefore made two value-identical trees compare unequal
    // whenever a user toggled an override off and back on, because the re-added
    // path appends rather than returning to its old position. The consequences were
    // all user-visible: `hasPendingChanges()` stayed true forever, the per-page
    // Discard could not clear it (it is value-based, finds every value already
    // equal, and never writes), and the no-op write guard fired a tree-changed
    // signal for an order-only delta. Since this runs on BOTH the read and the
    // write side, canonicalising here fixes the dirty check, the no-op guard, and
    // the persisted JSON in one place.
    for (const QString& path : shaderSupportedEventPaths()) {
        if (src.hasOverride(path)) {
            pruned.setOverride(path, src.directOverride(path));
        }
    }
    return pruned;
}

} // namespace PlasmaZones
