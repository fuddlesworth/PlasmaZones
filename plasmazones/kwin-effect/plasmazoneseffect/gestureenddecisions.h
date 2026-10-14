// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QtGlobal>

/// The end of an interactive resize, what a frame change crossed, how a drag's
/// held crossing settles and what an output settle reports as KWin's,
/// header-only so the rules are unit-testable without a compositor (same
/// pattern as pretiledecisions.h). The resize hold and its drain are in
/// window_moveresize_connections.cpp, the drag crossing hold and the settle in
/// screenchangehandler_settle.cpp.
namespace PlasmaZones::GestureEndDecisions {

/// The client has committed the size KWin asked for at the end of a resize:
/// within a pixel on each axis, because the request is fractional while the
/// committed frame is snapped to pixels.
inline bool frameAnswersRequest(const QSizeF& frame, const QSizeF& requested)
{
    return qAbs(frame.width() - requested.width()) <= 1.0 && qAbs(frame.height() - requested.height()) <= 1.0;
}

enum class Crossing {
    None,
    Output,
    VirtualScreen
};

/// What a held resize crossed, from the screen it started on to the one it
/// ended on: nothing when either id is unknown or both are the same, a
/// virtual-screen crossing when both are on one monitor, else an output
/// crossing.
inline Crossing classify(const QString& before, const QString& after)
{
    if (before.isEmpty() || after.isEmpty() || before == after) {
        return Crossing::None;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(before, after)) {
        return Crossing::VirtualScreen;
    }
    if (PhosphorIdentity::VirtualScreenId::samePhysical(before, after)) {
        return Crossing::None;
    }
    return Crossing::Output;
}

/// The neighbour-reflow report (#652) describes a resize on the screen it
/// started on, so a resize that ended on another screen sends none: the
/// window no longer belongs to the tiles it would reflow.
inline bool reportsResize(Crossing crossing)
{
    return crossing == Crossing::None;
}

/// A frame change that kept the window's top-left (within a pixel) and
/// changed only its size: a client resizing itself, or acking a size its
/// placement did not ask for. It moves nothing, so it crosses no screen
/// (F248). A frame that also moved is judged by its centre as before.
inline bool isSizeOnlyChange(const QRectF& before, const QRectF& after)
{
    return (before.topLeft() - after.topLeft()).manhattanLength() <= 1.0 && before.size() != after.size();
}

/// The rect a size restored during a move takes so the grab point stays at
/// the same fraction of the window, as KWin's own restore-on-drag places it
/// (F702). The fraction is read from @p frame and clamped into it, so a grab
/// point outside the frame pins to the nearest edge. An empty frame keeps its
/// top-left.
inline QRect frameUnderGrab(const QRectF& frame, const QPointF& grab, const QSize& size)
{
    if (frame.width() <= 0 || frame.height() <= 0) {
        return QRect(QPoint(qRound(frame.x()), qRound(frame.y())), size);
    }
    const qreal fx = qBound(0.0, (grab.x() - frame.x()) / frame.width(), 1.0);
    const qreal fy = qBound(0.0, (grab.y() - frame.y()) / frame.height(), 1.0);
    return QRect(QPoint(qRound(grab.x() - fx * size.width()), qRound(grab.y() - fy * size.height())), size);
}

/// A move that finished without a pointer release was reverted by KWin (the
/// Escape of a keyboard or pointer move puts the window back at its start
/// rect) when the rect it ends on is the one it started on. Integer-aligned:
/// a fractional-scale frame carries sub-pixel residue. A move that ends
/// exactly where it began reads as reverted, which changes nothing (F752).
/// A move that started fully maximized is reverted when it ends fully
/// maximized again (KWin's MaximizeMode bits, 3 for full): any move restores
/// it, and the cancel re-applies the starting mode after putting the window
/// back, which commands the maximize area rather than a start rect PlasmaZones
/// inset from it, a Monocle tile's gaps (F1014). Not an axis maximize: KWin
/// keeps that one through a move along the free axis, so an accepted move ends
/// in it too, and its cancel puts the start rect back (F1057). Only on the
/// output it started on, where the cancel puts it back: a keyboard or touch
/// move that ends maximized on another output was maximized there by an
/// electric edge, and keeps it (F1072).
inline bool moveReverted(const QRectF& start, const QRectF& commandedAtEnd, int startMaximizeMode, int endMaximizeMode,
                         bool endsOnStartOutput)
{
    constexpr int fullMaximize = 3;
    return start.toRect() == commandedAtEnd.toRect()
        || (startMaximizeMode == fullMaximize && endMaximizeMode == fullMaximize && endsOnStartOutput);
}

/// The grab point of a drag that left the pointer idle (touch, tablet),
/// carried with the window: where it was grabbed plus how far the window
/// has moved since (F774).
inline QPointF grabPointAt(const QPointF& anchor, const QPointF& startTopLeft, const QPointF& nowTopLeft)
{
    return anchor + (nowTopLeft - startTopLeft);
}

/// What a drop's answer did with the dragged window, for the crossing it held.
enum class DropAnswer {
    Floated, ///< ApplyFloat: the effect floats it where it was dropped
    DaemonPlaced, ///< NoOp or NotifyDragOutUnsnap: the daemon placed it itself (a drag-insert, a plain drop)
    Snapped, ///< ApplySnap or RestoreSize: the snap drop owns the move
    Unanswered, ///< CancelSnap, an error, the timeout, or no daemon
};

/// How a dragged window's held crossing settles at its drop.
enum class DragCrossingSettle {
    RunBody, ///< the tiling transfer a crossing runs: release, then announce on the new screen
    RunBodyAndReport, ///< the same, and the crossing is reported, since no answer covered it
    TrackOnly, ///< the transfer without the announce: the drop is its placement there
    ReKeyOnly, ///< only the effect's screen record moves
};

/// A dragged window's crossings wait for its drop and settle once by what the
/// drop did, so a tiling monitor the pointer passes over is not rearranged
/// mid-drag (F713, F731). A float is tracked where it lands and never
/// announced there, which would tile it (l18_plain_drop_on_tiling). A window
/// the daemon placed on a tiling screen is re-keyed when the effect tracks it
/// and tracked when it does not, with no release relay, which would delete the
/// tile the daemon just put there (F753). A snap drop's own release covers the
/// screen left, and a drop with no answer runs the crossing as it used to run
/// mid-drag, once, and reports it.
inline constexpr DragCrossingSettle settleDragCrossing(DropAnswer answer, bool tracked, bool landedManaged)
{
    switch (answer) {
    case DropAnswer::Floated:
        return DragCrossingSettle::TrackOnly;
    case DropAnswer::DaemonPlaced:
        if (landedManaged) {
            return tracked ? DragCrossingSettle::ReKeyOnly : DragCrossingSettle::TrackOnly;
        }
        return DragCrossingSettle::RunBody;
    case DropAnswer::Snapped:
        return DragCrossingSettle::RunBody;
    case DropAnswer::Unanswered:
        return DragCrossingSettle::RunBodyAndReport;
    }
    return DragCrossingSettle::RunBody;
}

/// How a cancelled engine drag's tile is put back (F1015).
enum class CancelReassert {
    None,
    Retile, ///< Tiling.retile for the tile's screen: the autotile engine re-emits every tile there
    ReapplyWindow, ///< Scrolling.reapplyWindowGeometry: the strip emits on change, so the window's own re-emit
};

/// A cancelled drag of a tile the effect floated at the drag's start (Float
/// mode: handleDragToFloat shed its tiled marks, centring target and claims)
/// leaves the daemon still tiling it, and KWin puts it back as it started,
/// which for a maximized tile is the maximize area rather than its gap-inset
/// rect. Nothing else undoes that float, so the daemon is asked to re-emit the
/// tile, whose batch re-applies all of it. A tile a snapping screen suspended
/// mid-drag (suspendTileForSnapDrag) shed the same marks, and the daemon's
/// cancel re-emits nothing for it either (F1058). Nothing for a drop that was
/// not cancelled, a window whose tile state the drag left alone (it was
/// floating already, or Reorder mode kept it tiled on its screen), or one the
/// effect does not track.
inline constexpr CancelReassert cancelledDragReassert(bool cancelled, bool floatedAtDragStart, bool suspendedForSnap,
                                                      bool tracked, bool scrolling)
{
    if (!cancelled || !(floatedAtDragStart || suspendedForSnap) || !tracked) {
        return CancelReassert::None;
    }
    return scrolling ? CancelReassert::ReapplyWindow : CancelReassert::Retile;
}

/// Whether only KWin moved a window an output change settles
/// (OutputSettleRow::kwinOnly): no interactive move or resize since the
/// baseline, not the window being dragged, and no output change outside KWin's
/// output-update cascade, either one a later cascade found at its start
/// (@p movedBetweenCascades) or one since the last cascade ended
/// (@p cascadeOutput, empty when no cascade sampled the window, against
/// @p outputNow). A Move to Screen from the keyboard or the window menu is the
/// user's and moves no count (F1009).
inline bool settleRowKwinOnly(int baseMoveResizeCount, int moveResizeCount, bool dragged, bool movedBetweenCascades,
                              const QString& cascadeOutput, const QString& outputNow)
{
    return moveResizeCount == baseMoveResizeCount && !dragged && !movedBetweenCascades
        && (cascadeOutput.isEmpty() || cascadeOutput == outputNow);
}

/// How long the end-of-resize reports wait for the client to commit the size
/// it was dragged to (F701): long enough for a slow first frame at 60 Hz,
/// short enough that a client that never answers does not hold the window.
inline constexpr int kAckDeadlineMs = 250;

} // namespace PlasmaZones::GestureEndDecisions
