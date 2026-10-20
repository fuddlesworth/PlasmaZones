// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// moveWindowToZone, swapWindowsById moved to SnapAdaptor
// (src/dbus/snapadaptor/commit.cpp).
//
// getWindowState, getAllWindowStates stay — they are cross-mode queries
// over every engine's state.

#include "windowtrackingadaptor.h"
#include "core/platform/logging.h"
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <array>

namespace PlasmaZones {

PhosphorProtocol::WindowStateEntry WindowTrackingAdaptor::stateRowFor(const QString& windowId) const
{
    // A window a tiling engine tracks reads as that engine has it, unless snap
    // holds it on a context that runs snapping: a snap zone kept on a context
    // that no longer runs snapping is memory for a return, not a placement.
    // The engine holding it in the context in view first, as
    // lastActiveScreenName reads it (F216): another context's tile or column
    // says nothing about where the window is. Else any engine tracking it.
    PhosphorEngine::PlacementEngineBase* tiling = nullptr;
    QString tilingScreen;
    const std::array<PhosphorEngine::PlacementEngineBase*, 2> tilingEngines{m_autotileEngine.data(),
                                                                            m_scrollEngine.data()};
    for (PhosphorEngine::PlacementEngineBase* engine : tilingEngines) {
        const QString held = engine ? engine->heldScreenForWindow(windowId) : QString();
        if (!held.isEmpty()) {
            tiling = engine;
            tilingScreen = held;
            break;
        }
    }
    for (PhosphorEngine::PlacementEngineBase* engine : tilingEngines) {
        if (!tiling && engine && engine->isWindowTracked(windowId)) {
            tiling = engine;
            tilingScreen = engine->screenForTrackedWindow(windowId);
        }
    }
    if (tiling) {
        const auto snapKey = m_snapEngine ? m_snapEngine->heldKeyForWindow(windowId) : std::nullopt;
        const bool snapPlaces = snapKey
            && (!m_layoutManager
                || m_layoutManager->modeForScreen(snapKey->screenId, snapKey->desktop, snapKey->activity)
                    == PhosphorZones::AssignmentEntry::Mode::Snapping);
        if (!snapPlaces) {
            return PhosphorProtocol::WindowStateEntry{
                windowId,
                QString(),
                tilingScreen, // where it is held in view, else where it is tracked
                !tiling->isWindowTiled(windowId),
                QString(), // changeType: empty for query
                QStringList(),
                m_service->isWindowSticky(windowId),
            };
        }
    }
    return PhosphorProtocol::WindowStateEntry{
        windowId,
        m_service->zoneForWindow(windowId),
        m_service->screenForWindow(windowId),
        m_service->isWindowFloating(windowId),
        QString(), // changeType: empty for query (not a state change event)
        m_service->zonesForWindow(windowId),
        m_service->isWindowSticky(windowId),
    };
}

PhosphorProtocol::WindowStateEntry WindowTrackingAdaptor::getWindowState(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "getWindowState: empty window ID";
        return PhosphorProtocol::WindowStateEntry{};
    }
    return stateRowFor(windowId);
}

PhosphorProtocol::WindowStateList WindowTrackingAdaptor::getAllWindowStates()
{
    // Every window an engine holds: snap's snapped and floating windows, and
    // every registered window a tiling engine tracks in any context (a
    // background desktop's tile included).
    QSet<QString> allWindowIds;
    const QStringList snapped = m_service->snappedWindows();
    for (const QString& windowId : snapped) {
        allWindowIds.insert(windowId);
    }
    const QStringList floating = m_service->floatingWindows();
    for (const QString& windowId : floating) {
        allWindowIds.insert(windowId);
    }
    // m_shellWindowFacts holds a row for every registered window, keyed by its
    // canonical id; WindowRegistry has no walk over every id.
    if (m_autotileEngine || m_scrollEngine) {
        for (auto it = m_shellWindowFacts.cbegin(); it != m_shellWindowFacts.cend(); ++it) {
            const QString& windowId = it.key();
            if ((m_autotileEngine && m_autotileEngine->isWindowTracked(windowId))
                || (m_scrollEngine && m_scrollEngine->isWindowTracked(windowId))) {
                allWindowIds.insert(windowId);
            }
        }
    }

    PhosphorProtocol::WindowStateList result;
    result.reserve(allWindowIds.size());
    for (const QString& windowId : std::as_const(allWindowIds)) {
        result.append(stateRowFor(windowId));
    }
    return result;
}

} // namespace PlasmaZones
