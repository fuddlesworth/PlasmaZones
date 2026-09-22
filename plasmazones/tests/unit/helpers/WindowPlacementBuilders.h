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
                                                     const QRect& rect = QRect(10, 20, 300, 400), int order = 0)
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
        slot.order = order;
    }
    p.engines.insert(engine, slot);
    // Conditional on validity so a test can build a GEOMETRY-LESS floating
    // record by passing QRect() — the shape the reopen accept distinguishes
    // (same-instance only) from a restorable float-back. Inserting an
    // invalid rect instead would create a third record shape that neither
    // hasRestorableContent nor the merge treats like an absent entry.
    if ((state == PhosphorEngine::WindowPlacement::stateFree()
         || state == PhosphorEngine::WindowPlacement::stateFloating())
        && rect.isValid()) {
        p.freeGeometryByScreen.insert(screen, rect);
    }
    return p;
}

} // namespace PlasmaZones::TestHelpers
