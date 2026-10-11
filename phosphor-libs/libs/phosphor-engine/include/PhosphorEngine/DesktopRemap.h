// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>

#include <QList>
#include <QMetaType>
#include <QStringList>

namespace PhosphorEngine {

/// How KWin's desktop positions moved in one change of its desktop list: for
/// each 1-based position in the OLD list, the position the same desktop holds
/// in the NEW list, or 0 when it was removed.
///
/// Every store keyed by desktop NUMBER (the engines' per-desktop states, the
/// window registry, the context rules, the disabled-desktop gates) takes the
/// same map, so a removal, an insert or a reorder moves them all alike
/// (orchestrator_phaseb_1, F763, F787). Built by diffing the desktop manager's
/// id lists, which are stable across a renumber where positions are not.
///
/// A number outside the old list (0, the "every desktop" value several stores
/// use, or one already stale) is never this map's to move: map() and
/// successor() hand it back unchanged. A default-constructed map is therefore
/// the identity.
class PHOSPHORENGINE_EXPORT DesktopRemap
{
public:
    DesktopRemap() = default;

    /// The map from @p oldIds to @p newIds, both in position order. A desktop
    /// is matched by id, so a rename alone is the identity. Either list empty
    /// gives the identity (nothing to compare against). Lists that share no id
    /// at all are read as KWin re-creating its desktops, not as every desktop
    /// removed: the positions are kept, and those past the new count go.
    static DesktopRemap fromIds(const QStringList& oldIds, const QStringList& newIds);
    /// The map for exactly one desktop removed at @p position out of
    /// @p oldCount. The identity when @p position is not in 1..oldCount.
    static DesktopRemap removal(int position, int oldCount);

    /// True when no old position moves or goes. Desktops appended after the
    /// last one leave it true: no stored number changes.
    bool isIdentity() const;
    int oldCount() const;
    int newCount() const;

    /// The new position of the desktop at @p oldPosition, or 0 when it was
    /// removed. A position outside the old list comes back unchanged.
    int map(int oldPosition) const;
    /// Like map(), except that a removed desktop answers the desktop KWin moves
    /// its windows and outputs to: the next surviving desktop after it, which
    /// takes its number, or the last one before it when it was the last. 0
    /// only when nothing survived (newCount() is 0).
    int successor(int oldPosition) const;
    /// The old position when this change is exactly one desktop removed with
    /// every other keeping its order, else 0.
    int singleRemoval() const;

    bool operator==(const DesktopRemap& other) const = default;

private:
    /// Index i holds the new position of old position i + 1, or 0.
    QList<int> m_newPositions;
    int m_newCount = 0;
};

} // namespace PhosphorEngine

Q_DECLARE_METATYPE(PhosphorEngine::DesktopRemap)
