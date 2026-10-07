// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file navigation_crosssurface.cpp
 * @brief SnapEngine cross-surface resolution helpers.
 *
 * These are SnapEngine members invoked by the in-surface navigation entry points
 * in navigation_actions.cpp when a directional operation reaches a layout
 * boundary and must resolve a landing on a neighbouring virtual desktop or
 * output (the positionally-equivalent zone on a target desktop, the cross-mode
 * tiling-neighbour handoff, and the entry zone / occupant lookups they need).
 */

#include <PhosphorSnapEngine/SnapEngine.h>

#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/SnapState.h>

#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>

#include <algorithm>

namespace PhosphorSnapEngine {

std::pair<QString, QRect> SnapEngine::resolveCrossDesktopZone(const QString& currentZoneId, const QString& screenId,
                                                              int targetDesktop) const
{
    PhosphorZones::Layout* targetLayout =
        m_layoutManager ? m_layoutManager->layoutForScreen(screenId, targetDesktop, currentActivity()) : nullptr;
    if (!targetLayout || targetLayout->zones().isEmpty()) {
        return {};
    }
    // Position is the 1-based enumeration index of zones sorted by zone number
    // (NOT the raw zone number) — the same scheme the resnap path uses.
    const int position =
        PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts()).value(currentZoneId, 0);
    QVector<PhosphorZones::Zone*> zones = targetLayout->zones();
    PhosphorZones::LayoutUtils::sortZonesByNumber(zones);
    PhosphorZones::Zone* targetZone =
        (position >= 1 && position <= zones.size()) ? zones.value(position - 1, nullptr) : nullptr;
    if (!targetZone) {
        return {};
    }
    const QString targetZoneId = targetZone->id().toString();
    const QRect geo = m_windowTracker ? m_windowTracker->zoneGeometry(targetZoneId, screenId) : QRect();
    if (!geo.isValid()) {
        return {};
    }
    return {targetZoneId, geo};
}

bool SnapEngine::tryCrossModeOutput(const QString& windowId, const QString& direction, const QString& screenId,
                                    bool swap)
{
    if (!m_crossSurfaceResolver || !m_layoutManager) {
        return false;
    }
    const QString neighbour = m_crossSurfaceResolver->neighborOutputInDirection(screenId, direction);
    if (neighbour.isEmpty()) {
        return false;
    }
    // Reaching here means the resolver found no snap entry zone on the neighbour
    // (no_adjacent_zone). Any TILING neighbour is a cross-mode handoff; a snap
    // neighbour with no entry zone falls through to the desktop axis, as
    // autotile does (F339). A move inserts the window into the neighbour's stack
    // (or strip); a swap trades it with the entry-edge tile (or column).
    if (!isTilingOutput(neighbour)) {
        return false;
    }
    if (swap) {
        Q_EMIT crossModeSwapRequested(windowId, neighbour, 0, direction);
    } else {
        Q_EMIT crossModeMoveRequested(windowId, neighbour, 0, direction);
    }
    // A refused receive re-homes the window into this screen's store, snapped
    // again, and the OSD says the move failed (F305).
    const SnapState* const home = m_states.stateForKey(currentKeyForScreen(screenId));
    if (home && home->isWindowSnapped(canonicalWindowId(windowId))) {
        Q_EMIT navigationFeedback(false, swap ? QStringLiteral("swap") : QStringLiteral("move"),
                                  QStringLiteral("swap_failed"), QString(), QString(), screenId);
        return true;
    }
    // Same success OSD as the cross-desktop autotile handoff in
    // tryCrossDesktopMove: the daemon's cross-mode handler relocates the
    // window but emits no feedback itself, so without this a successful
    // cross-output handoff is the one nav action with no OSD. Shown on the
    // NEIGHBOUR output — the window lands there, matching the snap-to-snap
    // cross-output convention (the resolver's cross results carry the
    // destination screen).
    Q_EMIT navigationFeedback(true, swap ? QStringLiteral("swap") : QStringLiteral("move"),
                              QStringLiteral("screen:") + direction, QString(), QString(), neighbour);
    return true;
}

QString SnapEngine::windowInZoneOnScreen(const QString& zoneId, const QString& screenId) const
{
    return windowsInZoneInView(zoneId, screenId, QString()).value(0);
}

bool SnapEngine::isTilingOutput(const QString& screenId) const
{
    // Live resolver first, registry cascade as fallback. The live resolver
    // carries the unclaimed-tiling downgrade to Snapping, so mid mode-toggle the
    // two disagree and the live answer is the one the screen runs. Read on the
    // screen's own current desktop; tryCrossDesktopMove keeps the cascade, since
    // the live resolver is keyed on the screen alone and cannot answer for
    // another desktop.
    if (m_liveModeResolver) {
        return m_liveModeResolver(screenId) != PhosphorZones::AssignmentEntry::Snapping;
    }
    return m_layoutManager
        && m_layoutManager->modeForScreen(screenId, currentVirtualDesktopForScreen(screenId), currentActivity())
        != PhosphorZones::AssignmentEntry::Snapping;
}

bool SnapEngine::tryCrossModeFocus(const QString& direction, const QString& screenId)
{
    if (!m_crossSurfaceResolver) {
        return false;
    }
    const QString neighbour = m_crossSurfaceResolver->neighborOutputInDirection(screenId, direction);
    if (neighbour.isEmpty() || !isTilingOutput(neighbour)) {
        return false;
    }
    // The tiling engine names its entry-edge window and the daemon activates
    // it, as the tiling engines already do toward a snapping monitor (F422).
    bool handled = false;
    Q_EMIT crossModeFocusRequested(neighbour, direction, &handled);
    if (!handled) {
        return false;
    }
    Q_EMIT navigationFeedback(true, QStringLiteral("focus"), QStringLiteral("screen:") + direction, QString(),
                              QString(), neighbour);
    return true;
}

QStringList SnapEngine::windowsInZoneInView(const QString& zoneId, const QString& screenId,
                                            const QString& excludeWindowId) const
{
    // The store the screen shows: a window snapped in the same zone on another
    // desktop is not an occupant here, and the store lists each window once.
    if (zoneId.isEmpty() || !m_windowTracker) {
        return {};
    }
    const QString exclude = excludeWindowId.isEmpty() ? QString() : canonicalWindowId(excludeWindowId);
    QStringList result;
    if (const SnapState* state = m_states.stateForKey(currentKeyForScreen(screenId))) {
        for (const QString& windowId : state->windowsInZone(zoneId)) {
            if ((exclude.isEmpty() || windowId != exclude) && holdsWindowInState(windowId, state)
                && !state->isFloating(windowId)) {
                result.append(windowId);
            }
        }
    }
    // A window in no per-context store (an engine on the shared store alone
    // has no desktops to tell apart) is an occupant of its screen.
    for (const QString& windowId : m_windowTracker->windowsInZone(zoneId)) {
        const QString canonical = canonicalWindowId(windowId);
        if ((exclude.isEmpty() || canonical != exclude) && m_states.membershipsForWindow(canonical).isEmpty()
            && m_windowTracker->screenForWindow(windowId) == screenId) {
            result.append(canonical);
        }
    }
    std::sort(result.begin(), result.end());
    result.removeDuplicates();
    return result;
}

QString SnapEngine::entryZoneForCrossing(const QString& direction, const QString& neighbourScreen) const
{
    return entryZoneForCrossing(direction, neighbourScreen, 0);
}

QString SnapEngine::entryZoneForCrossing(const QString& direction, const QString& neighbourScreen, int desktop) const
{
    if (!m_zoneAdjacencyResolver) {
        return {};
    }
    // Enter the neighbour from the edge facing back toward the source (crossing
    // "right" lands on the neighbour's LEFT-edge zone, etc.) — shared mapping.
    const QString opposite = oppositeCrossingDirection(direction);
    if (opposite.isEmpty()) {
        return {};
    }
    return m_zoneAdjacencyResolver->getFirstZoneInDirectionOnDesktop(opposite, neighbourScreen, desktop);
}

void SnapEngine::moveUnsnapped(const QString& windowId, const QString& fromScreen, const QString& toScreen,
                               int toDesktop, const QString& direction)
{
    if (!m_windowTracker || windowId.isEmpty()) {
        return;
    }
    const bool desktopMove = toDesktop >= 1;
    // The landing rect, read before anything is released: the float-back on
    // the destination, else (a monitor crossing only) the one on the source or
    // the live frame, carried onto the destination's area. A desktop move with
    // no float-back keeps the frame it has: desktops share the monitor.
    QRect landing = m_windowTracker->validatedUnmanagedGeometry(windowId, toScreen).value_or(QRect());
    if (!landing.isValid() && !desktopMove) {
        QRect from = m_windowTracker->validatedUnmanagedGeometry(windowId, fromScreen).value_or(QRect());
        if (!from.isValid() && m_navState) {
            from = m_navState->frameGeometry(windowId);
        }
        PhosphorScreens::ScreenManager* const mgr = m_windowTracker->screenManager();
        if (from.isValid() && mgr) {
            landing = PhosphorEngine::GeometryUtils::carryRectOntoArea(from, mgr->screenAvailableGeometry(fromScreen),
                                                                       mgr->screenAvailableGeometry(toScreen));
        }
    }

    // Its snap memory where it leaves goes (memory_clears_on_move).
    const QString canonical = canonicalWindowId(windowId);
    if (desktopMove) {
        QStringList removed;
        bool lastUsedCleared = releaseMembership(windowId, currentKeyForScreen(fromScreen), removed);
        lastUsedCleared |= clearGlobalLastUsedIfRemoved(removed);
        if (lastUsedCleared) {
            m_windowTracker->markLastUsedZoneDirty();
        }
    } else {
        releaseWindowOffScreen(windowId, toScreen);
    }
    if (m_states.membershipsForWindow(canonical).isEmpty()) {
        m_states.removeWindow(canonical);
        m_windowTracker->releaseEngineSlot(windowId, engineId());
    }

    Q_EMIT windowSnapStateChanged(windowId,
                                  PhosphorProtocol::WindowStateEntry{windowId, QString(), QString(), false,
                                                                     QStringLiteral("unsnapped"), QStringList{},
                                                                     false});
    // Applied before the desktop move, while the window is still visible: a
    // suspended client on a hidden desktop does not ack a larger configure.
    if (landing.isValid()) {
        Q_EMIT applyGeometryRequested(windowId, landing.x(), landing.y(), landing.width(), landing.height(), QString(),
                                      toScreen, false);
    }
    if (desktopMove) {
        Q_EMIT windowDesktopMoveRequested(windowId, toDesktop);
    }
    Q_EMIT navigationFeedback(true, QStringLiteral("move"),
                              (desktopMove ? QStringLiteral("desktop:") : QStringLiteral("screen:")) + direction,
                              QString(), QString(), toScreen);
}

} // namespace PhosphorSnapEngine
