// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file navigation_crosssurface.cpp
 * @brief SnapEngine cross-surface resolution helpers.
 *
 * Split out of navigation_actions.cpp to keep that TU under the 1000-line guideline.
 * These are SnapEngine members invoked by the in-surface navigation entry points
 * in navigation_actions.cpp when a directional operation reaches a layout
 * boundary and must resolve a landing on a neighbouring virtual desktop or
 * output (the positionally-equivalent zone on a target desktop, the cross-mode
 * autotile-neighbour handoff, and the entry zone / occupant lookups they need).
 */

#include <PhosphorSnapEngine/SnapEngine.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/IWindowTrackingService.h>

#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>

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
    // (the resolver returned no_adjacent_zone). Only an AUTOTILE neighbour is a
    // cross-mode handoff; a snap neighbour with no entry zone is a genuine
    // boundary — leave it. A move inserts the window into the neighbour's stack;
    // a swap trades it with the neighbour's entry-edge tile.
    if (m_layoutManager->modeForScreen(neighbour, currentVirtualDesktopForScreen(neighbour), currentActivity())
        != PhosphorZones::AssignmentEntry::Autotile) {
        return false;
    }
    if (swap) {
        Q_EMIT crossModeSwapRequested(windowId, neighbour, 0, direction);
    } else {
        Q_EMIT crossModeMoveRequested(windowId, neighbour, 0, direction);
    }
    return true;
}

QString SnapEngine::windowInZoneOnScreen(const QString& zoneId, const QString& screenId) const
{
    if (!m_windowTracker || zoneId.isEmpty()) {
        return QString();
    }
    // windowsInZone is screen-agnostic (a zone UUID is shared by every output the
    // layout drives), so pin to the daemon's stored screen assignment. The
    // screenForWindow point accessor also canonicalizes the id (issue #628),
    // which a raw flat-map .value() lookup never did.
    const QStringList windows = m_windowTracker->windowsInZone(zoneId);
    for (const QString& windowId : windows) {
        if (m_windowTracker->screenForWindow(windowId) == screenId) {
            return windowId;
        }
    }
    return QString();
}

QString SnapEngine::entryZoneForCrossing(const QString& direction, const QString& neighbourScreen) const
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
    return m_zoneAdjacencyResolver->getFirstZoneInDirection(opposite, neighbourScreen);
}

} // namespace PhosphorSnapEngine
