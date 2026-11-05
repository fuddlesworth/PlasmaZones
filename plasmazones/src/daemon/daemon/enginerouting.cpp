// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "enginerouting.h"

#include <PhosphorEngine/WindowPlacement.h>

namespace PlasmaZones::EngineRouting {

Mode windowEngineMode(const Inputs& inputs, const QString& windowId, const QString& screenOverride)
{
    QString screenId = screenOverride;
    if (screenId.isEmpty()) {
        // A tiling hold in view wins over the screen snap has on record,
        // which goes stale when the window moves while tiled (F113). Only a
        // hold in the context in view counts: a background desktop's tile of
        // a multi-desktop window says nothing about who owns it here.
        if (inputs.autotileHeldScreen && !inputs.autotileHeldScreen(windowId).isEmpty()) {
            return PhosphorZones::AssignmentEntry::Autotile;
        }
        if (inputs.scrollHeldScreen && !inputs.scrollHeldScreen(windowId).isEmpty()) {
            return PhosphorZones::AssignmentEntry::Scrolling;
        }
        if (inputs.trackedScreen) {
            screenId = inputs.trackedScreen(windowId);
        }
    }
    if (!screenId.isEmpty() && inputs.configuredMode) {
        // Resolved at the WINDOW's own desktop and activity, not the screen's
        // current ones: reading through the screen's current context made the
        // answer flip when a per-output desktop or activity switch crossed a
        // snap/autotile mode boundary, with no float broadcast to follow it.
        const WindowPlace place = inputs.placeOnScreen ? inputs.placeOnScreen(windowId, screenId) : WindowPlace{};
        // In view, the mode the screen RUNS: a configured mode whose engine
        // is off (a master switch, a context that disables it) runs
        // snapping, and float reads and writes must go there (F112).
        if (place.inView && inputs.liveMode) {
            return inputs.liveMode(screenId);
        }
        return inputs.configuredMode(screenId, place.desktop, place.activity);
    }
    // No screen and no hold: snapping, the no-context fallback.
    return PhosphorZones::AssignmentEntry::Snapping;
}

QString engineIdForMode(Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return QString(PhosphorEngine::WindowPlacement::autotileEngineId());
    case PhosphorZones::AssignmentEntry::Scrolling:
        return QString(PhosphorEngine::WindowPlacement::scrollingEngineId());
    case PhosphorZones::AssignmentEntry::Snapping:
        break;
    }
    return QString(PhosphorEngine::WindowPlacement::snapEngineId());
}

QStringList ownedFloatingWindows(QStringList candidates, const std::function<bool(const QString&)>& isFloating)
{
    candidates.removeDuplicates();
    if (!isFloating) {
        return candidates;
    }
    candidates.removeIf([&isFloating](const QString& windowId) {
        return !isFloating(windowId);
    });
    return candidates;
}

} // namespace PlasmaZones::EngineRouting
