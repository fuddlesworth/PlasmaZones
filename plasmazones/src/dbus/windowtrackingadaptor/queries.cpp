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
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/VirtualScreenId.h>

namespace PlasmaZones {

QRect WindowTrackingAdaptor::frameGeometry(const QString& windowId) const
{
    // The shadow store is keyed on CANONICAL ids (see setFrameGeometry), so a
    // caller holding the effect's CURRENT composite for a class-mutating app
    // has to be translated before the lookup or it reads an empty rect.
    return m_frameGeometry.value(shadowWindowId(windowId));
}

QStringList WindowTrackingAdaptor::knownWindowIds() const
{
    return m_frameGeometry.keys();
}

QString WindowTrackingAdaptor::resolveBusScreen(const QString& reported, const QString& windowId) const
{
    if (reported.isEmpty()) {
        return {};
    }
    PhosphorScreens::ScreenManager* mgr = m_service ? m_service->screenManager() : nullptr;
    if (!mgr) {
        return reported;
    }
    const QString id = PhosphorScreens::ScreenIdentity::idForName(reported);
    const QStringList known = mgr->effectiveScreenIds();
    if (known.contains(id)) {
        return id;
    }
    if (mgr->hasVirtualScreens(id)) {
        const QRect frame = windowId.isEmpty() ? QRect() : m_frameGeometry.value(shadowWindowId(windowId));
        if (frame.isValid()) {
            const QString vs = Utils::effectiveScreenIdAt(mgr, frame.center());
            if (!vs.isEmpty() && PhosphorScreens::ScreenIdentity::belongsToPhysicalScreen(vs, id)) {
                return vs;
            }
        }
        if (PhosphorIdentity::VirtualScreenId::isVirtual(m_lastCursorScreenId)
            && PhosphorScreens::ScreenIdentity::belongsToPhysicalScreen(m_lastCursorScreenId, id)) {
            return m_lastCursorScreenId;
        }
        const QStringList children = mgr->virtualScreenIdsFor(id);
        if (!children.isEmpty()) {
            return children.first();
        }
    }
    for (const QString& candidate : known) {
        if (PhosphorScreens::ScreenIdentity::screensMatch(candidate, id)) {
            return candidate;
        }
    }
    return {};
}

int WindowTrackingAdaptor::desktopCount() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->desktopCount() : 0;
}

QString WindowTrackingAdaptor::lastActiveScreenName() const
{
    // Prefer where an engine holds the focused window now over the cached
    // m_lastActiveScreenId: KWin fires windowActivated only on focus changes,
    // so a window dragged, snapped or tiled elsewhere without losing focus
    // leaves the cache on the screen it left, and the shortcut router would
    // dispatch to the wrong engine.
    //
    // Lookup order:
    //   1. snap's tracked screen, only where snap runs that screen: memory
    //      kept for a screen another engine took over is not where the
    //      window is (F29);
    //   2. autotile's hold in the context in view;
    //   3. scroll's hold in the context in view (a background desktop's
    //      tile or column says nothing about where the window is, F216);
    //   4. the cache, for windows no engine holds (new windows, dialogs).
    if (!m_lastActiveWindowId.isEmpty()) {
        if (m_snapEngine) {
            const QString tracked = m_snapEngine->screenForTrackedWindow(m_lastActiveWindowId);
            if (!tracked.isEmpty() && m_snapEngine->isActiveOnScreen(tracked)) {
                return tracked;
            }
        }
        if (m_autotileEngine) {
            const QString held = m_autotileEngine->heldScreenForWindow(m_lastActiveWindowId);
            if (!held.isEmpty()) {
                return held;
            }
        }
        if (m_scrollEngine) {
            const QString held = m_scrollEngine->heldScreenForWindow(m_lastActiveWindowId);
            if (!held.isEmpty()) {
                return held;
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

PhosphorProtocol::ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    // The D-Bus contract for this overload is "primary screen". Resolve that
    // screen here and pass its identifier: WindowTrackingService::zoneGeometry()
    // answers INVALID for an empty screen id rather than guessing the primary,
    // because every internal caller means a specific monitor and a wrong guess
    // moves the window to it.
    //
    // The screen manager is asked first, because it is the authority the
    // service resolves the id against and it may front a provider other than
    // Qt's. physicalScreenFor({}) is that manager's own "primary output" answer
    // and returns the TRACKED entry, so the identifier handed back is the one
    // the service will match on. QGuiApplication is the fallback for a service
    // wired without a screen manager.
    QString primaryId;
    if (auto* mgr = m_service->screenManager()) {
        primaryId = mgr->physicalScreenFor(QString()).identifier;
    }
    if (primaryId.isEmpty()) {
        if (QScreen* primary = QGuiApplication::primaryScreen()) {
            primaryId = PhosphorScreens::ScreenIdentity::identifierFor(primary);
        }
    }
    return getZoneGeometryForScreen(zoneId, primaryId);
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
