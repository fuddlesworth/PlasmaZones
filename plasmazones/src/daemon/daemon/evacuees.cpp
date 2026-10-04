// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>

namespace PlasmaZones {

void Daemon::retireOutputPlacements(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return;
    }
    // Park first, while the engines still hold the output's contexts: the
    // windows KWin is moving off it may come back. Idempotent, so the effect's
    // settle report and this daemon's own screen-removed handler can both run
    // it for one removal (F690): the second park finds nothing and keeps the
    // first.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->parkOutput(physicalScreenId);
    }
    // All three engines need the explicit whole-output reap: snap's
    // per-(screen,desktop,activity) stores are created lazily on placement,
    // and the two tiling engines' updateEngineScreens sweep only reaps
    // CURRENT-context states, so sibling-context states (other desktops or
    // activities) of the removed output would leak and resurface ghost tiles
    // on replug. Each engine matches every virtual sub-screen of the removed
    // physical id.
    if (m_snapEngine) {
        m_snapEngine->pruneStatesForRemovedScreen(physicalScreenId);
    }
    if (m_autotileEngine) {
        m_autotileEngine->pruneStatesForRemovedScreen(physicalScreenId);
    }
    if (m_scrollEngine) {
        m_scrollEngine->pruneStatesForRemovedScreen(physicalScreenId);
    }
    // The tiling prunes captured each window's slot into its record on the
    // way out; for a parked window those slots name the output that went away.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->releaseParkedSlots(physicalScreenId);
    }
    // A mode round trip's remembered order for the output goes with it: the
    // park holds its windows now, and a replayed order would tile windows
    // that have since moved on (F695).
    for (auto it = m_lastEngineOrders.begin(); it != m_lastEngineOrders.end();) {
        it = PhosphorIdentity::VirtualScreenId::samePhysical(it.key().screenId, physicalScreenId)
            ? m_lastEngineOrders.erase(it)
            : std::next(it);
    }
}

void Daemon::applyScreenDesktopSeed(const QString& screenId, int desktop)
{
    // KWin picks the desktop a returning output shows without a desktop
    // change, and every engine dropped the output's desktop when it went
    // away: without this they lay it out under the desktop they started on
    // while the desktop manager answers the live one (F700). Not a switch,
    // so no OSD, no drag cancel and no switch announce: the engines take the
    // desktop, the managed screens are recomputed, and the windows spanning
    // that desktop get their share.
    if (m_autotileEngine) {
        m_autotileEngine->setCurrentDesktopForScreen(screenId, desktop);
    }
    if (m_snapEngine) {
        m_snapEngine->setCurrentDesktopForScreen(screenId, desktop);
    }
    if (m_scrollEngine) {
        m_scrollEngine->setCurrentDesktopForScreen(screenId, desktop);
    }
    updateEngineScreens();
    if (m_screenManager) {
        reconcileMembershipsForScreens(m_tilingAdaptor, m_screenManager->virtualScreenIdsFor(screenId));
    }
}

} // namespace PlasmaZones
