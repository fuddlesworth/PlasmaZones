// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's evacuee park (see IPlacementEngine's evacuee park
// section). Each strip of an output that disconnects is stashed under its own
// key, exempt from the stash sweeps and the saves while parked; an untouched
// window KWin brings back re-enters through the open path, whose exact-id
// stash claim rebuilds its column.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include "evacueepark_p.h"
#include "scrollenginelogging.h"

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

} // namespace

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
        // that key already has a stash of its own.
        if (key != oldKey && m_stripStash.contains(oldKey) && !m_stripStash.contains(key)) {
            m_stripStash.insert(key, m_stripStash.take(oldKey));
            if (m_stripStashConsumed.contains(oldKey)) {
                m_stripStashConsumed.insert(key, m_stripStashConsumed.take(oldKey));
            }
        }
        if (key == currentKeyForScreen(screen) && m_scrollingScreens.contains(screen)) {
            // The open path, as a re-statement: no focus. Its exact-id stash
            // claim rebuilds the column; a window parked floating floats again.
            const QSize minSize = m_evacueePark->minSizes.value(canonical);
            const bool focusEligible = openFocusEligible();
            setOpenFocusEligible(false);
            windowOpened(canonical, screen, minSize.width(), minSize.height());
            setOpenFocusEligible(focusEligible);
            if (m_evacueePark->floating.contains(canonical)) {
                if (ScrollState* state = stateForKey(key, false); state && state->strip().containsWindow(canonical)) {
                    floatWindowInternal(state, key, canonical, screen, FloatAnnounce::Passive);
                }
            }
            seated = seated || isWindowTracked(canonical);
        } else {
            // Out of view: the stash under that key claims the window by exact
            // id when it arrives in that context.
            seated = true;
        }
        ctxIt = ctxIt->isEmpty() ? outIt->erase(ctxIt) : std::next(ctxIt);
    }
    if (outIt->isEmpty()) {
        m_evacueePark->byOutput.erase(outIt);
    }
    if (!hasParked(canonical, QString())) {
        m_evacueePark->floating.remove(canonical);
        m_evacueePark->minSizes.remove(canonical);
    }
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
                // The stash must never hand this window its parked slot.
                m_stripStashConsumed[key].insert(canonical);
            }
            if (ctxIt->isEmpty()) {
                // Nothing owed under this key any more: its stash goes too.
                m_stripStash.remove(key);
                m_stripStashConsumed.remove(key);
                ctxIt = outIt->erase(ctxIt);
            } else {
                ++ctxIt;
            }
        }
        outIt = outIt->isEmpty() ? m_evacueePark->byOutput.erase(outIt) : std::next(outIt);
    }
    if (!hasParked(canonical, QString())) {
        m_evacueePark->floating.remove(canonical);
        m_evacueePark->minSizes.remove(canonical);
    }
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
    return false;
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
