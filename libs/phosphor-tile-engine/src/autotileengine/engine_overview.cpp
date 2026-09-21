// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// AutotileEngine's workspace-overview read surface (IOverviewModelSource).
// A pure read of the per-key TilingState: it never creates a state, never
// touches focus or scheduling, and emits nothing.

#include <PhosphorTileEngine/AutotileEngine.h>

#include <PhosphorTiles/TilingState.h>

#include <QPoint>
#include <QSet>

#include <algorithm>

namespace PhosphorTileEngine {

std::optional<QList<PhosphorEngine::OverviewWindowEntry>>
AutotileEngine::overviewWindowsFor(const PhosphorEngine::PlacementStateKey& key) const
{
    // constFind on the forward map, not tilingStateForScreen: that accessor
    // creates a state for an unknown key, and the overview contract forbids
    // the read from leaving any trace.
    const auto it = m_states.states().constFind(key);
    if (it == m_states.states().constEnd() || !it.value()) {
        return std::nullopt;
    }
    const PhosphorTiles::TilingState* state = it.value();

    // Zones are absolute logical pixels: recalculateLayout hands the algorithm
    // the screen's available geometry as the canvas and clamps the result to
    // that same rect, and applyTiling emits zones[i] unchanged as the window
    // frame (layout_apply.cpp). No translation is needed here.
    const QStringList tiled = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();
    const int zoned = static_cast<int>(std::min(tiled.size(), zones.size()));

    QList<PhosphorEngine::OverviewWindowEntry> entries;
    entries.reserve(state->windowOrder().size());

    QSet<QString> listed;
    for (int i = 0; i < tiled.size(); ++i) {
        PhosphorEngine::OverviewWindowEntry entry;
        entry.windowId = tiled.at(i);
        // Past the zone cap (maxWindows overflow the layout did not size), the
        // window is still tiled by membership; the best rect this engine has
        // for it is the one it last applied, which may be null.
        entry.rect = i < zoned ? zones.at(i) : m_lastAppliedTileRect.value(entry.windowId);
        entry.floating = false;
        entries.append(entry);
        listed.insert(entry.windowId);
    }

    // Every other window in the order is a float (minimize-floats included,
    // which the daemon delivers to this engine as floats). lastManagedRect is
    // this engine's own memory of the last tile rect it emitted for the
    // window, so a fresh float still reports where its tile was. A null rect
    // is the signal for the daemon to fill in tracked geometry.
    const QStringList order = state->windowOrder();
    for (const QString& windowId : order) {
        if (listed.contains(windowId)) {
            continue;
        }
        PhosphorEngine::OverviewWindowEntry entry;
        entry.windowId = windowId;
        entry.rect = lastManagedRect(windowId);
        if (entry.rect.isNull()) {
            entry.rect = m_lastAppliedTileRect.value(windowId);
        }
        entry.floating = true;
        entries.append(entry);
        listed.insert(windowId);
    }

    return entries;
}

int AutotileEngine::insertIndexForPoint(const PhosphorEngine::PlacementStateKey& key, const QPoint& pos) const
{
    const auto it = m_states.states().constFind(key);
    if (it == m_states.states().constEnd() || !it.value()) {
        return 0;
    }
    const PhosphorTiles::TilingState* state = it.value();
    const QStringList tiled = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();
    const QStringList order = state->windowOrder();

    // Same walk as computeDragInsertIndexAtPoint: the first zone containing
    // the point wins, and windows past the layout cap have no zone to hit.
    // The hit is a tiled index; the caller's unit is the window-order index,
    // so it is mapped through the window's order position.
    const int limit = static_cast<int>(std::min(zones.size(), tiled.size()));
    for (int i = 0; i < limit; ++i) {
        if (zones.at(i).contains(pos)) {
            const int orderIndex = static_cast<int>(order.indexOf(tiled.at(i)));
            return orderIndex >= 0 ? orderIndex : static_cast<int>(order.size());
        }
    }
    // No zone hit: append. The end of the order keeps the arrival after every
    // tiled window even when floats sit at the tail, and addWindow treats any
    // position at or past the size as an append.
    return static_cast<int>(order.size());
}

} // namespace PhosphorTileEngine
