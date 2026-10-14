// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QSet>
#include <QString>

#include <functional>

namespace PlasmaZones {

/// The daemon's evacuee ledger. When an output disconnects, KWin moves its
/// windows to another one and each engine parks what it held of them there
/// (IPlacementEngine's evacuee park). This keeps what classifies a window
/// when the output comes back: which output it was parked for, and the KWin
/// state it had before the output went away (S0). A window KWin returns with
/// that state unchanged is re-seated from the park; one the user touched is
/// not. Session-scoped: a daemon restart loses it, and the user-move rule
/// then applies.
struct EvacueeLedger
{
    struct Entry
    {
        /// The PlasmaZones physical id of the output the window was parked for.
        QString physicalId;
        /// KWin's uuid of that output, once a settle row carried it.
        QString outputUuid;
        /// S0: the window's KWin state when the output went away.
        bool hasS0 = false;
        int moveResizeCount = 0;
        int maximizeMode = 0;
        int quickTileMode = 0;
        bool fullscreen = false;
        /// Snap held the window floating there when the output went away. Read
        /// at the park, since the removed-screen prune drops that float (F670).
        bool snapFloating = false;
    };

    /// By canonical window id: one entry per output the window is parked for.
    QHash<QString, QList<Entry>> entries;
    /// Windows a PlasmaZones verb placed while an output they were parked for
    /// was away, with the physical ids of those outputs. KWin may pull them
    /// back onto one when it returns, and the settle then re-asserts where
    /// PlasmaZones put them. Dropped once every output it names is back and a
    /// settle was read, and on close (F1049).
    QHash<QString, QStringList> placedAfterEvacuation;
    /// A float an evacuee could not take when its output went away: where it
    /// landed, and which mode's float it is.
    struct PendingFloat
    {
        QString screenId;
        bool snap = false; ///< a snap float, else a tiling screen's
    };
    /// Evacuees that could not be adopted floating when their output went away
    /// (minimized, or on a desktop or activity out of view): their first
    /// arrival on the output they landed on, in the same mode, adopts them
    /// floating instead of placing them (F643, F1047).
    QHash<QString, PendingFloat> evacueeFloatPending;
    /// Set while the ledger itself places a window, so the engines' float
    /// announcements from that placement do not read as the user's touch.
    bool adoptInProgress = false;
    /// The daemon's retire primitive, for a settle that reports an output
    /// gone before the daemon's own screen-removed handler ran.
    std::function<void(const QString&)> retirer;
    /// The touch watches on the engines, dropped on every rewire.
    QList<QMetaObject::Connection> touchConnections;
};

} // namespace PlasmaZones
