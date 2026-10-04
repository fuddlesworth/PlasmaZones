// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "enginerouting.h"

#include <PhosphorEngine/WindowPlacement.h>

namespace PlasmaZones::EngineRouting {

Mode windowEngineMode(const Inputs& inputs, const QString& windowId, const QString& screenOverride)
{
    QString screenId = screenOverride;
    if (screenId.isEmpty() && inputs.trackedScreen) {
        screenId = inputs.trackedScreen(windowId);
    }
    if (!screenId.isEmpty() && inputs.configuredMode) {
        // Resolved at the WINDOW's own desktop and activity, not the screen's
        // current ones: reading through the screen's current context made the
        // answer flip when a per-output desktop or activity switch crossed a
        // snap/autotile mode boundary, with no float broadcast to follow it.
        const WindowPlace place = inputs.placeOnScreen ? inputs.placeOnScreen(windowId, screenId) : WindowPlace{};
        return inputs.configuredMode(screenId, place.desktop, place.activity);
    }
    // No tracked screen (a window snap never saw): a tiling engine that
    // tracks it wins, else Snapping, the historical no-context fallback.
    if (inputs.autotileTracks && inputs.autotileTracks(windowId)) {
        return PhosphorZones::AssignmentEntry::Autotile;
    }
    if (inputs.scrollTracks && inputs.scrollTracks(windowId)) {
        return PhosphorZones::AssignmentEntry::Scrolling;
    }
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

} // namespace PlasmaZones::EngineRouting
