// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/// Which virtual screen a window that fills its split output belongs to,
/// header-only so the rule is unit-testable without a compositor (same pattern
/// as pretiledecisions.h). The KWin-facing reads are in screens_fill.cpp.
namespace PlasmaZones::WindowScreenDecisions {

/// A window that fills its output (maximized on any axis, or fullscreen,
/// committed or requested) on a split output. Its frame centre is the
/// output's centre, which names one virtual screen for every such window, so
/// its screen comes from elsewhere.
struct FillingWindow
{
    QString engineScreen; ///< a tiling-tracked window's engine screen
    QString restoreScreen; ///< the virtual screen holding its restore rect's centre, on the same output
    QString trackedScreen; ///< the effect's last stamp, a tie-break
    QStringList outputScreens; ///< the live virtual screens of the output
};

/// The first of the engine, restore and tracked screens that is a live
/// virtual screen of the output, or empty: the caller then keeps the
/// positional answer, so a window that maps maximized with no restore rect
/// still resolves, and a dead id is never answered.
inline QString resolveFilling(const FillingWindow& in)
{
    for (const QString* id : {&in.engineScreen, &in.restoreScreen, &in.trackedScreen}) {
        if (!id->isEmpty() && in.outputScreens.contains(*id)) {
            return *id;
        }
    }
    return {};
}

} // namespace PlasmaZones::WindowScreenDecisions
