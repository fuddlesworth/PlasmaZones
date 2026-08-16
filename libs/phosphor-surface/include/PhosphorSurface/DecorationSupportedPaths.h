// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1Char>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PhosphorSurfaceShaders {

/// The root of the plasmashell surface subtree and its two leaves. Single
/// spellings for the `shell` family: the leaf list, the baseline-isolation
/// predicate, and the KWin effect's path producer all build from these, so
/// renaming the root cannot silently un-isolate the subtree while leaving the
/// paths decorable (which would fail OPEN — panels would start inheriting the
/// baseline).
inline QString decorationShellRootPath()
{
    return QStringLiteral("shell");
}
inline QString decorationShellPanelPath()
{
    return QStringLiteral("shell.panel");
}
inline QString decorationShellAppletPopupPath()
{
    return QStringLiteral("shell.appletPopup");
}

/// Leaf surface paths a per-surface decoration profile actually resolves
/// against. Each names a concrete surface PlasmaZones decorates: the three
/// window placement states (tiled / snapped / floating), the OSD, the
/// four transient popups, and the foreign plasma-shell surfaces under
/// `shell.*`. (The zone overlay is intentionally NOT a decoration target — it
/// is a fullscreen, mostly-transparent zone canvas drawn by the separate
/// overlay shader category, not a card to round/border.) When a future
/// surface gains a decoration leg, append its leaf path here in lockstep.
///
/// Surface-state analogue of `PlasmaZones::shaderConsumedLeafEventPaths()` for the
/// decoration concern: the SSOT for "which surfaces can carry a
/// DecorationProfile override".
inline QStringList decorationLeafSurfacePaths()
{
    return QStringList{
        // window.* — per-placement window decoration (border / corners /
        // titlebar appearance + surface-pack chain) for each placement state.
        QStringLiteral("window.tiled"),
        QStringLiteral("window.snapped"),
        QStringLiteral("window.floating"),
        // osd — the notification surface.
        QStringLiteral("osd"),
        // popup.* — the four transient overlays invoked by user action.
        QStringLiteral("popup.snapAssist"),
        QStringLiteral("popup.zoneSelector"),
        QStringLiteral("popup.layoutPicker"),
        QStringLiteral("popup.cheatsheet"),
        // shell.* — surfaces owned by plasmashell rather than by us or by an
        // application. Unlike every path above, these are FOREIGN windows the
        // KWin effect decorates in place. The subtree is baseline-isolated
        // (decorationPathIsBaselineIsolated below): a shell surface never
        // inherits the tree baseline, so it stays undecorated until a chain is
        // engaged at `shell` or one of its leaves — engaging that chain IS the
        // opt-in (applying a decoration set that carries shell paths engages
        // the chain the same way, and counts as the same opt-in: the user
        // chose to apply the set). It also resolves chain-only: the
        // config-backed border /
        // opacity-tint "easy mode" layers never apply to it, so a shell
        // surface is styled by an explicit pack chain here or not at all.
        decorationShellPanelPath(),
        decorationShellAppletPopupPath(),
    };
}

/// Walk @p path up one level in the dot-hierarchy ("window.floating" ->
/// "window" -> ""). Single source of truth for the decoration ancestor-walk,
/// shared by decorationSupportedSurfacePaths() below and
/// DecorationProfileTree::resolve(), so the two cannot drift. Unlike the
/// animation ProfilePaths::parentPath there is no synthetic "global" node: the
/// tree's baseline IS the global default, so the chain terminates at "".
inline QString decorationParentPath(const QString& path)
{
    if (path.isEmpty())
        return QString();
    const int dotIdx = path.lastIndexOf(QLatin1Char('.'));
    return (dotIdx < 0) ? QString() : path.left(dotIdx);
}

/// Single source of truth for "which surface paths can carry a decoration
/// profile". Includes every consumed leaf AND every ancestor of a consumed
/// leaf — setting the decoration on an ancestor cascades to its descendants
/// via @c DecorationProfileTree::resolve's chain walk (deeper-leaf-wins
/// overlay merge), so e.g. `window` styles every window placement and
/// `window.floating` overrides only the floating state.
///
/// Surface-state analogue of `PlasmaZones::shaderSupportedEventPaths()`. The
/// de-duplicated result is consumed as a set (see decorationSurfaceSupported);
/// no consumer relies on its order.
inline QStringList decorationSupportedSurfacePaths()
{
    QStringList out;
    QSet<QString> seen;
    const QStringList leaves = decorationLeafSurfacePaths();
    for (const QString& leaf : leaves) {
        QString cursor = leaf;
        while (!cursor.isEmpty()) {
            if (!seen.contains(cursor)) {
                seen.insert(cursor);
                out.append(cursor);
            }
            cursor = decorationParentPath(cursor);
        }
    }
    return out;
}

/// Roots whose subtree does NOT inherit the tree baseline. The `shell.*`
/// family decorates FOREIGN plasmashell windows, and inheriting the baseline
/// there would border every panel the instant the user configured a global
/// decoration chain for their own windows — so a shell surface resolves only
/// from overrides inside its own subtree, and an unconfigured shell surface
/// resolves empty (undecorated). Consulted by DecorationProfileTree::resolve
/// and withSeedDefaults so the two cannot disagree.
inline bool decorationPathIsBaselineIsolated(const QString& path)
{
    const QString root = decorationShellRootPath();
    return path == root || path.startsWith(root + QLatin1Char('.'));
}

/// Convenience predicate: does @p path name a surface that can carry a
/// decoration profile (a leaf or an ancestor of one)?
///
/// Surface-state analogue of `PlasmaZones::eventPathSupportsShaderLeg()`.
inline bool decorationSurfaceSupported(const QString& path)
{
    static const QSet<QString> kSupported = []() {
        const QStringList list = decorationSupportedSurfacePaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return kSupported.contains(path);
}

} // namespace PhosphorSurfaceShaders
