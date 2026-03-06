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
 * @brief Resolve current screen for keyboard shortcuts
 *
 * Primary source: the cursor's screen, reported by the KWin effect via
 * cursorScreenChanged (fires on every monitor crossing in slotMouseChanged).
 * This accurately reflects where the user is looking, even if no window
 * on that screen has focus.
 *
 * Fallback: the focused window's screen, reported via windowActivated.
 * Used when the effect hasn't loaded yet or no mouse movement has occurred.
 *
 * QCursor::pos() is NOT used — it returns stale data for background daemons
 * on Wayland.
 */
inline QScreen* resolveShortcutScreen(const WindowTrackingAdaptor* trackingAdaptor)
{
    if (!trackingAdaptor) {
        return nullptr;
    }

    // Prefer cursor screen — tracks the physical cursor position
    const QString& cursorScreen = trackingAdaptor->lastCursorScreenName();
    if (!cursorScreen.isEmpty()) {
        QScreen* screen = Utils::findScreenByIdOrName(cursorScreen);
        if (screen) {
            return screen;
        }
    }

    // Cursor screen not yet reported (effect not loaded or no mouse movement).
    // Fall back to focused window's screen.
    const QString& activeScreen = trackingAdaptor->lastActiveScreenName();
    if (!activeScreen.isEmpty()) {
        QScreen* screen = Utils::findScreenByIdOrName(activeScreen);
        if (screen) {
            return screen;
        }
    }

    // Last resort: primary screen (daemon just started, no KWin effect data yet)
    qCDebug(lcDaemon) << "resolveShortcutScreen: falling back to primary screen";
    return Utils::primaryScreen();
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
