// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>
#include <QString>

#include <utility>

/**
 * @file untiledecisions.h
 * @brief The untile diff's pure decisions: whether a window a tile batch no
 *        longer carries for its screen is untiled there, and which outputs'
 *        batches predate a context switch the daemon has not announced yet.
 *        No KWin types, so the rows are unit tested; the caller reads the
 *        live window.
 */

namespace PlasmaZones::UntileDecisions {

/// Outputs whose context (desktop or activity) the effect has seen switch and
/// whose switch the daemon has not announced yet (F793, F213). A batch
/// processed for such an output was built for the context it left (the daemon
/// holds post-switch batches behind its announce), so it has no say over what
/// that output shows now. Keyed by physical screen id. edgeSeen records that
/// the effect saw a switch at all, on an output with a managed screen or not:
/// the announce for it then voids only the owed outputs' applies, and one with
/// no edge behind it (the daemon's bring-up re-announce) voids every output's.
struct SwitchAnnounceLedger
{
    QSet<QString> owed;
    bool edgeSeen = false;
    void noteEdge()
    {
        edgeSeen = true;
    }
    void noteSwitch(const QString& physicalId)
    {
        edgeSeen = true;
        owed.insert(physicalId);
    }
    bool isOwed(const QString& physicalId) const
    {
        return owed.contains(physicalId);
    }
    /// A switch announce landed: every owed output is settled and returned, and
    /// @p sawEdge (when given) says whether the effect saw a switch for it.
    QSet<QString> settle(bool* sawEdge = nullptr)
    {
        if (sawEdge) {
            *sawEdge = edgeSeen;
        }
        edgeSeen = false;
        return std::exchange(owed, {});
    }
};

/// A tile batch describes ONE (screen, desktop, activity) context, the one its
/// screen shows. A window the batch no longer carries is untiled on that screen
/// unless the batch has no say over it:
///  - it sits on a desktop the batch's OUTPUT is not showing (per-output
///    desktops, F211) or on another activity: a sibling context's retile
///    decides it (#808);
///  - the user is dragging it: the drag ends its tiling on its own terms, and
///    untiling it mid-drag strips its tiled chrome under the pointer (F185);
///  - it is moving off this screen: the daemon armed its output move from this
///    screen, so its destination's batch takes it over (F283).
/// A window that no longer resolves is untiled, so its tracking goes.
constexpr bool untilesOnBatchScreen(bool resolved, bool onDesktopShownOnBatchOutput, bool onCurrentActivity,
                                    bool beingDragged, bool movingOffThisScreen)
{
    if (!resolved) {
        return true;
    }
    return onDesktopShownOnBatchOutput && onCurrentActivity && !beingDragged && !movingOffThisScreen;
}

} // namespace PlasmaZones::UntileDecisions
