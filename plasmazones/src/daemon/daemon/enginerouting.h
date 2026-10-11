// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>
#include <QStringList>

#include <functional>

namespace PlasmaZones::EngineRouting {

using Mode = PhosphorZones::AssignmentEntry::Mode;

/// Where a window sits on a screen: the desktop and activity its context
/// resolves to there.
struct WindowPlace
{
    int desktop = 0;
    QString activity;
    /// On the desktop and activity the screen shows now.
    bool inView = true;
};

/// What the routing reads of the daemon, injected so it runs without one.
struct Inputs
{
    /// The screen the tracking service has the window on; empty when none.
    std::function<QString(const QString& windowId)> trackedScreen;
    /// The desktop and activity the window's own context resolves to on a
    /// screen (its own desktop, span or stickiness, else the screen's current).
    std::function<WindowPlace(const QString& windowId, const QString& screenId)> placeOnScreen;
    /// The configured mode of a (screen, desktop, activity) context.
    std::function<Mode(const QString& screenId, int desktop, const QString& activity)> configuredMode;
    /// The mode a screen runs now (the router: live engine sets first, a
    /// disabled engine downgraded). Answers for the context in view only.
    std::function<Mode(const QString& screenId)> liveMode;
    /// Where the autotile or scroll engine holds the window in the context in
    /// view; empty when it does not.
    std::function<QString(const QString& windowId)> autotileHeldScreen;
    std::function<QString(const QString& windowId)> scrollHeldScreen;
};

/// The mode of the engine that owns @p windowId: the float resolver and
/// writer, the autotile-mode predicate and the synthesized-slot engine id all
/// answer "which engine owns this window" through it. A tiling engine holding
/// the window in view owns it, whatever screen snap still has on record;
/// otherwise the screen's mode decides, live when the window is in view.
/// @p screenOverride, when given, replaces the window's tracked screen (a
/// close names its screen) and skips the hold check.
Mode windowEngineMode(const Inputs& inputs, const QString& windowId, const QString& screenOverride = QString());

/// The engine id a placement record uses for @p mode.
QString engineIdForMode(Mode mode);

/// @p candidates (every engine's float list), each once, keeping only those
/// @p isFloating answers true for: the owning engine's float verdict.
QStringList ownedFloatingWindows(QStringList candidates, const std::function<bool(const QString&)>& isFloating);

} // namespace PlasmaZones::EngineRouting
