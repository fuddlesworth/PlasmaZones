// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The desktop renumber: every structure this engine keys by desktop NUMBER
// follows a change of KWin's desktop list, a removal, an insert or a reorder,
// through the one old-to-new position map (PhosphorEngine::DesktopRemap,
// F763, F787). Moved out of engine_context.cpp, which was at the file-size
// ceiling, when the single-removal shift was generalised.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/DesktopRemap.h>

#include "evacueepark_p.h"
#include "scrollenginelogging.h"

#include <algorithm>

namespace PhosphorScrollEngine {

using PhosphorEngine::DesktopRemap;
using PhosphorEngine::PlacementStateKey;

void ScrollEngine::remapDesktops(const DesktopRemap& remap)
{
    if (remap.isIdentity()) {
        return;
    }
    // Every removed desktop's contexts go first, while every stored number is
    // still in the OLD numbering the map is written in.
    for (int oldDesktop = 1; oldDesktop <= remap.oldCount(); ++oldDesktop) {
        if (remap.map(oldDesktop) == 0) {
            pruneStatesForDesktop(oldDesktop);
        }
    }
    applyDesktopRemap(remap);
}

void ScrollEngine::renumberDesktopsAfterRemoval(int removedDesktop)
{
    if (removedDesktop < 1) {
        return;
    }
    // The caller ran pruneStatesForDesktop first. The removal map has to reach
    // every number held, or an entry above its old count would read as outside
    // the list and keep its number.
    int highest = std::max(removedDesktop, m_context.highestDesktop());
    const auto reach = [&highest](const PlacementStateKey& key) {
        highest = std::max(highest, key.desktop);
    };
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        reach(it.key());
    }
    for (auto it = m_stripStash.cbegin(); it != m_stripStash.cend(); ++it) {
        reach(it.key());
    }
    for (auto it = m_perScreenOverrides.cbegin(); it != m_perScreenOverrides.cend(); ++it) {
        reach(it.key());
    }
    for (auto it = m_burstPendingApplies.cbegin(); it != m_burstPendingApplies.cend(); ++it) {
        reach(it.key());
    }
    std::for_each(m_pendingFocusEmitContexts.cbegin(), m_pendingFocusEmitContexts.cend(), reach);
    applyDesktopRemap(DesktopRemap::removal(removedDesktop, highest));
}

void ScrollEngine::applyDesktopRemap(const DesktopRemap& remap)
{
    const auto moves = [&remap](const PlacementStateKey& key) {
        const int to = remap.map(key.desktop);
        return to > 0 && to != key.desktop;
    };
    const auto mapped = [&remap](const PlacementStateKey& key) {
        return PlacementStateKey{key.screenId, remap.map(key.desktop), key.activity};
    };
    // A scratch key no live desktop can name, unique per old desktop.
    const auto scratch = [](const PlacementStateKey& key) {
        return PlacementStateKey{key.screenId, -key.desktop, key.activity};
    };

    // The context-keyed side maps OUTLIVE their states: a strip stash is
    // written as the state is torn down, and the override map is pushed per
    // context whether or not one exists. migrateStateKey carries the entries
    // whose key has a live state; the rest are taken out HERE, before any state
    // moves, so a state landing on their old key finds it vacant and keeps its
    // own entries, and are put back under their new number once every state
    // has moved. Left behind they keep the number their desktop had before, and
    // the next desktop to take it would resolve the template of another (see
    // pruneStatesForDesktop).
    const auto takeStateless = [&](auto& map) {
        QList<std::pair<PlacementStateKey, typename std::decay_t<decltype(map)>::mapped_type>> taken;
        for (auto it = map.begin(); it != map.end();) {
            if (moves(it.key()) && !m_states.containsKey(it.key())) {
                taken.append({mapped(it.key()), it.value()});
                it = map.erase(it);
            } else {
                ++it;
            }
        }
        return taken;
    };
    const auto stash = takeStateless(m_stripStash);
    const auto stashConsumed = takeStateless(m_stripStashConsumed);
    const auto burst = takeStateless(m_burstPendingApplies);
    const auto overrides = takeStateless(m_perScreenOverrides);
    QList<PlacementStateKey> focusContexts;
    for (auto it = m_pendingFocusEmitContexts.begin(); it != m_pendingFocusEmitContexts.end();) {
        if (moves(*it) && !m_states.containsKey(*it)) {
            focusContexts.append(mapped(*it));
            it = m_pendingFocusEmitContexts.erase(it);
        } else {
            ++it;
        }
    }

    // The states, in two steps: every moving key to a scratch key first, then
    // each to its new number. A reorder is a cycle (2 -> 3 and 3 -> 2), and a
    // move straight to the new number would land on a state not yet moved; the
    // old single-removal shift avoided that only by walking ascending. Nothing
    // on the way rejects a desktop below 1: migrateStateKey and the containers
    // take any key. Snapshot first, since a move mutates the map it iterates.
    QList<PlacementStateKey> moving;
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        if (moves(it.key())) {
            moving.append(it.key());
        }
    }
    QStringList displacedWindows;
    QSet<QString> displacedScreens;
    QSet<QString> touchedScreens;
    for (const PlacementStateKey& oldKey : std::as_const(moving)) {
        migrateStateKey(oldKey, scratch(oldKey), displacedWindows, displacedScreens);
    }
    for (const PlacementStateKey& oldKey : std::as_const(moving)) {
        if (migrateStateKey(scratch(oldKey), mapped(oldKey), displacedWindows, displacedScreens)) {
            touchedScreens.insert(oldKey.screenId);
        }
    }
    finishDisplacedRelease(displacedWindows, displacedScreens);

    // The stateless entries back, under their new number, onto a VACANT key
    // only: one there already came with a live state and outranks a leftover.
    const auto putBack = [](auto& map, const auto& entries) {
        for (const auto& [key, value] : entries) {
            if (!map.contains(key)) {
                map.insert(key, value);
            }
        }
    };
    putBack(m_stripStash, stash);
    putBack(m_stripStashConsumed, stashConsumed);
    putBack(m_burstPendingApplies, burst);
    putBack(m_perScreenOverrides, overrides);
    for (const PlacementStateKey& key : std::as_const(focusContexts)) {
        m_pendingFocusEmitContexts.insert(key);
    }
    // A parked strip's contexts name desktops too; the stash under them moved
    // above. One on a removed desktop has nothing left to re-seat.
    if (m_evacueePark) {
        for (auto& contexts : m_evacueePark->byOutput) {
            QHash<PlacementStateKey, QSet<QString>> remapped;
            for (auto it = contexts.cbegin(); it != contexts.cend(); ++it) {
                const int to = remap.map(it.key().desktop);
                if (to > 0) {
                    remapped.insert(PlacementStateKey{it.key().screenId, to, it.key().activity}, it.value());
                }
            }
            contexts = std::move(remapped);
        }
    }

    // The pins and the per-output desktop map index the same numbering as the
    // keys just moved, so they move together or they name a position whose
    // content moved.
    m_context.remapDesktops(remap);

    if (!touchedScreens.isEmpty()) {
        qCInfo(lcScrollEngine) << "applyDesktopRemap: re-keyed strips for a desktop list change on"
                               << touchedScreens.size() << "screen(s)";
        // A screen now showing a re-keyed strip has a layout the compositor
        // has never seen under this numbering, and the emit-on-change gate
        // would swallow it — the same pairing the unpin migration makes.
        for (const QString& screenId : std::as_const(touchedScreens)) {
            if (!m_scrollingScreens.contains(screenId)) {
                continue;
            }
            if (announceStripContextIfChanged(screenId)) {
                m_forceEmitScreens.insert(screenId);
            }
            scheduleRetileForScreen(screenId);
        }
    }
}

} // namespace PhosphorScrollEngine
