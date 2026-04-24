// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// toggleFloatForWindow, calculateUnfloatRestore, windowUnsnappedForFloat
// moved to SnapAdaptor (src/dbus/snapadaptor/commit.cpp).
//
// This file retains cross-mode float methods that stay on WTA:
//   notifyDragOutUnsnap, getPreFloatZone, clearPreFloatZone,
//   isWindowFloating, queryWindowFloating, setWindowFloating (WTS delegate),
//   getFloatingWindows, applyGeometryForFloat, setWindowFloatingForScreen.

#include "../windowtrackingadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../core/interfaces.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../snap/SnapEngine.h"
#include <PhosphorEngineApi/PlacementEngineBase.h>

namespace PlasmaZones {

void WindowTrackingAdaptor::notifyDragOutUnsnap(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("drag-out unsnap"))) {
        return;
    }

    QString zoneId = m_service->zoneForWindow(windowId);
    if (zoneId.isEmpty()) {
        // Window was not snapped — nothing to do
        return;
    }

    QString screenId = m_service->screenAssignments().value(windowId, m_lastActiveScreenId);
    qCInfo(lcDbusWindow) << "Drag-out unsnap (no activation trigger) for" << windowId << "screen:" << screenId;

    // Delegate unsnap-for-float to the service directly (the SnapAdaptor
    // method windowUnsnappedForFloat does the same thing, but this path
    // doesn't need the SnapAdaptor detour for a WTS-level operation).
    m_service->unsnapForFloat(windowId);
    setWindowFloating(windowId, true);

    // Restore pre-snap size (not position — window stays where the user dropped it).
    // This mirrors the activated-drag path in WindowDragAdaptor::dragStopped.
    if (m_settings && m_settings->restoreOriginalSizeOnUnsnap()) {
        auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
        if (geo) {
            Q_EMIT applyGeometryRequested(windowId, 0, 0, geo->width(), geo->height(), QString(), screenId, true);
            if (m_snapEngine) {
                m_snapEngine->clearUnmanagedGeometry(windowId);
            }
            qCInfo(lcDbusWindow) << "Drag-out unsnap: restoring size" << geo->width() << "x" << geo->height();
        }
    }
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneIdOut)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getPreFloatZone: empty windowId";
        zoneIdOut.clear();
        return false;
    }
    // Delegate to service
    zoneIdOut = m_service->preFloatZone(windowId);
    qCDebug(lcDbusWindow) << "getPreFloatZone for" << windowId << "-> found:" << !zoneIdOut.isEmpty()
                          << "zone:" << zoneIdOut;
    return !zoneIdOut.isEmpty();
}

void WindowTrackingAdaptor::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Only log if there was something to clear
    bool hadPreFloatZone = !m_service->preFloatZone(windowId).isEmpty();
    // Delegate to service
    m_service->clearPreFloatZone(windowId);
    if (hadPreFloatZone) {
        qCDebug(lcDbusWindow) << "Cleared pre-float zone for window" << windowId;
    }
}

bool WindowTrackingAdaptor::isWindowFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Delegate to service
    return m_service->isWindowFloating(windowId);
}

bool WindowTrackingAdaptor::queryWindowFloating(const QString& windowId)
{
    return isWindowFloating(windowId);
}

void WindowTrackingAdaptor::setWindowFloating(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float state"))) {
        return;
    }
    // Delegate to service
    m_service->setWindowFloating(windowId, floating);
    qCInfo(lcDbusWindow) << "Window" << windowId << "is now" << (floating ? "floating" : "not floating");
    // Notify effect so it can update its local cache (use full windowId for per-instance tracking).
    // Use the window's tracked screen if available, otherwise fall back to last active screen.
    QString screen = m_service->screenAssignments().value(windowId, m_lastActiveScreenId);
    Q_EMIT windowFloatingChanged(windowId, floating, screen);

    // Emit unified state change
    Q_EMIT windowStateChanged(windowId,
                              WindowStateEntry{
                                  windowId,
                                  m_service->zoneForWindow(windowId),
                                  screen,
                                  floating,
                                  floating ? QStringLiteral("floated") : QStringLiteral("unfloated"),
                                  QStringList{},
                                  false,
                              });
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    // Delegate to service
    return m_service->floatingWindows();
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
    if (geo) {
        qCInfo(lcDbusWindow) << "applyGeometryForFloat: windowId=" << windowId << "geo=" << *geo
                             << "screen=" << screenId;
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }

    qCInfo(lcDbusWindow) << "applyGeometryForFloat: no geometry found for" << windowId;
    return false;
}

// WindowTrackingAdaptor::clearFloatingStateForSnap was removed — all
// snap-commit paths now route through WindowTrackingService::commitSnap
// which handles clearing the floating state internally and emits
// windowFloatingClearedForSnap, which this adaptor relays to its own
// windowFloatingChanged D-Bus signal in the constructor wiring.

void WindowTrackingAdaptor::setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float for screen"))) {
        return;
    }

    qCInfo(lcDbusWindow) << "setWindowFloatingForScreen: windowId=" << windowId << "floating=" << floating
                         << "screen=" << screenId;

    // Route to the correct engine based on screen mode
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
        // If the window isn't tracked by autotile yet (e.g., dragged from a snap screen),
        // adopt it as floating before setting state. adoptWindowAsFloating is
        // AutotileEngine-specific (snap mode has no equivalent concept).
        if (floating) {
            if (auto* autotile = qobject_cast<AutotileEngine*>(m_autotileEngine.data())) {
                autotile->adoptWindowAsFloating(windowId, screenId);
            }
        }
        m_autotileEngine->setWindowFloat(windowId, floating);
    } else if (m_snapEngine) {
        m_snapEngine->setWindowFloat(windowId, floating);
    }
}

} // namespace PlasmaZones
