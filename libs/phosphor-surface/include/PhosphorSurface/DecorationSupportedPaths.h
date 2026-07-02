// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace PhosphorSurfaceShaders {

/// Leaf surface paths a per-surface decoration profile actually resolves
/// against. Each names a concrete surface the shell decorates: the three
/// window placement states (tiled / snapped / floating), the OSD, and the
/// three transient popups. (The zone overlay is intentionally NOT a
/// decoration target — it is a fullscreen, mostly-transparent zone canvas
/// drawn by the separate overlay shader category, not a card to round/border.)
/// When a future surface gains a decoration leg, append its leaf path here in
/// lockstep.
///
/// Mirror of `PlasmaZones::shaderConsumedLeafEventPaths()` for the
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
        // popup.* — transient overlays invoked by user action.
        QStringLiteral("popup.snapAssist"),
        QStringLiteral("popup.zoneSelector"),
        QStringLiteral("popup.layoutPicker"),
    };
}

/// Single source of truth for "which surface paths can carry a decoration
/// profile". Includes every consumed leaf AND every ancestor of a consumed
/// leaf — setting the decoration on an ancestor cascades to its descendants
/// via @c DecorationProfileTree::resolve's chain walk (deeper-leaf-wins
/// overlay merge), so e.g. `window` styles every window placement and
/// `window.floating` overrides only the floating state.
///
/// Mirror of `PlasmaZones::shaderSupportedEventPaths()`: leaves + ancestors,
/// insertion-ordered, de-duplicated.
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
            const int dotIdx = cursor.lastIndexOf(QLatin1Char('.'));
            cursor = (dotIdx < 0) ? QString() : cursor.left(dotIdx);
        }
    }
    return out;
}

/// Convenience predicate: does @p path name a surface that can carry a
/// decoration profile (a leaf or an ancestor of one)?
///
/// Mirror of `PlasmaZones::eventPathSupportsShaderLeg()`.
inline bool decorationSurfaceSupported(const QString& path)
{
    static const QSet<QString> kSupported = []() {
        const QStringList list = decorationSupportedSurfacePaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return kSupported.contains(path);
}

} // namespace PhosphorSurfaceShaders
