// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

// Why one edit to a window's desktop or activity set waits (EE). KWin re-desks
// a window whose output changes (finishInteractiveOutputChange), and the edit
// can arrive before the move that caused it has settled: mid-drag, before a
// size-changing Move to Screen is acked, or while the crossing waits for a
// screen-change settle. A held edit pushes the context the window had and runs
// once at the release, after the crossing notice, so no daemon reconcile carries
// the window into a context nobody is looking at mid-move. Kept free of KWin
// types so it is unit-testable on its own; PlasmaZonesEffect::holdContextEdit
// reads the inputs off the window.
namespace PlasmaZones::WindowContextHold {

/// One bit per condition; a hold stands until every bit it gathered is released.
enum Reason : quint8 {
    DragSubject = 1, ///< the drag's subject, until its drop's outcome is applied
    OutputInFlight = 2, ///< KWin moved it to an output the client has not committed to yet
    SettlePending = 4, ///< its crossing waits for the screen-change settle
};

struct Inputs
{
    bool dragLatched = false; ///< a drag subject whose drop is not settled yet
    bool outputInFlight = false; ///< moveResizeOutput() differs from output()
    /// In view on its COMMITTED output by the pre-edit stamp, on both axes.
    bool inViewByStamp = false;
    bool crossingAwaitsSettle = false; ///< ScreenChangeHandler::crossingAwaitsSettle
};

/// The reasons an edit made now would be held for (0: run it at once).
inline quint8 reasonsFor(const Inputs& in)
{
    quint8 reasons = 0;
    if (in.dragLatched) {
        reasons |= DragSubject;
    }
    // A visible window is held until KWin commits the move (F650). One already
    // hidden when the move began is not: an off-desktop client never acks a
    // grow, so nothing but the deadline would release it (F711, F687).
    if (in.outputInFlight && in.inViewByStamp) {
        reasons |= OutputInFlight;
    }
    if (in.crossingAwaitsSettle) {
        reasons |= SettlePending;
    }
    return reasons;
}

} // namespace PlasmaZones::WindowContextHold
