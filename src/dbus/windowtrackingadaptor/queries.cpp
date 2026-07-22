// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — tracking and zone-geometry queries
//
// Read-only lookups delegating to the WindowTrackingService: zone/window queries,
// empty-zone resolution, frame geometry, and zone-geometry conversion.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "core/resolve/daemongeometryresolver.h"
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include "persistenceworker.h"
#include "dbus/zonedetectionadaptor.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/platform/logging.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "core/types/types.h"
#include <PhosphorEngine/WindowRegistry.h>
// Complete type required where ~WindowTrackingAdaptor destroys the
// unique_ptr<RuleEvaluator> member (m_ruleEvaluator).
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

QRect WindowTrackingAdaptor::frameGeometry(const QString& windowId) const
{
    return m_frameGeometry.value(windowId);
}

QString WindowTrackingAdaptor::lastActiveScreenName() const
{
    // Prefer the active window's live screen tracking from either engine
    // over the cached m_lastActiveScreenId. KWin only fires windowActivated
    // on focus changes, so a window dragged/snapped/tiled to a different
    // VS without losing focus leaves the cache pointing at the source
    // screen — and the shortcut router would then dispatch (float,
    // navigate, etc.) to the wrong engine.
    //
    // Lookup order:
    //   1. snap-side screenForTrackedWindow (covers snap-mode windows
    //      including snap-floated ones whose screen we now preserve through
    //      float, and floating windows received via cross-engine handoff)
    //   2. autotile-side screenForTrackedWindow (covers windows that
    //      crossed engines via drag-insert handoff — snap released its
    //      tracking, autotile took ownership)
    //   3. cached m_lastActiveScreenId (windows neither engine tracks —
    //      brand-new windows pre-tile, dialogs, etc.)
    if (!m_lastActiveWindowId.isEmpty()) {
        if (m_snapEngine) {
            const QString tracked = m_snapEngine->screenForTrackedWindow(m_lastActiveWindowId);
            if (!tracked.isEmpty()) {
                return tracked;
            }
        }
        if (m_autotileEngine) {
            const QString tracked = m_autotileEngine->screenForTrackedWindow(m_lastActiveWindowId);
            if (!tracked.isEmpty()) {
                return tracked;
            }
        }
    }
    return m_lastActiveScreenId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Tracking Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::getZoneForWindow(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("get zone for window"))) {
        return QString();
    }
    // Delegate to service
    return m_service->zoneForWindow(windowId);
}

QStringList WindowTrackingAdaptor::getWindowsInZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "getWindowsInZone: empty zone ID";
        return QStringList();
    }
    // Delegate to service
    return m_service->windowsInZone(zoneId);
}

QStringList WindowTrackingAdaptor::getSnappedWindows()
{
    // Delegate to service
    return m_service->snappedWindows();
}

PhosphorProtocol::EmptyZoneList WindowTrackingAdaptor::getEmptyZones(const QString& screenId)
{
    return m_service->getEmptyZones(screenId);
}

QStringList WindowTrackingAdaptor::getMultiZoneForWindow(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("get multi-zone for window"))) {
        return QStringList();
    }

    // Return stored zone IDs directly (multi-zone support)
    return m_service->zonesForWindow(windowId);
}

QString WindowTrackingAdaptor::getLastUsedZoneId()
{
    // Delegate to service
    return m_service->lastUsedZoneId();
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone Geometry Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::findEmptyZone()
{
    // Use cursor screen for per-screen layout resolution
    return m_service->findEmptyZone(m_lastCursorScreenId);
}

PhosphorProtocol::ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    return getZoneGeometryForScreen(zoneId, QString());
}

PhosphorProtocol::ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometryForScreen(const QString& zoneId,
                                                                                   const QString& screenId)
{
    QRect geo = zoneGeometryRect(zoneId, screenId);
    if (!geo.isValid()) {
        return PhosphorProtocol::ZoneGeometryRect{};
    }
    return PhosphorProtocol::ZoneGeometryRect::fromRect(geo);
}

QRect WindowTrackingAdaptor::zoneGeometryRect(const QString& zoneId, const QString& screenId)
{
    if (zoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "zoneGeometryRect: empty zone ID";
        return QRect();
    }
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        qCDebug(lcDbusWindow) << "zoneGeometryRect: invalid geometry for zone:" << zoneId;
    }
    return geo;
}

} // namespace PlasmaZones
