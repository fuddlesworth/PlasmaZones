// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// What a window leaves behind on a screen it is no longer on. A window is on
// one screen, and its snap memory on any other is stale: a zone there names
// another layout, and a membership pass or a resnap on that screen would
// re-apply it and drag the window back across monitors.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

namespace {
/// The kept screen, in any spelling of it (a connector name for an EDID id):
/// a differently spelled report must never release the window's live memory.
bool isKeptScreen(const QString& screenId, const QString& keepScreenId)
{
    return screenId == keepScreenId || PhosphorScreens::ScreenIdentity::screensMatch(screenId, keepScreenId);
}
} // namespace

void SnapEngine::releaseWindowOffScreen(const QString& windowId, const QString& keepScreenId)
{
    if (windowId.isEmpty()) {
        return;
    }
    const QString canonical = canonicalWindowId(windowId);
    QStringList removed;
    int released = 0;
    bool lastUsedCleared = false;
    for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (!keepScreenId.isEmpty() && isKeptScreen(key.screenId, keepScreenId)) {
            continue;
        }
        lastUsedCleared |= releaseMembership(windowId, key, removed);
        ++released;
    }
    // A home kept where the window is not a member (the snap release keeps
    // the capture when a tiling engine takes the window) still answers every
    // pre-float lookup.
    for (SnapState* state : m_states.states()) {
        const QString home = state ? state->preFloatScreen(canonical) : QString();
        if (!home.isEmpty() && (keepScreenId.isEmpty() || !isKeptScreen(home, keepScreenId))) {
            dropPreFloatHome(state, windowId);
        }
    }
    lastUsedCleared |= clearGlobalLastUsedIfRemoved(removed);
    if (lastUsedCleared && m_windowTracker) {
        m_windowTracker->markLastUsedZoneDirty();
    }
    if (released == 0) {
        return;
    }
    if (m_states.membershipsForWindow(canonical).isEmpty()) {
        m_states.removeWindow(canonical);
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::releaseWindowOffScreen:" << canonical << "released"
                                             << released << "membership(s), keeping" << keepScreenId;
}

bool SnapEngine::releaseMembership(const QString& windowId, const PhosphorEngine::PlacementStateKey& key,
                                   QStringList& removed)
{
    const QString canonical = canonicalWindowId(windowId);
    bool lastUsedCleared = false;
    if (SnapState* state = m_states.stateForKey(key)) {
        // The unassign, not removeWindowData alone: it is what clears the
        // store's last-used naming the zone, which a placement on that screen
        // would otherwise reach for.
        if (state->isWindowSnapped(canonical)) {
            removed += state->zonesForWindow(canonical);
            lastUsedCleared = state->unassignWindow(canonical).lastUsedZoneCleared;
        }
        dropPreFloatHome(state, windowId);
        state->removeWindowData(canonical);
    }
    m_states.removeMembership(canonical, key);
    if (m_windowTracker && key.desktop >= 1) {
        m_windowTracker->forgetDesktopZones(canonical, engineId(), key.desktop);
    }
    return lastUsedCleared;
}

bool SnapEngine::clearGlobalLastUsedIfRemoved(const QStringList& removed)
{
    if (m_globals && !m_globals->lastUsedZoneId().isEmpty() && removed.contains(m_globals->lastUsedZoneId())) {
        m_globals->restoreLastUsedZone({}, {}, {}, 0);
        return true;
    }
    return false;
}

void SnapEngine::dropPreFloatHome(SnapState* state, const QString& windowId)
{
    if (!state) {
        return;
    }
    const QString canonical = canonicalWindowId(windowId);
    const QStringList home = state->preFloatZones(canonical);
    state->clearPreFloatZone(canonical);
    if (!m_windowTracker || home.isEmpty()) {
        return;
    }
    // The appId alias the float wrote beside it (for a close and reopen)
    // answers every pre-float lookup from any store, so it goes too, but
    // only while it still names this window's home: a sibling of the same
    // app may have written its own since.
    const QString appId = m_windowTracker->currentAppIdFor(windowId);
    if (!appId.isEmpty() && appId != canonical && state->preFloatZones(appId) == home) {
        state->clearPreFloatZone(appId);
    }
}

} // namespace PhosphorSnapEngine
