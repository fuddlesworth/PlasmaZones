// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "seedorderfilter.h"

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>

namespace PlasmaZones {

void filterAutotileSeedOrder(QStringList& order, PhosphorPlacement::WindowTrackingService* wts,
                             const PhosphorEngine::WindowRegistry* registry)
{
    if (order.isEmpty() || !wts) {
        return;
    }
    order.removeIf([wts, registry](const QString& windowId) {
        // minimizedState().value_or(false), not isMinimized(): keeps this
        // filter's unknown-handling in lockstep with the resnap-order and
        // restore-entry filters (autotile.cpp / autotile_init.cpp), which the
        // registry doc steers toward the tri-state accessor.
        const bool minimized = registry && registry->minimizedState(windowId).value_or(false);
        if (!minimized && wts->isWindowFloating(windowId)) {
            return true;
        }
        const auto record = wts->placementStore().peekExact(windowId);
        if (record
            && record->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state
                == PhosphorEngine::WindowPlacement::stateFloating()) {
            return true;
        }
        return false;
    });
}

} // namespace PlasmaZones
