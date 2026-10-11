// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QList>
#include <QRect>
#include <QString>

#include <functional>

/// The instant snap-restore cache's entry and its pick rule, header-only so
/// the rule is unit-testable without a compositor (kwin-effect has no linkable
/// test target; same pattern as pretiledecisions.h). The KWin-facing teleport
/// stays in PlasmaZonesEffect::tryInstantSnapRestore.
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

} // namespace InstantRestoreDecisions
} // namespace PlasmaZones
