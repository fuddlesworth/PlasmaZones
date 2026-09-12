// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership for snapping: which of a screen's desktops a window
// may hold its own zone assignment on.
//
// The third arm of the same change the scroll and tile engines carry, and the
// one that differs most. Those engines PLACE a window, so adopting one into a
// desktop means inserting it into that desktop's layout. Snapping places
// nothing by itself — the user drops a window into a zone — so adoption here
// only grants the window a store of its own on the desktop in view. What it
// buys is that the next snap writes THERE instead of overwriting the zone the
// window occupies on the desktop it came from, which is what made a sticky
// window share one zone across every desktop.
//
// The eviction in stateForWindowOnScreen is the other half: it now spares
// stores the window is a member of, so the two assignments coexist instead of
// the newer one wiping the older.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>

#include "snapenginelogging.h"

#include <algorithm>

namespace PhosphorSnapEngine {

void SnapEngine::installContextResolver()
{
    // Without this the container's primary resolution falls back to a window's
    // FIRST membership, so a window snapped on desktop 1 and then on desktop 2
    // resolves to desktop 1's store both times — and the second snap overwrites
    // the first rather than living beside it. The global-scalar holder sits
    // under the empty key and is never a membership, so it is unaffected.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

void SnapEngine::seedPersistedDesktopZones(const QString& windowId, const PhosphorEngine::EngineSlot& slot,
                                           const QString& screenId, int restoreDesktop)
{
    // A restored record carries the zone the window occupied on EVERY desktop
    // it was present on. The open path applies only the one for the desktop
    // being restored onto; the rest have to be put back into their own stores
    // here, or switching to those desktops after a restart would find nothing
    // and leave the window wherever it happens to be.
    if (slot.zonesByDesktop.isEmpty() || screenId.isEmpty()) {
        return;
    }
    for (auto it = slot.zonesByDesktop.constBegin(); it != slot.zonesByDesktop.constEnd(); ++it) {
        const int desktop = it.key();
        if (desktop < 1 || desktop == restoreDesktop || it.value().isEmpty()) {
            continue; // the restore desktop is applied by the caller
        }
        const PhosphorEngine::PlacementStateKey key{screenId, desktop, currentActivity()};
        SnapState* state = ensureStateForKey(key);
        if (!state) {
            continue;
        }
        state->assignWindowToZones(windowId, it.value(), screenId, desktop);
        m_states.addMembership(windowId, key);
        qCInfo(lcSnapEngine) << "seedPersistedDesktopZones: restored" << windowId << "to" << it.value().size()
                             << "zone(s) on desktop" << desktop << "of" << screenId;
    }
}

void SnapEngine::reconcileDesktopMemberships(const QString& screenId, const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || screenId.isEmpty()) {
        return;
    }
    const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot: the arms below create stores and mutate the membership map.
    struct Pending
    {
        QString windowId;
        QList<PhosphorEngine::PlacementStateKey> stale;
        bool adopt = false;
    };
    QList<Pending> pending;
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<PhosphorEngine::PlacementStateKey> held = m_states.membershipsForWindow(windowId);
        const bool onThisScreen = std::any_of(held.cbegin(), held.cend(), [&screenId](const auto& key) {
            return key.screenId == screenId;
        });
        if (!onThisScreen) {
            continue;
        }
        const QSet<int> span = spanOf(windowId);
        Pending entry;
        entry.windowId = windowId;
        // An EMPTY span is a sticky window (or one whose desktop is unknown):
        // it covers every desktop, so it is never stale and always wants one.
        if (!span.isEmpty()) {
            for (const PhosphorEngine::PlacementStateKey& key : held) {
                if (key.screenId == screenId && !span.contains(key.desktop)) {
                    entry.stale.append(key);
                }
            }
        }
        entry.adopt =
            (span.isEmpty() || span.contains(currentKey.desktop)) && !m_states.hasMembership(windowId, currentKey);
        if (entry.adopt || !entry.stale.isEmpty()) {
            pending.append(entry);
        }
    }

    for (const Pending& entry : std::as_const(pending)) {
        for (const PhosphorEngine::PlacementStateKey& stale : entry.stale) {
            if (SnapState* state = m_states.stateForKey(stale)) {
                // The zone assignment on a desktop the window has left is not
                // a float-back: it is still snapped on the desktops its span
                // does cover, so there is nothing to restore here.
                state->removeWindowData(entry.windowId);
            }
            m_states.removeMembership(entry.windowId, stale);
            // Forgetting a desktop has to be explicit: the store MERGES
            // zonesByDesktop, so a capture that simply stops naming this
            // desktop leaves the old zone on disk forever and a later restart
            // would resurrect the window onto a desktop it no longer occupies.
            if (m_windowTracker) {
                m_windowTracker->placementStore().forgetDesktopZones(entry.windowId, engineId(), stale.desktop);
            }
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: released" << entry.windowId << "from desktop"
                                 << stale.desktop << "of" << stale.screenId << "— its span no longer covers it";
        }
        if (entry.adopt) {
            // Membership only. No zone is chosen for the window here: an
            // unsnapped window on this desktop stays unsnapped, and the store
            // exists so that snapping it later lands in ITS OWN assignment.
            ensureStateForKey(currentKey);
            m_states.addMembership(entry.windowId, currentKey);
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: adopted" << entry.windowId << "into desktop"
                                 << currentKey.desktop << "of" << currentKey.screenId;
        }
    }

    // Whether any window holds a zone in the context just entered. Computed
    // AFTER the arms above, not before: a window adopted into this desktop by
    // this very pass has to count. On a restart that is the entire population
    // — the membership map is rebuilt from scratch, so every multi-desktop
    // window is adopted here rather than arriving already a member.
    bool reapplyCurrent = false;
    if (const SnapState* currentState = m_states.stateForKey(currentKey)) {
        for (const QString& windowId : m_states.trackedWindowIds()) {
            if (m_states.hasMembership(windowId, currentKey) && m_states.membershipsForWindow(windowId).size() > 1
                && !currentState->zonesForWindow(windowId).isEmpty()) {
                reapplyCurrent = true;
                break;
            }
        }
    }

    if (reapplyCurrent) {
        // Scoped to this screen: the other outputs' assignments did not move,
        // and a whole-session resnap would fight whatever they are doing.
        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: re-applying" << screenId << "zones for desktop"
                             << currentKey.desktop << "— a multi-desktop window is snapped here";
        resnapCurrentAssignments(screenId);
    }
}

} // namespace PhosphorSnapEngine
