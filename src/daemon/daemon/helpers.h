// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Inline helpers shared across daemon TU files (daemon_start.cpp,
// daemon_signals.cpp, daemon_navigation.cpp).  Defined inline to
// avoid ODR issues in both unity and normal builds.

#include <QScreen>
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
#include "../../dbus/windowtrackingadaptor.h"

namespace PlasmaZones {

/**
 * @brief Resolve a physical screen ID to a virtual screen ID if subdivisions exist.
 *
 * When the KWin effect sends a physical screen ID (e.g., during daemon reconnect
 * before virtual screen configs are loaded), the daemon must resolve it to the
 * correct virtual screen.  Uses the focused window's geometry center as the
 * position hint (QCursor::pos() is unreliable on Wayland for background daemons).
 * Falls back to vs:0 (leftmost) if no better hint is available.
 */
inline QString resolveVirtualScreenId(const QString& physicalId, const WindowTrackingAdaptor* trackingAdaptor,
                                      const QPoint& cursorPos = QPoint(-1, -1))
{
    auto* mgr = ScreenManager::instance();
    if (!mgr || !mgr->hasVirtualScreens(physicalId)) {
        return physicalId;
    }

    // Best source: the focused window's daemon-tracked screen assignment.
    // When a window is snapped to a zone, windowSnapped() stores the virtual
    // screen ID in m_windowScreenAssignments.  This is authoritative — it was
    // set at snap time by the daemon itself, not by the effect.
    if (trackingAdaptor && trackingAdaptor->service()) {
        const QString activeWindowId = trackingAdaptor->lastActiveWindowId();
        if (!activeWindowId.isEmpty()) {
            const QString trackedScreen = trackingAdaptor->service()->screenAssignments().value(activeWindowId);
            if (VirtualScreenId::isVirtual(trackedScreen)
                && VirtualScreenId::extractPhysicalId(trackedScreen) == physicalId) {
                return trackedScreen;
            }
        }
    }

    // Fallback: effect-reported active screen (may be virtual if effect has VS configs)
    if (trackingAdaptor) {
        const QString activeScreen = trackingAdaptor->lastActiveScreenName();
        if (VirtualScreenId::isVirtual(activeScreen)
            && VirtualScreenId::extractPhysicalId(activeScreen) == physicalId) {
            return activeScreen;
        }
    }

    // Cursor position hint: when a keyboard shortcut fires with no focused window,
    // the cursor position (from the effect) can resolve the correct virtual screen.
    if (cursorPos.x() >= 0) {
        const QString vsAtCursor = mgr->effectiveScreenAt(cursorPos);
        if (VirtualScreenId::isVirtual(vsAtCursor) && VirtualScreenId::extractPhysicalId(vsAtCursor) == physicalId) {
            return vsAtCursor;
        }
    }

    // Last resort: first virtual screen (vs:0)
    QStringList vsIds = mgr->virtualScreenIdsFor(physicalId);
    if (!vsIds.isEmpty()) {
        return vsIds.first();
    }
    return physicalId;
}

/**
 * @brief Resolve the screen ID for a keyboard shortcut action (virtual-screen-aware).
 *
 * Every navigation shortcut (float, move, focus, restore, swap, ...) targets
 * the focused window, so the focused window's screen is authoritative — the
 * cursor is only consulted when there is no active window (e.g. empty-desktop
 * shortcuts). Using cursor-first silently misroutes actions when the user
 * lets the mouse rest on a different virtual screen than the focused window.
 */
inline QString resolveShortcutScreenId(const WindowTrackingAdaptor* trackingAdaptor)
{
    if (!trackingAdaptor) {
        QScreen* primary = Utils::primaryScreen();
        return primary ? Utils::screenIdentifier(primary) : QString();
    }

    // Primary: focused window's screen (may already be a virtual ID from the effect)
    const QString activeScreen = trackingAdaptor->lastActiveScreenName();
    if (!activeScreen.isEmpty()) {
        if (!VirtualScreenId::isVirtual(activeScreen)) {
            return resolveVirtualScreenId(activeScreen, trackingAdaptor);
        }
        return activeScreen;
    }

    // Fallback: cursor screen, for shortcuts fired with no focused window
    const QString cursorScreen = trackingAdaptor->lastCursorScreenName();
    if (!cursorScreen.isEmpty()) {
        if (!VirtualScreenId::isVirtual(cursorScreen)) {
            return resolveVirtualScreenId(cursorScreen, trackingAdaptor);
        }
        return cursorScreen;
    }

    // Last resort: primary screen physical ID
    QScreen* primary = Utils::primaryScreen();
    return primary ? Utils::screenIdentifier(primary) : QString();
}

/**
 * @brief Convert NavigationDirection enum to string for D-Bus/engine calls
 */
inline QString navigationDirectionToString(NavigationDirection direction)
{
    switch (direction) {
    case NavigationDirection::Left:
        return QStringLiteral("left");
    case NavigationDirection::Right:
        return QStringLiteral("right");
    case NavigationDirection::Up:
        return QStringLiteral("up");
    case NavigationDirection::Down:
        return QStringLiteral("down");
    }
    Q_UNREACHABLE();
    return QString();
}

} // namespace PlasmaZones
