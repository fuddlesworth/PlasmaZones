// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// The per-page scope of an animation leaf. Every animation leaf shares the
/// single AnimationsPageController staging domain AND the single
/// ShaderProfileTree key, but Reset/Discard/dirty are NOT whole-tree: each
/// surface leaf (windows/osds/overlays/desktops/motion/dragging/panels/
/// widgets/editor) owns one event-path root subtree, General owns only the
/// config keys, and the presets / motion sets / shaders library leaves act on the whole
/// editable tree. Scoping keeps a Reset on one surface from wiping the
/// others (mirrors the decoration domain — see decorationpagescope.h).
struct AnimationPageScope
{
    enum Kind {
        EventSubtree,
        ConfigOnly,
        WholeTree
    };
    Kind kind = WholeTree;
    /// For EventSubtree: a path is in scope iff it is under an `include`
    /// root AND not under an `exclude` root. window.movement (Motion)
    /// EXCLUDES window.movement.move, which the Dragging page owns — the
    /// one carve-out.
    QStringList include;
    QStringList exclude;
    /// For EventSubtree: also act on the General config keys, for a page that
    /// hosts the global timing and window-filter cards alongside its event
    /// cards (the condensed simple page). Ignored by ConfigOnly, which is
    /// already keys-only, and by WholeTree, which already covers every key.
    bool includeGeneralKeys = false;
};

/// Scope for @p page. Unknown / library leaves fall back to WholeTree.
AnimationPageScope animationPageScope(const QString& page);

/// True iff @p path is @p root or a descendant of it, for any root in
/// @p roots.
bool animationPathUnderAny(const QString& path, const QStringList& roots);

/// True iff @p path falls inside an EventSubtree scope (under an include
/// root, clear of every exclude root).
bool animationPathInScope(const QString& path, const AnimationPageScope& scope);

/// Built-in event paths that fall inside @p scope — the file-backed paths a
/// surface leaf's Reset/Discard/dirty acts on.
///
/// PRECONDITION: EventSubtree scopes only. Both this and shaderTreeScopeDiffers
/// funnel through animationPathInScope, which requires a non-empty `include`,
/// so a WholeTree or ConfigOnly scope yields the EMPTY set here and "no
/// difference" below — NOT "everything", which is what WholeTree's name
/// suggests. Every caller pre-branches on scope.kind, which is why that is
/// safe today; call these only on an EventSubtree scope.
QStringList animationScopedBuiltInPaths(const AnimationPageScope& scope);

/// True iff the two shader trees' overrides differ anywhere inside @p scope.
/// Same EventSubtree-only precondition as animationScopedBuiltInPaths above.
bool shaderTreeScopeDiffers(const PhosphorAnimationShaders::ShaderProfileTree& current,
                            const PhosphorAnimationShaders::ShaderProfileTree& baseline,
                            const AnimationPageScope& scope);

} // namespace PlasmaZones
