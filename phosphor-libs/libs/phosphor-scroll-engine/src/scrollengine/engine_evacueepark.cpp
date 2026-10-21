// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's evacuee park (see IPlacementEngine's evacuee park
// section). Each strip of an output that disconnects is stashed under its own
// key, exempt from the leave, aliveness and output sweeps and the saves while
// parked (a desktop or activity prune drops it); an untouched tile KWin brings
// back re-enters through the open path, whose exact-id stash claim rebuilds its
// column, and a window parked floating is seated floating where it stands.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include "evacueepark_p.h"
#include "scrollenginelogging.h"

#include <algorithm>

namespace PhosphorScrollEngine {

namespace VirtualScreenId = PhosphorIdentity::VirtualScreenId;

namespace {

/// @p screenId carried onto @p returnedPhysical, on the bare output when the
/// returned output no longer has that virtual screen.
QString translateOntoOutput(const QString& screenId, const QString& returnedPhysical,
                            PhosphorScreens::ScreenManager* mgr)
{
    const QString screen = VirtualScreenId::onOutput(screenId, returnedPhysical);
    return screen == returnedPhysical || !mgr || mgr->screenGeometry(screen).isValid() ? screen : returnedPhysical;
}

/// Whether @p stash has a tile for @p windowId. Floats are never tiles there.
bool stashHoldsTile(const StashedStrip& stash, const QString& windowId)
{
    return std::any_of(stash.columns.cbegin(), stash.columns.cend(), [&windowId](const StashedColumn& column) {
        return std::any_of(column.tiles.cbegin(), column.tiles.cend(), [&windowId](const StashedTile& tile) {
            return tile.windowId == windowId;
        });
    });
}

} // namespace

bool ScrollEvacueePark::owes(const QString& windowId) const
{
    for (const auto& contexts : byOutput) {
        for (const QSet<QString>& windows : contexts) {
            if (windows.contains(windowId)) {
                return true;
            }
        }
    }
    return false;
}

void ScrollEvacueePark::forgetUnowed(const QString& windowId)
{
    if (!owes(windowId)) {
        floating.remove(windowId);
        minSizes.remove(windowId);
    }
    forgetCommittedIfUnheld(windowId);
}

bool ScrollEvacueePark::hasGrantFor(const QString& windowId) const
{
    return std::any_of(grants.cbegin(), grants.cend(), [&windowId](const QHash<QString, bool>& windows) {
        return windows.contains(windowId);
    });
}

void ScrollEvacueePark::forgetCommittedIfUnheld(const QString& windowId)
{
    if (!owes(windowId) && !hasGrantFor(windowId)) {
        committed.remove(windowId);
    }
}

void ScrollEvacueePark::dropContextsWhere(const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& gone)
{
    QSet<QString> released;
    for (auto outIt = byOutput.begin(); outIt != byOutput.end();) {
        for (auto ctxIt = outIt->begin(); ctxIt != outIt->end();) {
            if (gone(ctxIt.key())) {
                released.unite(ctxIt.value());
                ctxIt = outIt->erase(ctxIt);
            } else {
                ++ctxIt;
            }
        }
        outIt = outIt->isEmpty() ? byOutput.erase(outIt) : std::next(outIt);
    }
    // A window still owed elsewhere keeps them: another of its contexts
    // re-seats it with the same float verdict and clamp.
    for (const QString& windowId : std::as_const(released)) {
        forgetUnowed(windowId);
    }
    dropGrantsWhere(QString(), gone);
}

bool ScrollEvacueePark::owesUnder(const PhosphorEngine::PlacementStateKey& key, const QString& windowId) const
{
    for (const auto& contexts : byOutput) {
        if (const auto it = contexts.constFind(key); it != contexts.cend() && it->contains(windowId)) {
            return true;
        }
    }
    return false;
}

bool ScrollEvacueePark::hasTileGrant(const PhosphorEngine::PlacementStateKey& key) const
{
    const auto it = grants.constFind(key);
    if (it == grants.cend()) {
        return false;
    }
    for (const bool parkedFloating : *it) {
        if (!parkedFloating) {
            return true;
        }
    }
    return false;
}

std::optional<bool> ScrollEvacueePark::takeGrant(const PhosphorEngine::PlacementStateKey& key, const QString& windowId)
{
    const auto it = grants.find(key);
    if (it == grants.end() || !it->contains(windowId)) {
        return std::nullopt;
    }
    const bool parkedFloating = it->take(windowId);
    if (it->isEmpty()) {
        grants.erase(it);
    }
    forgetCommittedIfUnheld(windowId);
    return parkedFloating;
}

void ScrollEvacueePark::dropGrantsWhere(const QString& windowId,
                                        const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& gone)
{
    for (auto it = grants.begin(); it != grants.end();) {
        if (gone(it.key())) {
            if (windowId.isEmpty()) {
                it->clear();
            } else {
                it->remove(windowId);
            }
        }
        it = it->isEmpty() ? grants.erase(it) : std::next(it);
    }
    if (!windowId.isEmpty()) {
        forgetCommittedIfUnheld(windowId);
        return;
    }
    const QStringList held = committed.keys();
    for (const QString& id : held) {
        forgetCommittedIfUnheld(id);
    }
}

QStringList ScrollEngine::parkOutput(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return {};
    }
    if (!m_evacueePark) {
        m_evacueePark = std::make_unique<ScrollEvacueePark>();
    }
    // Collected first: the stash resolves the axis through the injected
    // providers, which must not run inside a walk over the state map.
    QList<QPair<PhosphorEngine::PlacementStateKey, ScrollState*>> matching;
    const auto& allStates = m_states.states();
    for (auto it = allStates.constBegin(); it != allStates.constEnd(); ++it) {
        if (it.value() && !it.key().screenId.isEmpty()
            && VirtualScreenId::samePhysical(it.key().screenId, physicalScreenId)) {
            matching.append({it.key(), it.value()});
        }
    }
    QHash<PhosphorEngine::PlacementStateKey, QSet<QString>> parked;
    QStringList windows;
    for (const auto& [key, state] : std::as_const(matching)) {
        const QStringList held = state->managedWindows();
        if (held.isEmpty()) {
            continue;
        }
        stashStripStructure(key, state, stripAxisForScreen(key.screenId).axis());
        parked.insert(key, QSet<QString>(held.begin(), held.end()));
        for (const QString& windowId : held) {
            if (state->isFloating(windowId)) {
                m_evacueePark->floating.insert(windowId);
            }
            m_evacueePark->minSizes.insert(windowId, windowMinimumSize(windowId));
            // The release below forgets the window's committed extents with
            // the rest of its engine state; the park carries them back.
            if (const auto committed = m_committedExtents.constFind(windowId); committed != m_committedExtents.cend()) {
                m_evacueePark->committed.insert(windowId, *committed);
            }
            if (!windows.contains(windowId)) {
                windows.append(windowId);
            }
        }
    }
    // A second park of the same output replaces the first, but only with
    // something to park: once the prune has run there is nothing left, and the
    // daemon's retire can run twice for one removal.
    if (!parked.isEmpty()) {
        m_evacueePark->byOutput.insert(physicalScreenId, std::move(parked));
        qCInfo(lcScrollEngine) << "ScrollEngine::parkOutput:" << physicalScreenId << "parked" << windows.size()
                               << "window(s)";
    }
    return windows;
}

bool ScrollEngine::readoptParked(const QString& windowId, const QString& parkedPhysicalId,
                                 const QString& returnedPhysicalId)
{
    if (!m_evacueePark || windowId.isEmpty() || returnedPhysicalId.isEmpty()) {
        return false;
    }
    auto outIt = m_evacueePark->byOutput.find(parkedPhysicalId);
    if (outIt == m_evacueePark->byOutput.end()) {
        return false;
    }
    const QString canonical = canonicalizeForLookup(windowId);
    bool seated = false;
    // The committed extents the park carried go back BEFORE any seat below:
    // the in-view open path lays the column out at once, and it has to read
    // the floor the client already taught. Taken back out after the walk when
    // nothing here ended up holding the window (a grant keeps the park's copy
    // for the arrival instead). A window some strip already holds keeps its
    // own live entry.
    const bool committedRestored = !isWindowTracked(canonical) && !m_committedExtents.contains(canonical)
        && m_evacueePark->committed.contains(canonical);
    if (committedRestored) {
        m_committedExtents.insert(canonical, m_evacueePark->committed.value(canonical));
    }
    // Contexts whose stash moved onto the key they return under, put back
    // under that key once the walk is done.
    QList<QPair<PhosphorEngine::PlacementStateKey, QSet<QString>>> rekeyed;
    // Out-of-view contexts to seat the window into once the walk is done.
    QList<PhosphorEngine::PlacementStateKey> outOfView;
    for (auto ctxIt = outIt->begin(); ctxIt != outIt->end();) {
        if (!ctxIt->contains(canonical)) {
            ++ctxIt;
            continue;
        }
        ctxIt->remove(canonical);
        const PhosphorEngine::PlacementStateKey oldKey = ctxIt.key();
        const QString screen = translateOntoOutput(oldKey.screenId, returnedPhysicalId, m_screenManager);
        const PhosphorEngine::PlacementStateKey key{screen, oldKey.desktop, oldKey.activity};
        // The stash follows the context onto the key it returns under, unless
        // that key already has a stash of its own. The windows the context
        // still owes follow it too: isEvacueeParkedKey is what exempts the
        // moved stash from the sweeps and the saves.
        bool stashMoved = false;
        if (key != oldKey && m_stripStash.contains(oldKey) && !m_stripStash.contains(key)) {
            m_stripStash.insert(key, m_stripStash.take(oldKey));
            if (m_stripStashConsumed.contains(oldKey)) {
                m_stripStashConsumed.insert(key, m_stripStashConsumed.take(oldKey));
            }
            stashMoved = true;
        }
        if (key == currentKeyForScreen(screen) && m_scrollingScreens.contains(screen)) {
            const QSize minSize = m_evacueePark->minSizes.value(canonical);
            if (m_evacueePark->floating.contains(canonical)) {
                // A window parked floating is seated floating where KWin put it
                // back, like handoffReceive's floating arm. The open path would
                // give it a column first, and that column's apply moves it onto
                // the column rect before any float could leave it in place.
                ScrollState* state = stateForKey(key, true);
                if (state && !isWindowTracked(canonical)) {
                    m_closedFullscreenHolds.remove(canonical);
                    m_lastAppliedRect.remove(canonical);
                    m_parkedScrollEdge.remove(canonical);
                    state->addFloating(canonical);
                    seedFloatRestoreForOpen(canonical, minSize.width(), minSize.height());
                    m_scrollFloatedWindows.insert(canonical);
                    m_states.setKeyForWindow(canonical, key);
                    consumePendingInitialOrder(screen, canonical);
                    announceFloat(FloatAnnounce::Passive, canonical, true, screen);
                    Q_EMIT placementChanged(screen);
                }
            } else {
                // The open path, as a re-statement: no focus. Its exact-id
                // stash claim rebuilds the column.
                const bool focusEligible = openFocusEligible();
                setOpenFocusEligible(false);
                windowOpened(canonical, screen, minSize.width(), minSize.height());
                setOpenFocusEligible(focusEligible);
            }
            seated = seated || isWindowTracked(canonical);
        } else if (m_scrollingModeResolver && !m_scrollingModeResolver(key.screenId, key.desktop, key.activity)) {
            // A context given another mode while the output was away gets
            // nothing back, as snap's re-adopt refuses a desktop no longer
            // snapping. m_scrollingScreens speaks only for the contexts in
            // view, so the mode the context runs decides, as the daemon
            // resolves it: a scrolling assignment with scrolling switched off
            // or disabled there does not scroll (F1102, F1137).
        } else if (m_scrollingScreens.contains(screen)) {
            // Out of view on a screen this engine scrolls: seated straight into
            // that context, after the walk. Nothing announces the window again
            // when its desktop is shown (the effect tracked it at the re-adopt
            // without an announce), so a claim left to that arrival never ran
            // and the strip came back empty.
            if (!outOfView.contains(key)) {
                outOfView.append(key);
            }
        } else {
            // The screen is not scrolling yet: the stash under that key claims
            // the window by exact id when it arrives there. It does arrive: the
            // screen's return to scrolling drops the effect's tracking of every
            // window on it and announces those in view, and the switch to an
            // out-of-view context announces the rest (F1099). A window parked
            // floating has no tile there, so its grant carries the verdict
            // instead, and the arrival seats it floating like the in-view arm
            // above. A tile's grant keeps the stash parked until the tile
            // arrives: the context entry may go below once every window is
            // granted (F1097).
            const bool parkedFloating = m_evacueePark->floating.contains(canonical);
            const auto stashIt = m_stripStash.constFind(key);
            if (parkedFloating || (stashIt != m_stripStash.cend() && stashHoldsTile(*stashIt, canonical))) {
                m_evacueePark->grants[key].insert(canonical, parkedFloating);
            }
            seated = true;
        }
        if (ctxIt->isEmpty()) {
            ctxIt = outIt->erase(ctxIt);
        } else if (stashMoved) {
            rekeyed.append({key, *ctxIt});
            ctxIt = outIt->erase(ctxIt);
        } else {
            ++ctxIt;
        }
    }
    for (const auto& [key, windows] : std::as_const(rekeyed)) {
        (*outIt)[key].unite(windows);
    }
    if (outIt->isEmpty()) {
        m_evacueePark->byOutput.erase(outIt);
    }
    // After the walk, so an in-view re-seat (whose open path makes its context
    // the window's only membership) has run first, and before forgetUnowed
    // takes the float verdict and the clamp. A background context: no apply
    // and no focus, the desktop switch lays the strip out.
    const QSize minSize = m_evacueePark->minSizes.value(canonical);
    const bool parkedFloating = m_evacueePark->floating.contains(canonical);
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(outOfView)) {
        ScrollState* state = stateForKey(key, true);
        if (!state) {
            continue;
        }
        // Seated counts only a context that holds the window: forgetUnowed below
        // takes the float verdict and the clamp either way (F1104).
        if (state->containsWindow(canonical)) {
            seated = true;
            continue;
        }
        if (!isWindowTracked(canonical)) {
            // windowOpened's re-adoption hygiene: memories from before the park.
            m_closedFullscreenHolds.remove(canonical);
            m_lastAppliedRect.remove(canonical);
            m_parkedScrollEdge.remove(canonical);
        }
        m_states.addMembership(canonical, key);
        if (parkedFloating) {
            state->addFloating(canonical);
            seedFloatRestoreForOpen(canonical, minSize.width(), minSize.height());
            m_scrollFloatedWindows.insert(canonical);
            announceFloat(FloatAnnounce::Passive, canonical, true, key.screenId);
        } else {
            // The exact-id stash claim rebuilds the column; with no tile left
            // to claim, the window takes a column at the strip's end.
            const ScrollLayoutParams params = layoutParamsForKey(key);
            if (!restoreFromStripStash(state, key, canonical, params, minSize.width(), minSize.height())) {
                if (!state->strip().insertWindowAt(state->strip().columnCount(), canonical, params.defaultColumnWidth,
                                                   effectiveDefaultColumnDisplay(key.screenId), params)) {
                    m_states.removeMembership(canonical, key);
                    continue;
                }
                state->strip().setWindowMinimumSize(canonical, minSize.width(), minSize.height());
            }
        }
        seated = true;
        Q_EMIT placementChanged(key.screenId);
    }
    if (committedRestored && !isWindowTracked(canonical)) {
        m_committedExtents.remove(canonical);
    }
    m_evacueePark->forgetUnowed(canonical);
    if (seated) {
        qCInfo(lcScrollEngine) << "ScrollEngine::readoptParked:" << canonical << "re-seated from" << parkedPhysicalId
                               << "on" << returnedPhysicalId;
    }
    return seated;
}

void ScrollEngine::dropParked(const QString& windowId, const QString& physicalScreenId, int desktop,
                              const QString& activity)
{
    if (!m_evacueePark || windowId.isEmpty()) {
        return;
    }
    const QString canonical = canonicalizeForLookup(windowId);
    // The stash must never hand this window its parked slot. Only a tile is
    // marked: a window parked floating has none, and an id in the consumed set
    // that names no tile would count toward the all-consumed drop and retire
    // the stash before the tiles it still owes are claimed.
    const auto markConsumed = [this, &canonical](const PhosphorEngine::PlacementStateKey& key) {
        const auto stashIt = m_stripStash.constFind(key);
        if (stashIt != m_stripStash.cend() && stashHoldsTile(*stashIt, canonical)) {
            m_stripStashConsumed[key].insert(canonical);
        }
    };
    // A grant already handed out on the same terms goes first, a granted tile
    // marked like a parked one, so the stash test below sees what is left.
    QList<PhosphorEngine::PlacementStateKey> ungranted;
    for (auto it = m_evacueePark->grants.begin(); it != m_evacueePark->grants.end();) {
        const PhosphorEngine::PlacementStateKey key = it.key();
        const bool matches =
            (physicalScreenId.isEmpty() || VirtualScreenId::samePhysical(key.screenId, physicalScreenId))
            && (desktop == 0 || key.desktop == desktop) && (activity.isEmpty() || key.activity == activity);
        if (matches && it->contains(canonical) && !it->take(canonical)) {
            markConsumed(key);
            ungranted.append(key);
        }
        it = it->isEmpty() ? m_evacueePark->grants.erase(it) : std::next(it);
    }
    for (auto outIt = m_evacueePark->byOutput.begin(); outIt != m_evacueePark->byOutput.end();) {
        if (!physicalScreenId.isEmpty() && outIt.key() != physicalScreenId) {
            ++outIt;
            continue;
        }
        for (auto ctxIt = outIt->begin(); ctxIt != outIt->end();) {
            const PhosphorEngine::PlacementStateKey key = ctxIt.key();
            const bool matches =
                (desktop == 0 || key.desktop == desktop) && (activity.isEmpty() || key.activity == activity);
            if (matches && ctxIt->remove(canonical)) {
                markConsumed(key);
            }
            if (ctxIt->isEmpty()) {
                // Nothing owed under this key any more: its stash goes too,
                // unless a granted tile has yet to claim it.
                if (!m_evacueePark->hasTileGrant(key)) {
                    m_stripStash.remove(key);
                    m_stripStashConsumed.remove(key);
                }
                ctxIt = outIt->erase(ctxIt);
            } else {
                ++ctxIt;
            }
        }
        outIt = outIt->isEmpty() ? m_evacueePark->byOutput.erase(outIt) : std::next(outIt);
    }
    // A stash kept only for the tile grant just dropped is owed to no one now.
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(ungranted)) {
        if (!isEvacueeParkedKey(key)) {
            m_stripStash.remove(key);
            m_stripStashConsumed.remove(key);
        }
    }
    m_evacueePark->forgetUnowed(canonical);
}

bool ScrollEngine::hasParked(const QString& windowId, const QString& physicalScreenId) const
{
    if (!m_evacueePark || windowId.isEmpty()) {
        return false;
    }
    const QString canonical = canonicalizeForLookup(windowId);
    for (auto outIt = m_evacueePark->byOutput.cbegin(); outIt != m_evacueePark->byOutput.cend(); ++outIt) {
        if (!physicalScreenId.isEmpty() && outIt.key() != physicalScreenId) {
            continue;
        }
        for (const QSet<QString>& windows : outIt.value()) {
            if (windows.contains(canonical)) {
                return true;
            }
        }
    }
    return false;
}

bool ScrollEngine::isEvacueeParkedKey(const PhosphorEngine::PlacementStateKey& key) const
{
    if (!m_evacueePark) {
        return false;
    }
    for (const auto& contexts : std::as_const(m_evacueePark->byOutput)) {
        if (contexts.contains(key)) {
            return true;
        }
    }
    // Still parked while a granted tile has yet to arrive: the
    // context entry is gone once every window is granted, and its stash would
    // otherwise meet the sweeps and the save before the claims it is kept for.
    return m_evacueePark->hasTileGrant(key);
}

void ScrollEngine::forceReemit(const QString& screenId)
{
    if (screenId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }
    m_forceEmitScreens.insert(screenId);
    scheduleRetileForScreen(screenId);
}

} // namespace PhosphorScrollEngine
