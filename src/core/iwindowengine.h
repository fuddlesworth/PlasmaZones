// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Per-screen window engine lifecycle interface.
 *
 * Both SnapEngine (manual zone-based snapping) and AutotileEngine (automatic
 * tiling algorithms) implement this interface so the daemon's dispatcher
 * (ScreenModeRouter / Daemon::engineForScreen) can look up which engine
 * owns a given screen and route window-lifecycle events to it uniformly.
 *
 * WindowTrackingService remains the shared state store (zone assignments,
 * pre-tile geometry, floating state). Engines use WTS for state and
 * implement this interface for per-window behavior on the screens they own.
 *
 * @note This interface intentionally does NOT include navigation methods
 *       (move/focus/swap/rotate/moveToPosition). Those are autotile-specific
 *       — snap navigation is daemon-driven via WindowTrackingAdaptor's D-Bus
 *       methods, not through a per-engine virtual call. Attempting to
 *       polymorphically dispatch navigation through this interface would
 *       require SnapEngine to implement stub methods that just log a
 *       warning, which is worse than no interface. Callers that need to
 *       drive autotile navigation should call AutotileEngine directly via
 *       the concrete pointer.
 *
 * @see SnapEngine, AutotileEngine, WindowTrackingService, ScreenModeRouter
 */
class PLASMAZONES_EXPORT IEngineLifecycle
{
public:
    virtual ~IEngineLifecycle() = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen ownership
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if this engine is active on the given screen.
     *
     * Used by ScreenModeRouter / Daemon::engineForScreen to dispatch
     * operations to the correct engine. AutotileEngine returns true for
     * screens with autotile layouts; SnapEngine returns true for screens
     * with manual layouts.
     *
     * @param screenId Effective screen ID (physical connector or virtual screen)
     * @return true if this engine manages the given screen
     */
    virtual bool isActiveOnScreen(const QString& screenId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Notify the engine that a new window appeared on its screen.
     *
     * AutotileEngine inserts the window into its tiling order and retiles.
     * SnapEngine runs the auto-snap fallback chain (app rule → persisted →
     * empty → last zone).
     *
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window appeared
     * @param minWidth Window minimum width (0 if unconstrained)
     * @param minHeight Window minimum height (0 if unconstrained)
     */
    virtual void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) = 0;

    /// Convenience overload — equivalent to windowOpened(id, screen, 0, 0).
    void windowOpened(const QString& windowId, const QString& screenId)
    {
        windowOpened(windowId, screenId, 0, 0);
    }

    /**
     * @brief Notify the engine that a window was closed.
     *
     * AutotileEngine removes the window and retiles to fill the gap.
     * SnapEngine cleans up zone assignments and persists state.
     *
     * @param windowId Window identifier from KWin
     */
    virtual void windowClosed(const QString& windowId) = 0;

    /**
     * @brief Notify the engine that a window gained focus.
     *
     * Both engines update their focus tracking for directional navigation.
     *
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window is located
     */
    virtual void windowFocused(const QString& windowId, const QString& screenId) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float management
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Toggle a window between managed and floating states.
     *
     * AutotileEngine removes/re-inserts the window from its tiling layout.
     * SnapEngine saves/restores zone assignments via WindowTrackingService.
     *
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window is located
     */
    virtual void toggleWindowFloat(const QString& windowId, const QString& screenId) = 0;

    /**
     * @brief Set a window's floating state explicitly (directional, not toggle).
     *
     * @param windowId Window identifier from KWin
     * @param shouldFloat true to float, false to unfloat
     */
    virtual void setWindowFloat(const QString& windowId, bool shouldFloat) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Save engine state for session persistence.
     */
    virtual void saveState() = 0;

    /**
     * @brief Load engine state from session persistence.
     */
    virtual void loadState() = 0;
};

} // namespace PlasmaZones
