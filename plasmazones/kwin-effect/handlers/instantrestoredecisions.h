// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QList>
#include <QRect>
#include <QSizeF>
#include <QString>

#include <functional>

/// The instant snap-restore cache's entry, its pick rule and the open
/// suppression's settle rule, header-only so the rules are unit-testable
/// without a compositor (kwin-effect has no linkable test target; same pattern
/// as pretiledecisions.h). The KWin-facing teleport stays in
/// PlasmaZonesEffect::tryInstantSnapRestore.
namespace PlasmaZones {

/// A pre-computed snap restore target for a pending app: the record's zone
/// geometry and the screen it was saved on. Fetched once from the daemon on
/// ready (newest record first per app) and used by the deferred-route dispatch
/// for a flash-free teleport ahead of the async resolve. Only ever applied on
/// the saved screen's own output: restores happen where the window opens.
struct CachedSnapRestore
{
    QRect geometry;
    QString screenId;
};

namespace InstantRestoreDecisions {

/// Index of the entry an opener may apply, or -1. @p newestFirst is one app's
/// cache list; @p openerPhysicalId is the physical id of the output the window
/// opened on; @p openerManaged says its screen is engine-managed (tiling), and
/// @p isManagedScreen answers that for a saved screen.
///
/// Nothing applies for a managed opener (its engine places it) or an unknown
/// output. Otherwise the NEWEST entry with a valid geometry whose saved screen
/// is on the opener's output and is not engine-managed wins: an entry on
/// another output stays for an opener there, and the async resolve, which
/// claims the same newest record on this output, confirms it.
inline int pickEntry(const QList<CachedSnapRestore>& newestFirst, const QString& openerPhysicalId, bool openerManaged,
                     const std::function<bool(const QString&)>& isManagedScreen)
{
    if (openerManaged || openerPhysicalId.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < newestFirst.size(); ++i) {
        const CachedSnapRestore& entry = newestFirst.at(i);
        if (!entry.geometry.isValid() || entry.screenId.isEmpty()) {
            continue;
        }
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(entry.screenId) != openerPhysicalId) {
            continue;
        }
        if (isManagedScreen && isManagedScreen(entry.screenId)) {
            continue;
        }
        return i;
    }
    return -1;
}

/// Whether a window withheld on open has landed: its frame left the spawn
/// position, or changed size to the one it was sent to (±1 px, fractional
/// scale). A client that commits a size of its own first has done neither, and
/// a frame change that leaves the spawn rect as it was is not a landing (F759).
inline bool suppressionSettled(const QRect& spawn, const QRect& target, const QRect& live)
{
    if (live.topLeft() != spawn.topLeft()) {
        return true;
    }
    return live.size() != spawn.size() && qAbs(live.width() - target.width()) <= 1
        && qAbs(live.height() - target.height()) <= 1;
}

/// Whether an opener of @p frame size is under the minimum window size
/// (@p minWidth / @p minHeight, 0 for no limit on that axis). Such a window
/// takes no cached restore: the snap engine excludes it from every remembered
/// placement, and the teleport would put a splash or popup into the zone of
/// its app's last window ahead of the resolve that refuses it (F735).
inline bool belowMinimumSize(const QSizeF& frame, int minWidth, int minHeight)
{
    return (minWidth > 0 && frame.width() < minWidth) || (minHeight > 0 && frame.height() < minHeight);
}

} // namespace InstantRestoreDecisions
} // namespace PlasmaZones
