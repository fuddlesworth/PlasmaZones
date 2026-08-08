// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "seedorderfilter.h"

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>

namespace PlasmaZones {

void filterEngineSeedOrder(QStringList& order, PhosphorPlacement::WindowTrackingService* wts,
                           const PhosphorEngine::WindowRegistry* registry, const QString& targetEngineId)
{
    if (order.isEmpty() || !wts) {
        return;
    }
    order.removeIf([wts, registry, &targetEngineId](const QString& windowId) {
        // minimizedState().value_or(false), not isMinimized(): keeps this
        // filter's unknown-handling in lockstep with the resnap-order and
        // restore-entry filters (autotile.cpp / autotile_init.cpp), which the
        // registry doc steers toward the tri-state accessor.
        const bool minimized = registry && registry->minimizedState(windowId).value_or(false);
        if (!minimized) {
            // Float is PER ENGINE: a live float read at seed time belongs to
            // the mode the screen is still in (the toggle seeds before the
            // assignment flips), and the durable snap slot's stateFloating is
            // the window's SNAPPING-mode verdict — neither says anything
            // about this engine. Dropping on them made a snap-floated window
            // untileable by mode swap: every snapping interlude restored the
            // float and re-poisoned the next seed, so even an explicit
            // Meta+F tile in autotile never survived a round trip. The snap
            // float is still restored on return to snapping (windowsReleased
            // reads the snap slot, which this seed never mutates).
            return false;
        }
        // Minimized entries stay as positional placeholders (the engine's
        // strict seed defers tiling them until their windowOpened arrives) —
        // EXCEPT a user-floated-then-minimized window, which a placeholder
        // would tile on unminimize instead of restoring its float.
        //
        // The slot read is the TARGET engine's, not a fixed one. Float is per
        // engine, and this branch is the only place the filter consults a
        // float verdict at all, so reading (say) the snap slot while seeding
        // the strip would drop a window that has no scrolling float verdict —
        // and would equally admit one that does. Same invariant the
        // non-minimized arm above turns on, applied to the minimized case.
        const auto record = wts->placementStore().peekExact(windowId);
        return record && record->slotFor(targetEngineId).state == PhosphorEngine::WindowPlacement::stateFloating();
    });
}

} // namespace PlasmaZones
