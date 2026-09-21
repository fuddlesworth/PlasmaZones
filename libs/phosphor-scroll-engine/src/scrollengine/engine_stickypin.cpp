// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The sticky-desktop pin: a screen whose managed windows are ALL on every
// desktop is pinned to the desktop its strip lives on, so a desktop switch
// keeps resolving that strip instead of an empty one ("virtualdesktops
// onlyonprimary"). Split out of engine_context.cpp when the per-desktop
// membership change pushed that file over the size ceiling; the pass shares
// nothing with the prunes there but the members.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

namespace {
// Whether `screenId` holds a non-empty strip on any desktop but `desktop`. A
// screen under "virtualdesktopsonlyonprimary" has no desktop dimension, so it
// never does — which separates it from a screen the user merely happens to be
// viewing with sticky windows on it. Only the former may be pinned: the pin
// outranks the per-output desktop in currentKeyForScreen, so while it is held
// every other desktop's strip on that screen is unreachable. Empty states are
// placeholders a transient lookup minted, so they are not evidence.
//
// The membership pass gives a sticky window a column on every desktop the
// screen visits, so once a screen has been switched with a sticky window on
// it, it holds strips on two desktops and is never pinned again. That is the
// intended outcome: the pin exists so sticky windows keep a strip across a
// switch, and a window with a column in each desktop's strip already does.
bool hasStripOnOtherDesktop(const QHash<PhosphorEngine::PlacementStateKey, ScrollState*>& states,
                            const QString& screenId, int desktop)
{
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (it.key().screenId == screenId && it.key().desktop != desktop && it.value()
            && !it.value()->managedWindows().isEmpty()) {
            return true;
        }
    }
    return false;
}
} // namespace

void ScrollEngine::updateStickyScreenPins(const PhosphorEngine::StickyPredicate& isSticky,
                                          PhosphorEngine::StickyPinPhase phase)
{
    // Windows displaced by an unpin migration, collected across the loop and
    // released AFTER it: windowsReleased is a synchronous signal, and a slot
    // that re-entered the engine (setActiveScreens) from inside the
    // m_scrollingScreens iteration would invalidate the live iterator.
    QStringList displacedWindows;
    QSet<QString> displacedScreens;
    // Screens whose resolved context key MOVED because a pin was released.
    // Collected here and announced after the loop for the same reason the
    // releases are: announceStripContextIfChanged emits synchronously, and a
    // consumer that re-entered the engine from inside this iteration would
    // invalidate the snapshot's premise.
    QSet<QString> contextChangedScreens;
    // Iterate a SNAPSHOT, not the member: the unpin-migration arm cancels a
    // live drag-insert preview, whose synchronous placementChanged reaches
    // the daemon's tiled-count gate and can re-enter setActiveScreens, which
    // REASSIGNS m_scrollingScreens mid-loop. QSet is implicitly shared, so
    // the copy is O(1) and keeps the iterated node hash alive. The per-
    // iteration membership re-check skips a screen such a re-entrant pass
    // removed, instead of migrating it into a torn-down state.
    const QSet<QString> scrollingSnapshot = m_scrollingScreens;
    for (const QString& screenId : scrollingSnapshot) {
        if (!m_scrollingScreens.contains(screenId)) {
            continue;
        }
        const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
        ScrollState* state = m_states.stateForKey(key);
        if (!state) {
            continue;
        }
        const QStringList managed = state->managedWindows();
        if (managed.isEmpty()) {
            // A pinned state that has been emptied has nothing to keep the pin
            // for, and left standing the pin would key every later open on
            // this screen under a desktop the user is not on. Take it; there
            // is nothing to migrate.
            if (phase == PhosphorEngine::StickyPinPhase::Release && m_context.hasStickyPin(screenId)) {
                const int pinnedDesktop = m_context.takeStickyPin(screenId);
                qCInfo(lcScrollEngine) << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop
                                       << "(pinned strip is empty)";
                contextChangedScreens.insert(screenId);
            }
            continue;
        }
        bool allSticky = true;
        for (const QString& wid : managed) {
            if (!isSticky(wid)) {
                allSticky = false;
                break;
            }
        }
        if (allSticky) {
            if (phase == PhosphorEngine::StickyPinPhase::Acquire && !m_context.hasStickyPin(screenId)
                && !hasStripOnOtherDesktop(m_states.states(), screenId, key.desktop)) {
                m_context.setStickyPin(screenId, key.desktop);
                qCInfo(lcScrollEngine) << "Pinning screen" << screenId << "to desktop" << key.desktop << "(all"
                                       << managed.size() << "windows sticky)";
            }
        } else if (phase == PhosphorEngine::StickyPinPhase::Release && m_context.hasStickyPin(screenId)) {
            const int pinnedDesktop = m_context.takeStickyPin(screenId);
            qCInfo(lcScrollEngine) << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop;
            // Migrate the strip to the screen's CURRENT desktop key (the pin
            // was just removed, so currentKeyForScreen resolves the true
            // effective desktop). Same terms as AutotileEngine: the pinned
            // state holds the actual windows, so a placeholder created at
            // the target key by a transient lookup is discarded.
            //
            // This depends on the caller having ALREADY moved the context
            // before running the Release phase, which is what the phase
            // split exists to guarantee. Run it beforehand and the key below
            // resolves to the desktop being LEFT, and the migration drops
            // this strip on top of that desktop's live one — the destination
            // then hits the non-empty branch in migrateStateKey and every
            // window it held is force-released.
            const PhosphorEngine::PlacementStateKey newKey = currentKeyForScreen(screenId);
            if (pinnedDesktop != newKey.desktop) {
                // The strip under this screen has just been replaced, and
                // nothing else will say so: this path does not go through
                // setActiveScreens, which is where the only other announce
                // lives. Gated on the KEY moving rather than on the migration
                // below finding a state, because the epoch is derived from the
                // key — a screen whose old-key state was absent has still
                // changed strips and its consumer still has to be told.
                //
                // The acquire arm above deliberately does NOT announce: it
                // pins the screen to the desktop currentKeyForScreen already
                // resolves, so the key does not move and the announcement
                // would be a guaranteed no-op. A pin only changes what the key
                // resolves to LATER, on a switch, and the announce there
                // correctly stays silent for a pinned screen.
                contextChangedScreens.insert(screenId);
                const PhosphorEngine::PlacementStateKey oldKey{screenId, pinnedDesktop, m_context.currentActivity()};
                if (migrateStateKey(oldKey, newKey, displacedWindows, displacedScreens)) {
                    qCInfo(lcScrollEngine) << "Migrated screen" << screenId << "strip from desktop" << pinnedDesktop
                                           << "to" << newKey.desktop;
                }
            }
        }
    }
    finishDisplacedRelease(displacedWindows, displacedScreens);
    // Strip identity last, AFTER the release above, so a consumer sees the
    // windows leave before it is told the screen is showing a different strip.
    // Re-check membership: a re-entrant setActiveScreens may have taken the
    // screen out of the set since the loop collected it, and the announce is
    // emit-on-change anyway, so a key that ended up where it started is free.
    for (const QString& screenId : std::as_const(contextChangedScreens)) {
        if (!m_scrollingScreens.contains(screenId)) {
            continue;
        }
        // Arm and retile with the announcement, the same pairing
        // setActiveScreens makes: the consumer has just been told to retire
        // this screen's strip-scoped state, so something has to carry the
        // batch that repopulates it. The migration moved the strip to a
        // context whose rects may already match what was last applied, which
        // is precisely the case the emit-on-change gate would suppress.
        if (announceStripContextIfChanged(screenId)) {
            m_forceEmitScreens.insert(screenId);
            scheduleRetileForScreen(screenId);
        }
    }
}

} // namespace PhosphorScrollEngine
