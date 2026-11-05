// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The desktop renumber: every structure this engine keys by desktop NUMBER
// follows a change of KWin's desktop list, a removal, an insert or a reorder,
// through the one old-to-new position map (PhosphorEngine::DesktopRemap,
// F763, F787). The scroll engine's engine_desktopremap.cpp is the twin.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>

#include <PhosphorEngine/DesktopRemap.h>

#include "evacueepark_p.h"
#include "pendingcarries_p.h"
#include "snapenginelogging.h"

#include <algorithm>

namespace PhosphorSnapEngine {

using PhosphorEngine::DesktopRemap;
using PhosphorEngine::PlacementStateKey;

void SnapEngine::remapDesktops(const DesktopRemap& remap)
{
    if (remap.isIdentity()) {
        return;
    }
    // Every removed desktop's stores go first, while every stored number is
    // still in the OLD numbering the map is written in.
    for (int oldDesktop = 1; oldDesktop <= remap.oldCount(); ++oldDesktop) {
        if (remap.map(oldDesktop) == 0) {
            pruneStatesForDesktop(oldDesktop);
        }
    }
    applyDesktopRemap(remap);
    // The persisted per-desktop zone maps follow, or a restart seeds a zone
    // under a number that now belongs to another desktop. Through the
    // tracker's wrapper so the change reaches disk. It moves every engine's
    // slot of a record, so the autotile engine's remap leaves it alone.
    if (m_windowTracker) {
        m_windowTracker->remapDesktopZones(remap);
    }
}

void SnapEngine::renumberDesktopsAfterRemoval(int removedDesktop)
{
    if (removedDesktop < 1) {
        return;
    }
    // The caller ran pruneStatesForDesktop first. The removal map has to reach
    // every number held, or an entry above its old count would read as outside
    // the list and keep its number.
    int highest = std::max(removedDesktop, m_context.highestDesktop());
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        highest = std::max(highest, it.key().desktop);
        if (const SnapState* state = it.value()) {
            highest = std::max(highest, state->lastUsedDesktop());
            for (const int desktop : state->desktopAssignments()) {
                highest = std::max(highest, desktop);
            }
        }
    }
    if (m_evacueePark) {
        for (const auto& windows : std::as_const(m_evacueePark->byOutput)) {
            for (const auto& contexts : windows) {
                for (const ParkedSnapContext& ctx : contexts) {
                    highest = std::max(highest, ctx.key.desktop);
                }
            }
        }
    }
    if (m_pendingCarries) {
        for (const PendingActivityCarry& carry : std::as_const(m_pendingCarries->byWindow)) {
            for (auto it = carry.zonesByDesktop.cbegin(); it != carry.zonesByDesktop.cend(); ++it) {
                highest = std::max(highest, it.key());
            }
        }
        for (const OwedFloatBack& owed : std::as_const(m_pendingCarries->owedFloatBacks)) {
            highest = std::max(highest, owed.key.desktop);
        }
    }
    applyDesktopRemap(DesktopRemap::removal(removedDesktop, highest));
    // The tracker sizes its own removal map to the numbers its records hold.
    if (m_windowTracker) {
        m_windowTracker->renumberDesktopZones(removedDesktop);
    }
}

void SnapEngine::applyDesktopRemap(const DesktopRemap& remap)
{
    // Snapping has no stack to re-flow and no strip to announce: a zone keeps
    // its geometry under its new number. The move still matters, since a zone
    // filed under the number its desktop had before is handed to whichever
    // desktop takes that number next. The global holder has an empty screenId
    // and no desktop identity, so it never moves.
    const auto moves = [&remap](const PlacementStateKey& key) {
        const int to = remap.map(key.desktop);
        return !key.screenId.isEmpty() && to > 0 && to != key.desktop;
    };
    // A scratch key no live desktop can name, unique per old desktop.
    const auto scratch = [](const PlacementStateKey& key) {
        return PlacementStateKey{key.screenId, -key.desktop, key.activity};
    };

    // The stores, in two steps: every moving key to a scratch key first, then
    // each to its new number. A reorder is a cycle (2 -> 3 and 3 -> 2), and a
    // move straight to the new number would land on a store not yet moved; the
    // old single-removal shift avoided that only by walking ascending.
    // Snapshot first, since a move mutates the map it iterates.
    QList<PlacementStateKey> moving;
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        if (moves(it.key())) {
            moving.append(it.key());
        }
    }
    for (const PlacementStateKey& oldKey : std::as_const(moving)) {
        m_states.insertState(scratch(oldKey), m_states.takeState(oldKey));
        m_states.rekeyWindows(oldKey, scratch(oldKey));
    }
    for (const PlacementStateKey& oldKey : std::as_const(moving)) {
        const PlacementStateKey newKey{oldKey.screenId, remap.map(oldKey.desktop), oldKey.activity};
        SnapState* state = m_states.takeState(scratch(oldKey));
        if (!state) {
            continue;
        }
        // Only a store the prunes did not reach meets a store at its target: a
        // renumber run without its prune, or a stale number past the old list.
        // That store's windows join the one moving in, whose key the re-key
        // gives their memberships, instead of going with it (F797).
        if (SnapState* existing = m_states.takeState(newKey)) {
            qCWarning(PhosphorSnapEngine::lcSnapEngine)
                << "applyDesktopRemap: merging a store found at" << newKey.screenId << newKey.desktop;
            QSet<QString> windows;
            for (auto it = existing->screenAssignments().cbegin(); it != existing->screenAssignments().cend(); ++it) {
                windows.insert(it.key());
            }
            for (auto it = existing->zoneAssignments().cbegin(); it != existing->zoneAssignments().cend(); ++it) {
                windows.insert(it.key());
            }
            for (const QString& windowId : existing->floatingWindows()) {
                windows.insert(windowId);
            }
            for (const QString& windowId : std::as_const(windows)) {
                existing->migrateWindowTo(state, windowId, newKey.screenId);
            }
            existing->deleteLater();
        }
        m_states.insertState(newKey, state);
        m_states.rekeyWindows(scratch(oldKey), newKey);
    }

    // The pins and the per-output desktop map index the same numbering as
    // the keys just moved, so they move together or they name a position
    // whose content moved.
    m_context.remapDesktops(remap);
    // So do the desktop numbers the stores hold for each window and for their
    // last-used zone (the global holder included): left alone they name the
    // desktop that takes the old number, which the resnap stamp, the
    // per-desktop forget and the last-zone gate then act on (F145).
    for (SnapState* state : m_states.states()) {
        if (state) {
            state->remapDesktops(remap);
        }
    }
    // A parked output's contexts name desktops too. One on a removed desktop
    // has nothing left to re-seat.
    if (m_evacueePark) {
        for (auto outIt = m_evacueePark->byOutput.begin(); outIt != m_evacueePark->byOutput.end();) {
            for (auto winIt = outIt->begin(); winIt != outIt->end();) {
                QVector<ParkedSnapContext>& contexts = winIt.value();
                for (ParkedSnapContext& ctx : contexts) {
                    ctx.key.desktop = remap.map(ctx.key.desktop);
                }
                contexts.erase(std::remove_if(contexts.begin(), contexts.end(),
                                              [](const ParkedSnapContext& ctx) {
                                                  return ctx.key.desktop == 0;
                                              }),
                               contexts.end());
                winIt = contexts.isEmpty() ? outIt->erase(winIt) : std::next(winIt);
            }
            outIt = outIt->isEmpty() ? m_evacueePark->byOutput.erase(outIt) : std::next(outIt);
        }
    }
    // So does a carry waiting for an activity: its zones are per desktop of
    // the activity it left. Rebuilt into a fresh map so a moving entry cannot
    // land on one not yet visited. A float-back owed to a desktop follows it,
    // and one owed to a removed desktop goes.
    if (m_pendingCarries) {
        for (PendingActivityCarry& carry : m_pendingCarries->byWindow) {
            QHash<int, QStringList> remapped;
            for (auto it = carry.zonesByDesktop.cbegin(); it != carry.zonesByDesktop.cend(); ++it) {
                if (const int to = remap.map(it.key()); to > 0) {
                    remapped.insert(to, it.value());
                }
            }
            carry.zonesByDesktop = std::move(remapped);
        }
        for (auto it = m_pendingCarries->owedFloatBacks.begin(); it != m_pendingCarries->owedFloatBacks.end();) {
            it->key.desktop = remap.map(it->key.desktop);
            it = it->key.desktop == 0 ? m_pendingCarries->owedFloatBacks.erase(it) : std::next(it);
        }
    }
    if (!moving.isEmpty()) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "applyDesktopRemap: re-keyed" << moving.size() << "store(s) for a desktop list change";
    }
}

} // namespace PhosphorSnapEngine
