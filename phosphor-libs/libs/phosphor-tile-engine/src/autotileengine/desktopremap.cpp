// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The desktop renumber: every structure this engine keys by desktop NUMBER
// follows a change of KWin's desktop list, a removal, an insert or a reorder,
// through the one old-to-new position map (PhosphorEngine::DesktopRemap,
// F763, F787). Moved out of context.cpp when the single-removal shift was
// generalised; the scroll engine's engine_desktopremap.cpp is the twin.

#include <PhosphorTileEngine/AutotileEngine.h>

#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingState.h>
#include "evacueepark_p.h"
#include "tileenginelogging.h"

#include <algorithm>
#include <vector>

namespace PhosphorTileEngine {

using PhosphorEngine::DesktopRemap;
using PhosphorEngine::TilingStateKey;

void AutotileEngine::remapDesktops(const DesktopRemap& remap)
{
    if (remap.isIdentity()) {
        return;
    }
    // Every removed desktop's states go first, through the full teardown,
    // while every stored number is still in the OLD numbering the map is
    // written in.
    for (int oldDesktop = 1; oldDesktop <= remap.oldCount(); ++oldDesktop) {
        if (remap.map(oldDesktop) == 0) {
            pruneStatesForDesktop(oldDesktop);
        }
    }
    applyDesktopRemap(remap);
}

void AutotileEngine::renumberDesktopsAfterRemoval(int removedDesktop)
{
    if (removedDesktop < 1) {
        return;
    }
    // The caller ran pruneStatesForDesktop first. The removal map has to reach
    // every number held, or an entry above its old count would read as outside
    // the list and keep its number.
    int highest = std::max(removedDesktop, m_context.highestDesktop());
    const auto reach = [&highest](const TilingStateKey& key) {
        highest = std::max(highest, key.desktop);
    };
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        reach(it.key());
    }
    for (const auto& entry : m_scriptStateStash) {
        reach(entry.first);
    }
    std::for_each(m_userTunedSplitRatio.cbegin(), m_userTunedSplitRatio.cend(), reach);
    std::for_each(m_userTunedMasterCount.cbegin(), m_userTunedMasterCount.cend(), reach);
    std::for_each(m_dirtyBackgroundContexts.cbegin(), m_dirtyBackgroundContexts.cend(), reach);
    if (m_evacueePark) {
        for (const auto& contexts : std::as_const(m_evacueePark->byOutput)) {
            for (const ParkedTileContext& ctx : contexts) {
                reach(ctx.key);
                std::for_each(ctx.granted.cbegin(), ctx.granted.cend(), reach);
            }
        }
    }
    applyDesktopRemap(DesktopRemap::removal(removedDesktop, highest));
}

void AutotileEngine::applyDesktopRemap(const DesktopRemap& remap)
{
    const auto moves = [&remap](const TilingStateKey& key) {
        const int to = remap.map(key.desktop);
        return to > 0 && to != key.desktop;
    };
    const auto mapped = [&remap](const TilingStateKey& key) {
        return TilingStateKey{key.screenId, remap.map(key.desktop), key.activity};
    };
    // A scratch key no live desktop can name, unique per old desktop.
    const auto scratch = [](const TilingStateKey& key) {
        return TilingStateKey{key.screenId, -key.desktop, key.activity};
    };

    // The script-state stash is context-keyed and OUTLIVES its state: it is
    // written as the state dies, so a stash with no live state is the normal
    // condition rather than an edge case. migrateStateKey moves a stash only
    // when a live state sits at the same key, so the leftovers are TAKEN OUT
    // here, before any state moves, and re-filed afterwards under their new
    // number. Left in place, migrateStateKey would overwrite or erase the one
    // at a moving state's target, which is right for the entry of a state it
    // replaces and wrong for a leftover itself queued to move; held locally,
    // the re-file cannot pick up a bag the state moves put there either. The
    // user-tuned flags without a live state go the same way. std::vector, not
    // QList: StashedScriptState owns a unique_ptr split tree and is move-only.
    std::vector<std::pair<TilingStateKey, StashedScriptState>> statelessStashes;
    for (auto it = m_scriptStateStash.begin(); it != m_scriptStateStash.end();) {
        if (moves(it->first) && !m_states.containsKey(it->first)) {
            statelessStashes.emplace_back(mapped(it->first), std::move(it->second));
            it = m_scriptStateStash.erase(it);
        } else {
            ++it;
        }
    }
    const auto takeStateless = [&](QSet<TilingStateKey>& flags) {
        QList<TilingStateKey> taken;
        for (auto it = flags.begin(); it != flags.end();) {
            if (moves(*it) && !m_states.containsKey(*it)) {
                taken.append(mapped(*it));
                it = flags.erase(it);
            } else {
                ++it;
            }
        }
        return taken;
    };
    const QList<TilingStateKey> tunedSplit = takeStateless(m_userTunedSplitRatio);
    const QList<TilingStateKey> tunedMaster = takeStateless(m_userTunedMasterCount);

    // The states, in two steps: every moving key to a scratch key first, then
    // each to its new number. A reorder is a cycle (2 -> 3 and 3 -> 2), and a
    // move straight to the new number would land on a state not yet moved;
    // the old single-removal shift avoided that only by walking ascending.
    // migrateStateKey carries each state's own bag and tuned flags with it.
    // Snapshot first, since a move mutates the map it iterates.
    QList<TilingStateKey> moving;
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        if (moves(it.key())) {
            moving.append(it.key());
        }
    }
    QSet<QString> touchedScreens;
    for (const TilingStateKey& oldKey : std::as_const(moving)) {
        migrateStateKey(oldKey, scratch(oldKey));
    }
    for (const TilingStateKey& oldKey : std::as_const(moving)) {
        if (migrateStateKey(scratch(oldKey), mapped(oldKey))) {
            touchedScreens.insert(oldKey.screenId);
        }
    }

    // NOW re-file what was taken out, at the new keys. A destination can only
    // be blocked by an entry still in the map, and none of these are; two of
    // them cannot collide either, since the map sends distinct desktops to
    // distinct ones. What a destination CAN hold is a live state's bag or flag
    // the moves put there, which outranks a leftover, so the leftover is
    // dropped rather than clobbering it.
    for (auto& [newKey, bag] : statelessStashes) {
        if (!m_scriptStateStash.contains(newKey)) {
            m_scriptStateStash.insert_or_assign(newKey, std::move(bag));
        }
    }
    for (const TilingStateKey& key : tunedSplit) {
        if (!m_states.containsKey(key)) {
            m_userTunedSplitRatio.insert(key);
        }
    }
    for (const TilingStateKey& key : tunedMaster) {
        if (!m_states.containsKey(key)) {
            m_userTunedMasterCount.insert(key);
        }
    }

    // The pins and the per-output desktop map index the same numbering, so they
    // move with the states or they name a position whose content moved.
    m_context.remapDesktops(remap);
    // The dirty-background memo indexes it too.
    QSet<TilingStateKey> remappedDirty;
    for (const TilingStateKey& key : std::as_const(m_dirtyBackgroundContexts)) {
        if (const int to = remap.map(key.desktop); to > 0) {
            remappedDirty.insert(TilingStateKey{key.screenId, to, key.activity});
        }
    }
    m_dirtyBackgroundContexts = std::move(remappedDirty);
    // So does a parked output's every context, and the key a parked window
    // was granted its place on. One on a removed desktop has nothing left to
    // re-seat.
    if (m_evacueePark) {
        for (auto outIt = m_evacueePark->byOutput.begin(); outIt != m_evacueePark->byOutput.end();) {
            QVector<ParkedTileContext>& contexts = outIt.value();
            for (ParkedTileContext& ctx : contexts) {
                ctx.key.desktop = remap.map(ctx.key.desktop);
                for (auto it = ctx.granted.begin(); it != ctx.granted.end();) {
                    it.value().desktop = remap.map(it.value().desktop);
                    it = it.value().desktop == 0 ? ctx.granted.erase(it) : std::next(it);
                }
            }
            contexts.erase(std::remove_if(contexts.begin(), contexts.end(),
                                          [](const ParkedTileContext& ctx) {
                                              return ctx.key.desktop == 0;
                                          }),
                           contexts.end());
            outIt = contexts.isEmpty() ? m_evacueePark->byOutput.erase(outIt) : std::next(outIt);
        }
    }

    if (!touchedScreens.isEmpty()) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "applyDesktopRemap: re-keyed states for a desktop list change on"
                                                 << touchedScreens.size() << "screen(s)";
        // Whichever of these screens now shows a re-keyed state has a layout
        // the compositor has never been told about under this numbering, and
        // the emit-on-change gate would swallow it. Retile the ones whose
        // CURRENT context is one of the states that moved.
        for (const QString& screenId : std::as_const(touchedScreens)) {
            if (m_autotileScreens.contains(screenId) && m_states.stateForKey(currentKeyForScreen(screenId))) {
                scheduleRetileForScreen(screenId);
            }
        }
    }
}

} // namespace PhosphorTileEngine
