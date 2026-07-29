// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

#include <PhosphorEngine/WindowPlacement.h>

namespace PlasmaZones::TestHelpers {

// Build a single-engine partial record: the calling engine's slot plus, for the
// un-managed states (free/floating), the shared per-screen free geometry the
// capture orchestrator would supply. WindowPlacementStore::record() merges these
// into the one record per window. Tests needing multi-engine or multi-screen
// fixtures set the extra fields on the returned value.
inline PhosphorEngine::WindowPlacement makePlacement(const QString& windowId, const QString& appId,
                                                     const QString& state, const QString& engine,
                                                     const QString& screen = QStringLiteral("DP-1"),
                                                     const QRect& rect = QRect(10, 20, 300, 400))
{
    PhosphorEngine::WindowPlacement p;
    p.windowId = windowId;
    p.appId = appId;
    p.screenId = screen;
    PhosphorEngine::EngineSlot slot;
    slot.state = state;
    if (state == PhosphorEngine::WindowPlacement::stateSnapped()) {
        slot.zoneIds = QStringList{QStringLiteral("z1")};
    } else if (state == PhosphorEngine::WindowPlacement::stateTiled()) {
        slot.order = 0;
    }
    p.engines.insert(engine, slot);
    if (state == PhosphorEngine::WindowPlacement::stateFree()
        || state == PhosphorEngine::WindowPlacement::stateFloating()) {
        p.freeGeometryByScreen.insert(screen, rect);
    }
    return p;
}

} // namespace PlasmaZones::TestHelpers
