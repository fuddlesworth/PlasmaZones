// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "core/platform/logging.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QSet>

#include <algorithm>

namespace PlasmaZones {

void Daemon::retireOutputPlacements(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return;
    }
    // A live drag-insert preview on the departing output (any virtual
    // sub-screen of it) must unwind BEFORE the prunes tear its states down:
    // the scroll engine's prune cancels internally, the autotile prune does
    // not, and neither caller may depend on which engine holds the preview.
    // Here and not in the screen-removed handler alone, so the effect's
    // settle report, which can retire the output first, cancels too (F1043).
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->cancelDragInsertPreviewsForScreen(physicalScreenId);
    }
    // Park next, while the engines still hold the output's contexts: the
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

void Daemon::pruneEngineOrdersForWindow(const QString& instanceId, const QString& keepScreenId)
{
    if (instanceId.isEmpty() || m_lastEngineOrders.isEmpty()) {
        return;
    }
    for (auto it = m_lastEngineOrders.begin(); it != m_lastEngineOrders.end();) {
        // A window that moved keeps its place in the orders of the screen it
        // is on: only the screens it left lose it.
        if (!keepScreenId.isEmpty() && PhosphorScreens::ScreenIdentity::screensMatch(it.key().screenId, keepScreenId)) {
            ++it;
            continue;
        }
        QStringList& order = it.value();
        const int before = order.size();
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&instanceId](const QString& wid) {
                                       return PhosphorIdentity::WindowId::extractInstanceId(wid) == instanceId;
                                   }),
                    order.end());
        if (order.isEmpty()) {
            it = m_lastEngineOrders.erase(it);
        } else {
            if (order.size() != before) {
                qCDebug(lcDaemon) << "Pruned" << (keepScreenId.isEmpty() ? "closed" : "moved") << "window" << instanceId
                                  << "from saved autotile order for screen=" << it.key().screenId
                                  << "desktop=" << it.key().desktop;
            }
            ++it;
        }
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

void Daemon::reseedScreenDesktopsAfterRenumber()
{
    // Every engine's desktop prune dropped the per-output desktop of an output
    // showing the removed desktop. KWin hands that output the successor, which
    // takes the removed number, so nothing reports a change there and the
    // engines laid it out under the desktop the session started on until its
    // next switch (F799). The desktop manager's map is renumbered by now, so
    // push it back as a seed. Pushed per PHYSICAL output, the key the effect
    // reports under; an output whose number did not move gets the value it
    // holds, which changes nothing.
    if (!m_screenManager || !m_virtualDesktopManager) {
        return;
    }
    QSet<QString> outputs;
    for (const QString& screenId : m_screenManager->effectiveScreenIds()) {
        outputs.insert(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    }
    for (const QString& output : std::as_const(outputs)) {
        const int desktop = m_virtualDesktopManager->currentDesktopForScreen(output);
        for (PhosphorEngine::PlacementEngineBase* engine :
             {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
            if (engine) {
                engine->setCurrentDesktopForScreen(output, desktop);
            }
        }
    }
    updateEngineScreens();
}

} // namespace PlasmaZones
