// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QString>

class QPoint;

namespace PlasmaZones {

/**
 * @brief Resolve a cursor position to the effective PlasmaZones screen ID.
 *
 * On a plain single-monitor setup "effective screen ID" is just the
 * physical `QScreen::name()`. On multi-monitor or virtual-screen setups
 * the daemon tracks a richer mapping (VS regions carved out of physical
 * outputs, duplicate connector disambiguation) that Qt doesn't know about
 * — so we ask the daemon via `org.plasmazones.Screen.getEffectiveScreenAt`.
 *
 * Used by the editor and any future launcher that wants "open this app on
 * the screen where the cursor is" behavior without duplicating the daemon
 * D-Bus call + fallback chain.
 */
class PLASMAZONES_EXPORT ScreenResolver
{
public:
    /**
     * @brief Resolve a screen coordinate to an effective screen ID.
     *
     * Tries the daemon's virtual-screen-aware lookup first, then falls
     * back to `QGuiApplication::screenAt(pos)` if the daemon is
     * unreachable, finally falls back to `primaryScreen()` if nothing
     * matches. Returns an empty string only if Qt reports no screens
     * at all (headless / test environments).
     *
     * @param pos           Position in global screen coordinates.
     * @param daemonTimeoutMs  D-Bus call timeout in milliseconds. Keep
     *                         this short — the caller is typically
     *                         blocking the user's shortcut keypress.
     */
    static QString effectiveScreenAt(const QPoint& pos, int daemonTimeoutMs = 2000);

    /// Convenience wrapper: resolve at the current `QCursor::pos()`.
    static QString effectiveScreenAtCursor(int daemonTimeoutMs = 2000);
};

} // namespace PlasmaZones
