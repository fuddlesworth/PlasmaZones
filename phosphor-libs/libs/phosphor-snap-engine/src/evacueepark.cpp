// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The snap engine's evacuee park (see IPlacementEngine's evacuee park
// section). When an output disconnects, KWin moves its windows to another
// one; their zones and floats there are parked here before the removed-screen
// prune drops the stores, and an untouched window KWin brings back is
// re-seated from the park as a re-statement.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "evacueepark_p.h"
#include "snapenginelogging.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>

#include <algorithm>

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
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

QStringList SnapEngine::parkOutput(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return {};
    }
    if (!m_evacueePark) {
        m_evacueePark = std::make_unique<SnapEvacueePark>();
    }
    QHash<QString, QVector<ParkedSnapContext>>& parked = m_evacueePark->byOutput[physicalScreenId];
    QStringList windows;
    const auto& allStates = m_states.states();
    for (auto it = allStates.constBegin(); it != allStates.constEnd(); ++it) {
        SnapState* state = it.value();
        if (!state || it.key().screenId.isEmpty()
            || !VirtualScreenId::samePhysical(it.key().screenId, physicalScreenId)) {
            continue;
        }
        QStringList here = state->snappedWindows();
        for (const QString& windowId : state->floatingWindows()) {
            if (!here.contains(windowId)) {
                here.append(windowId);
            }
        }
        for (const QString& windowId : std::as_const(here)) {
            // A leftover in a store the window is not a member of is not
            // memory of a context it held.
            if (!m_states.hasMembership(windowId, it.key())) {
                continue;
            }
            if (!windows.contains(windowId)) {
                // A second park of the same output replaces the first.
                parked[windowId].clear();
                windows.append(windowId);
            }
            ParkedSnapContext ctx;
            ctx.key = it.key();
            ctx.zoneIds = state->zonesForWindow(windowId);
            ctx.floating = state->isFloating(windowId);
            ctx.preFloatZones = state->preFloatZones(windowId);
            ctx.preFloatScreen = state->preFloatScreen(windowId);
            ctx.autoSnapped = state->isAutoSnapped(windowId);
            parked[windowId].append(ctx);
            if (ctx.floating) {
                m_evacueePark->suppressFloatFalse.insert(windowId);
            }
        }
    }
    if (parked.isEmpty()) {
        m_evacueePark->byOutput.remove(physicalScreenId);
    }
    if (!windows.isEmpty()) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::parkOutput:" << physicalScreenId << "parked" << windows.size() << "window(s)";
    }
    return windows;
}

bool SnapEngine::readoptParked(const QString& windowId, const QString& parkedPhysicalId,
                               const QString& returnedPhysicalId)
{
    if (!m_evacueePark || windowId.isEmpty() || returnedPhysicalId.isEmpty()) {
        return false;
    }
    auto outIt = m_evacueePark->byOutput.find(parkedPhysicalId);
    if (outIt == m_evacueePark->byOutput.end()) {
        return false;
    }
    const QString canonical = canonicalWindowId(windowId);
    const auto winIt = outIt->constFind(canonical);
    if (winIt == outIt->constEnd()) {
        return false;
    }
    const QVector<ParkedSnapContext> contexts = winIt.value();
    outIt->remove(canonical);
    if (outIt->isEmpty()) {
        m_evacueePark->byOutput.erase(outIt);
    }

    PhosphorScreens::ScreenManager* mgr = m_windowTracker ? m_windowTracker->screenManager() : nullptr;
    bool seated = false;
    for (const ParkedSnapContext& ctx : contexts) {
        const QString screen = translateOntoOutput(ctx.key.screenId, returnedPhysicalId, mgr);
        const int desktop = ctx.key.desktop;
        const QString& activity = ctx.key.activity;
        // The context must still be snapping, and a zone must still be in the
        // layout it runs: a desktop flipped to tiling or given another layout
        // while the output was away gets nothing back.
        if (m_layoutManager
            && m_layoutManager->modeForScreen(screen, desktop, activity)
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            continue;
        }
        const bool inView = PhosphorEngine::PlacementStateKey{screen, desktop, activity} == currentKeyForScreen(screen);
        if (ctx.floating) {
            SnapState* state = stateForWindowOnScreen(windowId, screen, desktop);
            state->setFloatingOnScreen(windowId, screen, desktop);
            // The home only when it was on this same output and its zones are
            // in the layout the context runs now.
            if (!ctx.preFloatZones.isEmpty() && VirtualScreenId::samePhysical(ctx.preFloatScreen, parkedPhysicalId)
                && PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, screen, desktop, activity,
                                                                       ctx.preFloatZones)) {
                state->addPreFloatZone(windowId, ctx.preFloatZones);
                state->addPreFloatScreen(windowId, translateOntoOutput(ctx.preFloatScreen, returnedPhysicalId, mgr));
            }
            if (inView && m_windowTracker) {
                m_windowTracker->setWindowFloating(windowId, true);
                Q_EMIT windowFloatingChanged(windowId, true, screen);
            }
        } else if (!ctx.zoneIds.isEmpty()
                   && PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, screen, desktop, activity,
                                                                          ctx.zoneIds)) {
            if (inView) {
                // A re-statement: no last-used, no focus.
                if (ctx.zoneIds.size() > 1) {
                    commitMultiZoneSnap(windowId, ctx.zoneIds, screen, SnapIntent::AutoReplaced, desktop);
                } else {
                    commitSnap(windowId, ctx.zoneIds.first(), screen, SnapIntent::AutoReplaced, desktop);
                }
                const QRect geo = m_windowTracker ? m_windowTracker->resolveZoneGeometry(ctx.zoneIds, screen) : QRect();
                if (geo.isValid()) {
                    Q_EMIT restatementGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(),
                                                        ctx.zoneIds.first(), screen);
                }
            } else {
                // Out of view: the assignment straight into that desktop's
                // store, as a cross-desktop handoff does, and its record.
                SnapState* state = stateForWindowOnScreen(windowId, screen, desktop);
                if (ctx.zoneIds.size() > 1) {
                    state->assignWindowToZones(windowId, ctx.zoneIds, screen, desktop);
                } else {
                    state->assignWindowToZone(windowId, ctx.zoneIds.first(), screen, desktop);
                }
                if (m_windowTracker) {
                    if (auto placement = capturePlacementAtDesktop(windowId, desktop)) {
                        placement->virtualDesktop = desktop;
                        m_windowTracker->placementStore().record(std::move(*placement));
                    }
                }
            }
        } else {
            continue;
        }
        if (ctx.autoSnapped) {
            stateForWindowOnScreen(windowId, screen, desktop)->markAsAutoSnapped(windowId);
        }
        seated = true;
    }
    if (seated) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::readoptParked:" << canonical << "re-seated from"
                                                 << parkedPhysicalId << "on" << returnedPhysicalId;
    }
    return seated;
}

void SnapEngine::dropParked(const QString& windowId, const QString& physicalScreenId, int desktop,
                            const QString& activity)
{
    if (!m_evacueePark || windowId.isEmpty()) {
        return;
    }
    const QString canonical = canonicalWindowId(windowId);
    auto& byOutput = m_evacueePark->byOutput;
    for (auto outIt = byOutput.begin(); outIt != byOutput.end();) {
        if (!physicalScreenId.isEmpty() && outIt.key() != physicalScreenId) {
            ++outIt;
            continue;
        }
        if (auto winIt = outIt->find(canonical); winIt != outIt->end()) {
            QVector<ParkedSnapContext>& contexts = winIt.value();
            contexts.erase(std::remove_if(contexts.begin(), contexts.end(),
                                          [desktop, &activity](const ParkedSnapContext& ctx) {
                                              return (desktop == 0 || ctx.key.desktop == desktop)
                                                  && (activity.isEmpty() || ctx.key.activity == activity);
                                          }),
                           contexts.end());
            if (contexts.isEmpty()) {
                outIt->erase(winIt);
            }
        }
        outIt = outIt->isEmpty() ? byOutput.erase(outIt) : std::next(outIt);
    }
    if (!hasParked(windowId, QString())) {
        m_evacueePark->suppressFloatFalse.remove(canonical);
    }
}

bool SnapEngine::hasParked(const QString& windowId, const QString& physicalScreenId) const
{
    if (!m_evacueePark || windowId.isEmpty()) {
        return false;
    }
    const QString canonical = canonicalWindowId(windowId);
    for (auto outIt = m_evacueePark->byOutput.cbegin(); outIt != m_evacueePark->byOutput.cend(); ++outIt) {
        if ((physicalScreenId.isEmpty() || outIt.key() == physicalScreenId) && outIt->contains(canonical)) {
            return true;
        }
    }
    return false;
}

} // namespace PhosphorSnapEngine
