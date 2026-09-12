// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership: which of a screen's desktops each window holds a
// place in, and the adopt / release pair that maintains it.
//
// A window present on several desktops at once — sticky (all of them) or a
// span such as {1,2} — used to live in exactly one, whichever context was
// current when it opened. On every other desktop it was visible but absent
// from the strip, floating over the columns rather than being one. Here it
// gets a real column in each desktop it spans, so it can be placed and sized
// independently per desktop.
//
// Its own translation unit because engine_context.cpp was at the file-size
// ceiling and this is a separate concern from context lifetime and reaping:
// it has its own entry point, its own daemon triggers (a desktop switch AND a
// sticky-state change), and it ADDS memberships where every path in that file
// moves or drops them.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollenginelogging.h"

#include <algorithm>

namespace PhosphorScrollEngine {

void ScrollEngine::installContextResolver()
{
    // Teach the state container which key each screen is showing, so a window
    // holding a membership on several desktops resolves to the one in view
    // rather than to whichever it was adopted into first. Capturing `this` is
    // safe: the container is a member and cannot outlive the engine.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

void ScrollEngine::reconcileDesktopMemberships(const QString& screenId, const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || !m_scrollingScreens.contains(screenId)) {
        return;
    }
    // TreatAsNormal only. Under RestoreOnly / IgnoreAll a sticky window is
    // floated out of the strip at insertion (see insertOpenedWindow's
    // stickyExcluded arm), so there is no column for it to hold on any
    // desktop and granting memberships would contradict the setting.
    if (effectiveStickyWindowHandling(screenId) != PhosphorEngine::StickyWindowHandling::TreatAsNormal) {
        return;
    }
    const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot first: both arms mutate the membership map, and the adopt arm
    // additionally creates states, so neither may run inside its iteration.
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
        // An EMPTY span is a sticky window (or one whose desktop is unknown),
        // which covers every desktop — so it is never stale anywhere and
        // always wants the context in view.
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
    if (pending.isEmpty()) {
        return;
    }
    bool touchedCurrent = false;
    for (const Pending& entry : std::as_const(pending)) {
        for (const PhosphorEngine::PlacementStateKey& stale : entry.stale) {
            releaseMembership(entry.windowId, stale);
            touchedCurrent |= (stale == currentKey);
        }
        if (entry.adopt && adoptIntoContext(entry.windowId, currentKey)) {
            touchedCurrent = true;
        }
    }
    if (touchedCurrent) {
        // The strip the user is looking at changed shape. Force the batch:
        // the adopted window's rect memory belongs to a DIFFERENT desktop's
        // strip, so the emit-on-change gate cannot be trusted to notice —
        // the same reason the unpin migration arms m_forceEmitScreens.
        m_forceEmitScreens.insert(screenId);
        scheduleRetileForScreen(screenId);
    }
}

bool ScrollEngine::adoptIntoContext(const QString& windowId, const PhosphorEngine::PlacementStateKey& key)
{
    ScrollState* state = stateForKey(key, true);
    if (!state || state->strip().containsWindow(windowId)) {
        return false;
    }
    // Carry the min size across from a strip that already holds it: the
    // compositor reports it once at open, and this window is not opening.
    int minWidth = 0;
    int minHeight = 0;
    for (const PhosphorEngine::PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
        const ScrollState* source = m_states.stateForKey(other);
        if (!source) {
            continue;
        }
        bool found = false;
        for (const Column& column : source->strip().columns()) {
            for (const Tile& tile : column.tiles) {
                if (tile.windowId == windowId) {
                    minWidth = tile.minWidth;
                    minHeight = tile.minHeight;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (found) {
            break;
        }
    }
    // Membership BEFORE the insert, matching windowOpened: the insert can
    // emit windowFloatingStateSynced, and a subscriber querying back
    // synchronously must already see the window as this engine's.
    m_states.addMembership(windowId, key);
    // The applied-geometry memos describe the OTHER desktop's strip and are
    // keyed by window alone, so they would gate this context's first batch
    // against a rect that was never on this strip. Drop them; the retile the
    // caller schedules re-derives every one.
    m_lastAppliedRect.remove(windowId);
    m_parkedScrollEdge.remove(windowId);
    m_lastAppliedWindowedFs.remove(windowId);
    m_lastAppliedMaximizedToEdges.remove(windowId);
    ScrollOpenParams openParams;
    QString displacedTab;
    if (!insertOpenedWindow(state, windowId, key.screenId, minWidth, minHeight, &openParams, /*migration=*/true,
                            &displacedTab)) {
        m_states.removeMembership(windowId, key);
        return false;
    }
    qCInfo(lcScrollEngine) << "reconcileDesktopMemberships: adopted" << windowId << "into desktop" << key.desktop
                           << "of" << key.screenId;
    return true;
}

void ScrollEngine::releaseMembership(const QString& windowId, const PhosphorEngine::PlacementStateKey& key)
{
    if (ScrollState* state = m_states.stateForKey(key)) {
        state->strip().removeWindow(windowId, layoutParamsForKey(key));
        state->removeFloating(windowId);
    }
    m_states.removeMembership(windowId, key);
    // Same reason the adopt arm drops them: what is remembered describes the
    // strip the window just left.
    m_lastAppliedRect.remove(windowId);
    m_parkedScrollEdge.remove(windowId);
    qCInfo(lcScrollEngine) << "reconcileDesktopMemberships: released" << windowId << "from desktop" << key.desktop
                           << "of" << key.screenId << "— its desktop span no longer covers it";
}

} // namespace PhosphorScrollEngine
