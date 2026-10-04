// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>

#include <functional>

namespace PlasmaZones::EngineRouting {

using Mode = PhosphorZones::AssignmentEntry::Mode;

/// Where a window sits on a screen: the desktop and activity its context
/// resolves to there.
struct WindowPlace
{
    int desktop = 0;
    QString activity;
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
    /// Whether the autotile or scroll engine tracks the window, in any context.
    std::function<bool(const QString& windowId)> autotileTracks;
    std::function<bool(const QString& windowId)> scrollTracks;
};

/// The mode of the engine that owns @p windowId: the float resolver and
/// writer, the autotile-mode predicate and the synthesized-slot engine id all
/// answer "which engine owns this window" through it. @p screenOverride, when
/// given, replaces the window's tracked screen (a close names its screen).
Mode windowEngineMode(const Inputs& inputs, const QString& windowId, const QString& screenOverride = QString());

/// The engine id a placement record uses for @p mode.
QString engineIdForMode(Mode mode);

} // namespace PlasmaZones::EngineRouting
