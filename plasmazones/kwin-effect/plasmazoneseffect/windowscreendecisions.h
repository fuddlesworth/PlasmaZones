// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/// Which virtual screen a window that fills its split output belongs to,
/// header-only so the rule is unit-testable without a compositor (same pattern
/// as pretiledecisions.h). The KWin-facing reads are in screens_fill.cpp.
namespace PlasmaZones::WindowScreenDecisions {

/// A window that fills its output on at least one axis (maximized, or
/// fullscreen, committed or requested) on a split output. Its frame centre is
/// the output's centre on every axis it fills, which names one virtual screen
/// for every such window, so its screen comes from elsewhere.
struct FillingWindow
{
    QString axisScreen; ///< a window maximized on one axis: the virtual screen at axisMaximizedProbe
    QString engineScreen; ///< a tiling-tracked window's engine screen
    QString restoreScreen; ///< the virtual screen holding its restore rect's centre, on the same output
    QString trackedScreen; ///< the effect's last stamp, a tie-break
    QStringList outputScreens; ///< the live virtual screens of the output
};

/// Where a window maximized on one axis only is read (F1057). KWin keeps an
/// axis maximize through a move along the free axis and leaves the restore
/// rect where it was, so the free axis comes from the frame. On the maximized
/// axis the frame spans the output, and KWin ends the maximize on any move
/// along it, so the restore rect's coordinate there is still the window's,
/// clamped into @p output.
inline QPointF axisMaximizedProbe(const QRectF& frame, const QRectF& restore, const QRectF& output,
                                  bool verticallyMaximized)
{
    QPointF probe = frame.center();
    if (verticallyMaximized) {
        probe.setY(qBound(output.top(), restore.center().y(), output.bottom() - 1));
    } else {
        probe.setX(qBound(output.left(), restore.center().x(), output.right() - 1));
    }
    return probe;
}

/// The first of the axis, engine, restore and tracked screens that is a live
/// virtual screen of the output, or empty: the caller then keeps the
/// positional answer, so a window that maps maximized with no restore rect
/// still resolves, and a dead id is never answered. The axis screen leads: the
/// frame places a window maximized on one axis along the other, even a tile
/// the engine still counts where it was.
inline QString resolveFilling(const FillingWindow& in)
{
    for (const QString* id : {&in.axisScreen, &in.engineScreen, &in.restoreScreen, &in.trackedScreen}) {
        if (!id->isEmpty() && in.outputScreens.contains(*id)) {
            return *id;
        }
    }
    return {};
}

} // namespace PlasmaZones::WindowScreenDecisions
