// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Shared interface for window management engines
 *
 * Both SnapEngine (manual zone-based snapping) and AutotileEngine (automatic
 * tiling algorithms) implement this interface, enabling the daemon/adaptor
 * layer to route operations to the correct engine per screen without
 * mode-specific branching.
 *
 * WindowTrackingService remains the shared state store (zone assignments,
 * pre-tile geometry, floating state). Engines use WTS for state and implement
 * this interface for behavior.
 *
 * @see SnapEngine, AutotileEngine, WindowTrackingService
 */
class PLASMAZONES_EXPORT IWindowEngine
{
public:
    virtual ~IWindowEngine() = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen ownership
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if this engine is active on the given screen
     *
     * Used by the routing layer to dispatch operations to the correct engine.
     * AutotileEngine returns true for screens with autotile layouts;
     * SnapEngine returns true for screens with manual layouts.
     *
     * @param screenName Screen connector name (e.g. "DP-1")
     * @return true if this engine manages the given screen
     */
    virtual bool isActiveOnScreen(const QString& screenName) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Notify the engine that a new window appeared
     *
     * AutotileEngine inserts the window into its tiling order and retiles.
     * SnapEngine runs the auto-snap fallback chain (app rule → persisted → empty → last zone).
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window appeared
     * @param minWidth Window minimum width (0 if unconstrained)
     * @param minHeight Window minimum height (0 if unconstrained)
     */
    virtual void windowOpened(const QString& windowId, const QString& screenName, int minWidth, int minHeight) = 0;

    /**
     * @brief Convenience overload — equivalent to windowOpened(id, screen, 0, 0)
     */
    void windowOpened(const QString& windowId, const QString& screenName)
    {
        windowOpened(windowId, screenName, 0, 0);
    }

    /**
     * @brief Notify the engine that a window was closed
     *
     * AutotileEngine removes the window and retiles to fill the gap.
     * SnapEngine cleans up zone assignments and persists state.
     *
     * @param windowId Window identifier from KWin
     */
    virtual void windowClosed(const QString& windowId) = 0;

    /**
     * @brief Notify the engine that a window gained focus
     *
     * Both engines update their focus tracking for directional navigation.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window is located
     */
    virtual void windowFocused(const QString& windowId, const QString& screenName) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float management
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Toggle a window between managed and floating states
     *
     * AutotileEngine removes/re-inserts the window from its tiling layout.
     * SnapEngine saves/restores zone assignments via WindowTrackingService.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window is located
     */
    virtual void toggleWindowFloat(const QString& windowId, const QString& screenName) = 0;

    /**
     * @brief Set a window's floating state explicitly (directional, not toggle)
     *
     * @param windowId Window identifier from KWin
     * @param shouldFloat true to float, false to unfloat
     */
    virtual void setWindowFloat(const QString& windowId, bool shouldFloat) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Focus a window in the given direction
     *
     * AutotileEngine maps directions to its linear tiling order.
     * SnapEngine uses zone adjacency from the layout geometry.
     *
     * @param direction Direction string ("left", "right", "up", "down")
     * @param action OSD action label ("focus", "cycle")
     */
    virtual void focusInDirection(const QString& direction, const QString& action) = 0;

    /**
     * @brief Swap the focused window with the neighbor in the given direction
     *
     * @param direction Direction string ("left", "right", "up", "down")
     * @param action OSD action label ("move", "swap")
     */
    virtual void swapInDirection(const QString& direction, const QString& action) = 0;

    /**
     * @brief Rotate all managed windows by one position
     *
     * @param clockwise Direction of rotation
     * @param screenName Screen to operate on (empty = active/all)
     */
    virtual void rotateWindows(bool clockwise, const QString& screenName) = 0;

    /**
     * @brief Move a window to a specific position/zone number
     *
     * AutotileEngine moves to position N in the tiling order.
     * SnapEngine snaps to zone number N in the layout.
     *
     * @param windowId Window to move (may be ignored if engine uses focused window)
     * @param position Target position (1-based)
     * @param screenName Screen for layout/geometry resolution
     */
    virtual void moveToPosition(const QString& windowId, int position, const QString& screenName) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Save engine state for session persistence
     */
    virtual void saveState() = 0;

    /**
     * @brief Load engine state from session persistence
     */
    virtual void loadState() = 0;
};

} // namespace PlasmaZones
