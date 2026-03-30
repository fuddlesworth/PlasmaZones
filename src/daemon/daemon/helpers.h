// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Inline helpers shared across daemon TU files (daemon_start.cpp,
// daemon_signals.cpp, daemon_navigation.cpp).  Defined inline to
// avoid ODR issues in both unity and normal builds.

#include <QScreen>
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../dbus/windowtrackingadaptor.h"

namespace PlasmaZones {

/**
 * @brief Resolve the screen ID for a keyboard shortcut action (virtual-screen-aware).
 *
 * Returns the virtual screen ID (e.g., "Dell:U2722D:115107/vs:0") if the cursor
 * is on a subdivided screen, otherwise the physical screen ID.  Returns the raw
 * string from the KWin effect, preserving virtual screen IDs.
 */
inline QString resolveShortcutScreenId(const WindowTrackingAdaptor* trackingAdaptor)
{
    if (!trackingAdaptor) {
        QScreen* primary = Utils::primaryScreen();
        return primary ? Utils::screenIdentifier(primary) : QString();
    }

    // Primary: cursor screen (may be virtual ID from effect)
    const QString cursorScreen = trackingAdaptor->lastCursorScreenName();
    if (!cursorScreen.isEmpty()) {
        return cursorScreen; // Return as-is — preserves virtual screen ID
    }

    // Fallback: focused window's screen (also may be virtual)
    const QString activeScreen = trackingAdaptor->lastActiveScreenName();
    if (!activeScreen.isEmpty()) {
        return activeScreen;
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
    return QString();
}

} // namespace PlasmaZones
