// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Navigation target context: the window and screen the user is acting on.
 *
 * Populated by the daemon's navigation shortcut handlers from compositor
 * shadow state (windowActivated / cursorScreenChanged pushes) before
 * dispatching through ScreenModeRouter::navigatorFor().
 *
 * Making the target explicit at the interface level (rather than letting
 * each engine re-read a global "last active window" shadow) has two
 * architectural benefits:
 *
 *   1. The engines stop reaching into WindowTrackingAdaptor's
 *      compositor-layer shadows on every navigation call, which is a
 *      step toward retiring the SnapEngine → WTA back-reference entirely.
 *   2. Tests can invoke navigation methods with an explicit context
 *      without needing to stub a compositor shadow.
 *
 * @note Both fields may be empty — for example on very-early-startup
 *       shortcuts or when no window is focused. Each method documents
 *       its behaviour when the fields are empty; the general pattern
 *       is "emit navigationFeedback with a 'no_window' / 'no_screen'
 *       reason and return without acting".
 *
 * @note Adapter asymmetry: SnapNavigationAdapter threads @c ctx through
 *       to SnapEngine on every call — SnapEngine honours the explicit
 *       target and only falls back to WTA shadows when both fields are
 *       empty. AutotileNavigationAdapter passes @c ctx.screenId through
 *       on the screen-keyed methods (rotate / reapply / snapAllWindows)
 *       but lets the other methods use AutotileEngine's internal
 *       focused-screen state. That's intentional: the autotile engine
 *       already tracks focused window + focused screen per-state, so
 *       duplicating the target at the call site would only create a
 *       split-brain risk. The interface contract is therefore "ctx is
 *       authoritative for snap; advisory for autotile".
 */
struct PLASMAZONES_EXPORT NavigationContext
{
    QString windowId; ///< The window the operation should act on (typically the focused window).
    QString screenId; ///< The screen the operation applies to (already resolved by the daemon).
};

/**
 * @brief Polymorphic dispatch for window-navigation user intents.
 *
 * Both AutotileEngine and SnapEngine implement this interface (via thin
 * adapter classes) so ScreenModeRouter can look up the right engine for a
 * given screen and dispatch user-facing navigation commands without the
 * caller branching on mode. The daemon's navigation shortcut handlers
 * (see src/daemon/daemon/navigation.cpp) collapse from ~20 copies of
 * `if (isAutotileScreen) { autotile->foo() } else { wta->bar() }` into
 * single-line calls like `router->navigatorFor(ctx.screenId)->foo(ctx)`.
 *
 * Semantic contract: each method represents a USER INTENT, not a
 * mode-specific implementation step. "Move focused window left" has
 * different internal meaning in tile-swap mode vs. zone-snap mode, but
 * the user's request is the same — the interface names the request and
 * each engine fulfills it in its own terms.
 *
 * All methods are idempotent with respect to "no focused window" — each
 * implementation emits navigation feedback with a sensible reason code
 * when there's nothing to act on, rather than erroring out. Callers do
 * NOT need to guard against empty focus state.
 *
 * @see ScreenModeRouter::navigatorFor
 * @see IEngineLifecycle for the complementary lifecycle interface
 *      (windowOpened / windowClosed / saveState / etc.)
 */
class PLASMAZONES_EXPORT INavigationActions
{
public:
    virtual ~INavigationActions() = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Directional operations on the focused window
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Move keyboard focus to the adjacent window in the given direction.
     *
     * Autotile: walks the tiling-order neighbour list.
     * Snap: queries zone adjacency from the target resolver.
     *
     * @param direction "left" / "right" / "up" / "down"
     * @param ctx       Target window + screen
     */
    virtual void focusInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /**
     * @brief Move the focused window to the adjacent slot in the given direction.
     *
     * Autotile: swaps with the next window in the tiling order (the window
     *   takes the neighbour's position; the neighbour shifts).
     * Snap: moves the window into the adjacent zone — displacing or filling
     *   depending on the target's occupancy.
     *
     * @param direction "left" / "right" / "up" / "down"
     * @param ctx       Target window + screen
     */
    virtual void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /**
     * @brief Swap the focused window with the adjacent window in the given direction.
     *
     * Autotile: identical to moveFocusedInDirection at the implementation
     *   level (tiling-order swap IS the move), but carries a distinct OSD
     *   feedback label.
     * Snap: exchanges the focused window's zone with the adjacent zone's
     *   current occupant — both windows change zones simultaneously.
     *
     * @param direction "left" / "right" / "up" / "down"
     * @param ctx       Target window + screen
     */
    virtual void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Positional operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Move the focused window to the Nth position on the screen.
     *
     * Autotile: Nth slot in tiling order (1 = master).
     * Snap: Nth zone on the screen (by sorted zone number).
     *
     * @param position 1-based position; implementations clamp to valid range
     * @param ctx      Target window + screen
     */
    virtual void moveFocusedToPosition(int position, const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Multi-window operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Rotate all tiled / snapped windows on the screen.
     *
     * Autotile: rotates positions in the tiling order.
     * Snap: rotates zone assignments through the current layout's zones.
     *
     * @param clockwise Rotation direction
     * @param ctx       Target screen (windowId unused)
     */
    virtual void rotateWindows(bool clockwise, const NavigationContext& ctx) = 0;

    /**
     * @brief Re-apply the current layout to all managed windows on the screen.
     *
     * Autotile: `retile(screenId)` — recomputes and applies tiling.
     * Snap: `resnapToNewLayout` — re-snaps windows by zone number after a
     *   layout switch (previous zone N → new zone N).
     *
     * Idempotent when the layout hasn't changed — safe to call after
     * configuration updates.
     *
     * @param ctx Target screen (windowId unused)
     */
    virtual void reapplyLayout(const NavigationContext& ctx) = 0;

    /**
     * @brief Snap every un-managed window on the screen to the current layout.
     *
     * Autotile: `retile(screenId)` — picks up any windows the engine isn't
     *   tracking yet and inserts them into the tiling order.
     * Snap: `snapAllWindows` — walks the KWin window list and snaps each
     *   currently-unsnapped window to a zone.
     *
     * Differs from reapplyLayout in that this is the "snap everything to
     * the grid" user action triggered by an explicit shortcut, whereas
     * reapplyLayout is the post-layout-change refresh.
     *
     * @param ctx Target screen (windowId unused)
     */
    virtual void snapAllWindows(const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float state
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Toggle the focused window between managed and floating states.
     *
     * Autotile: excludes from / reinserts into the tiling layout.
     * Snap: stores/restores zone assignments via WindowTrackingService.
     *
     * @param ctx Target window + screen
     */
    virtual void toggleFocusedFloat(const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Cycle operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Cycle keyboard focus through managed windows on the screen.
     *
     * Autotile: cycles through the tiling order (forward = right in the
     *   linear layout, wrapping).
     * Snap: cycles through windows stacked in the same zone, or to the
     *   next/previous zone if only one window per zone.
     *
     * @param forward true = next, false = previous
     * @param ctx     Target window + screen
     */
    virtual void cycleFocus(bool forward, const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Mode-asymmetric operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Move the focused window to the first empty zone on the screen.
     *
     * Autotile: no-op. Autotile mode has no concept of "empty zones" —
     *   every window participates in the layout, and the engine ignores
     *   this call. Exists on the interface so the daemon doesn't have to
     *   branch on mode at the shortcut entry point.
     * Snap: finds the first unoccupied zone in the layout and moves the
     *   focused window into it.
     *
     * @param ctx Target window + screen
     */
    virtual void pushToEmptyZone(const NavigationContext& ctx) = 0;

    /**
     * @brief "Restore" the focused window out of its managed state.
     *
     * Autotile: toggle-float. Pulling a window out of the tiling layout
     *   is what "restore to original state" means in tile mode.
     * Snap: restore the window to its pre-snap size (as captured in
     *   pre-tile geometry) at its current position, and unsnap.
     *
     * Same user intent ("un-manage this window"), different mechanics in
     * each mode.
     *
     * @param ctx Target window + screen
     */
    virtual void restoreFocusedWindow(const NavigationContext& ctx) = 0;
};

} // namespace PlasmaZones
