// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership: which of a screen's desktops each window holds a
// place in, and the adopt / release pair that maintains it.
//
// The autotile twin of the scroll engine's engine_membership.cpp, and the two
// answer the same question in the same terms. A window present on several
// desktops at once — sticky (all of them) or a span such as {1,2} — used to
// live in exactly one, whichever context was current when it opened; on every
// other desktop it was visible but absent from the layout, overlapping the
// tiles rather than being one. Here it gets a real tile in each desktop it
// spans, so the algorithm places it independently per desktop.
//
// Its own translation unit for the reason the scroll twin gives: this ADDS
// memberships where every path in context.cpp moves or drops them.

#include <PhosphorTileEngine/AutotileEngine.h>

#include <PhosphorTiles/TilingState.h>
#include "tileenginelogging.h"

#include <algorithm>

namespace PhosphorTileEngine {

// The key alias the sibling TUs reach through engine_internal.h.
using PhosphorEngine::TilingStateKey;

void AutotileEngine::installContextResolver()
{
    // Teach the state container which key each screen is showing, so a window
    // holding a membership on several desktops resolves to the one in view
    // rather than to whichever it was adopted into first. Capturing `this` is
    // safe: the container is a member and cannot outlive the engine.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

void AutotileEngine::reconcileDesktopMemberships(const QString& screenId,
                                                 const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || !isAutotileScreen(screenId)) {
        return;
    }
    // No separate TreatAsNormal gate here, unlike the scroll twin: this engine
    // reads the setting inside shouldTileWindow, which refuses a sticky window
    // outright under RestoreOnly and IgnoreAll. The adopt arm consults it
    // below, so those two modes grant no membership without a second check
    // that could drift from the first.
    const TilingStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot first: both arms mutate the membership map and the adopt arm
    // creates states, so neither may run inside its iteration.
    struct Pending
    {
        QString windowId;
        QList<TilingStateKey> stale;
        bool adopt = false;
    };
    QList<Pending> pending;
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<TilingStateKey> held = m_states.membershipsForWindow(windowId);
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
        // which covers every desktop — never stale anywhere, and always
        // wanting the context in view.
        if (!span.isEmpty()) {
            for (const TilingStateKey& key : held) {
                if (key.screenId == screenId && !span.contains(key.desktop)) {
                    entry.stale.append(key);
                }
            }
        }
        entry.adopt = (span.isEmpty() || span.contains(currentKey.desktop))
            && !m_states.hasMembership(windowId, currentKey) && shouldTileWindow(windowId);
        if (entry.adopt || !entry.stale.isEmpty()) {
            pending.append(entry);
        }
    }
    if (pending.isEmpty()) {
        return;
    }
    bool touchedCurrent = false;
    for (const Pending& entry : std::as_const(pending)) {
        for (const TilingStateKey& stale : entry.stale) {
            releaseMembership(entry.windowId, stale);
            touchedCurrent |= (stale == currentKey);
        }
        if (entry.adopt && adoptIntoContext(entry.windowId, currentKey)) {
            touchedCurrent = true;
        }
    }
    if (touchedCurrent) {
        scheduleRetileForScreen(screenId);
    }
}

bool AutotileEngine::adoptIntoContext(const QString& windowId, const TilingStateKey& key)
{
    // insertWindow places into the screen's CURRENT key, which is what the
    // caller resolved, and finishes with setKeyForWindow — a REPLACE. Capture
    // the memberships it is about to displace and put them back: adoption adds
    // a context, it does not move the window out of the ones it already holds.
    const QList<TilingStateKey> previous = m_states.membershipsForWindow(windowId);
    if (!insertWindow(windowId, key.screenId)) {
        return false;
    }
    for (const TilingStateKey& other : previous) {
        m_states.addMembership(windowId, other);
    }
    // Same passive float-state sync the other insert callers perform, so a
    // window this insert floated (matched Float rule, restored saved float)
    // does not desync from the daemon until its next add.
    emitInsertFloatStateSync(windowId, key.screenId);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "reconcileDesktopMemberships: adopted" << windowId << "into desktop" << key.desktop << "of" << key.screenId;
    return true;
}

void AutotileEngine::releaseMembership(const QString& windowId, const TilingStateKey& key)
{
    // Per-context teardown, not removeTrackedWindowNoRetile: that drops the
    // window's tracking outright, which would take the desktops its span
    // still covers with it.
    if (PhosphorTiles::TilingState* state = m_states.stateForKey(key)) {
        if (state->containsWindow(windowId)) {
            // The algorithm's remove hook, so a lifecycle-aware layout keeps
            // its own bookkeeping straight — the same pairing every other
            // removal path makes.
            PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(key.screenId);
            const int index = state->tiledWindows().indexOf(windowId);
            if (algo && algo->supportsLifecycleHooks() && index >= 0) {
                algo->onWindowRemoved(state, index);
            }
            state->removeWindow(windowId);
        }
    }
    m_states.removeMembership(windowId, key);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "reconcileDesktopMemberships: released" << windowId << "from desktop" << key.desktop << "of" << key.screenId
        << "— its desktop span no longer covers it";
}

} // namespace PhosphorTileEngine
