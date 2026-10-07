// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// What a window leaves behind on a screen it is no longer on. A window is on
// one screen, and its snap memory on any other is stale: a zone there names
// another layout, and a membership pass or a resnap on that screen would
// re-apply it and drag the window back across monitors. Also the prunes of a
// removed desktop or activity, which release what a window still open there
// held.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "snapenginelogging.h"

#include <QSet>

#include <algorithm>

namespace PhosphorSnapEngine {

namespace {
/// An open window whose every snap membership is in a context a prune drops,
/// and whether it held a zone there.
struct ContextLeaver
{
    QString windowId;
    bool zoned = false;
};

/// The open windows (the registry knows them) a prune by @p matches takes
/// every membership from. A window with a membership left elsewhere keeps its
/// record slot: snap still holds it there.
template<typename Matches>
QList<ContextLeaver> contextLeavers(const PhosphorEngine::PerScreenStates<SnapState>& states,
                                    const PhosphorEngine::IWindowRegistry* registry, const Matches& matches)
{
    QList<ContextLeaver> leavers;
    if (!registry) {
        return leavers;
    }
    for (const QString& windowId : states.trackedWindowIds()) {
        const QList<PhosphorEngine::PlacementStateKey> keys = states.membershipsForWindow(windowId);
        if (keys.isEmpty() || !std::all_of(keys.cbegin(), keys.cend(), matches)
            || !registry->desktopContext(windowId)) {
            continue;
        }
        const bool zoned = std::any_of(keys.cbegin(), keys.cend(), [&](const PhosphorEngine::PlacementStateKey& key) {
            const SnapState* state = states.stateForKey(key);
            return state && state->isWindowSnapped(windowId);
        });
        leavers.append({windowId, zoned});
    }
    return leavers;
}

/// The kept screen, in any spelling of it (a connector name for an EDID id):
/// a differently spelled report must never release the window's live memory.
bool isKeptScreen(const QString& screenId, const QString& keepScreenId)
{
    return screenId == keepScreenId || PhosphorScreens::ScreenIdentity::screensMatch(screenId, keepScreenId);
}
} // namespace

void SnapEngine::pruneStatesForDesktop(int removedDesktop)
{
    // removedDesktop is a real (>= 1) destroyed desktop; the global holder has an
    // empty screenId, so the !screenId.isEmpty() guard excludes it regardless of its
    // key's desktop. Drop every per-key store on the desktop, its reverse-map
    // entries, and the per-output desktop-map entries naming it.
    const auto matches = [removedDesktop](const PhosphorEngine::PlacementStateKey& key) {
        return !key.screenId.isEmpty() && key.desktop == removedDesktop;
    };
    const QList<ContextLeaver> leavers = contextLeavers(m_states, m_windowRegistry, matches);
    m_states.removeStatesIf(
        [&](const PhosphorEngine::PlacementStateKey& key, SnapState*) {
            return matches(key);
        },
        [](const PhosphorEngine::PlacementStateKey&, SnapState* state) {
            state->deleteLater();
        });
    m_states.removeWindowsIf([&](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key);
    });
    m_context.pruneDesktop(removedDesktop);
    // A window still open there leaves its snap memory like any other leave:
    // the record slot is released, and one that held a zone is told it is
    // unsnapped (F122).
    for (const ContextLeaver& leaver : leavers) {
        if (m_windowTracker) {
            m_windowTracker->releaseEngineSlot(leaver.windowId, engineId());
        }
        if (leaver.zoned) {
            Q_EMIT windowSnapStateChanged(leaver.windowId,
                                          PhosphorProtocol::WindowStateEntry{leaver.windowId, QString(), QString(),
                                                                             false, QStringLiteral("unsnapped"),
                                                                             QStringList{}, false});
        }
    }
}

void SnapEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    // An empty list is the activity service going away, not every activity
    // being removed: pruning against it would drop every activity's snap
    // memory for good (F423).
    if (validActivities.isEmpty()) {
        return;
    }
    const QSet<QString> valid(validActivities.begin(), validActivities.end());
    // The global holder has an empty activity, so !activity.isEmpty() excludes it.
    const auto matches = [&valid](const PhosphorEngine::PlacementStateKey& key) {
        return !key.activity.isEmpty() && !valid.contains(key.activity);
    };
    const QList<ContextLeaver> leavers = contextLeavers(m_states, m_windowRegistry, matches);
    m_states.removeStatesIf(
        [&](const PhosphorEngine::PlacementStateKey& key, SnapState*) {
            return matches(key);
        },
        [](const PhosphorEngine::PlacementStateKey&, SnapState* state) {
            state->deleteLater();
        });
    m_states.removeWindowsIf([&](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key);
    });
    // A window still open there leaves its snap memory like any other leave:
    // the record slot is released, and one that held a zone is told it is
    // unsnapped (F122).
    for (const ContextLeaver& leaver : leavers) {
        if (m_windowTracker) {
            m_windowTracker->releaseEngineSlot(leaver.windowId, engineId());
        }
        if (leaver.zoned) {
            Q_EMIT windowSnapStateChanged(leaver.windowId,
                                          PhosphorProtocol::WindowStateEntry{leaver.windowId, QString(), QString(),
                                                                             false, QStringLiteral("unsnapped"),
                                                                             QStringList{}, false});
        }
    }
}

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
    state->clearPreFloatZone(canonicalWindowId(windowId));
}

} // namespace PhosphorSnapEngine
