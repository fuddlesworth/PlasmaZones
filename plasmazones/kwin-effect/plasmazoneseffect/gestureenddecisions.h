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

/// The end of an interactive resize, and what a frame change crossed,
/// header-only so the rules are unit-testable without a compositor (same
/// pattern as pretiledecisions.h). The hold itself, and the drain that applies
/// these, are in window_moveresize_connections.cpp.
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

/// How long the end-of-resize reports wait for the client to commit the size
/// it was dragged to (F701): long enough for a slow first frame at 60 Hz,
/// short enough that a client that never answers does not hold the window.
inline constexpr int kAckDeadlineMs = 250;

} // namespace PlasmaZones::GestureEndDecisions
