// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortileengine_export.h>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>

namespace PhosphorTiles {
class TilingState;
}

namespace PhosphorTileEngine {

class AutotileEngine;

/**
 * @brief Handles navigation, focus cycling, and ratio/count adjustments
 *
 * NavigationController is a stateless helper extracted from AutotileEngine.
 * It manages keyboard-driven navigation (focus next/previous/master), window
 * swapping, rotation, directional focus/swap, position moves, and master
 * ratio/count adjustments.
 *
 * Stateless — no member data moves from AutotileEngine. All state is accessed
 * via the back-pointer to AutotileEngine.
 *
 * @see AutotileEngine for the owning engine
 */
class PHOSPHORTILEENGINE_EXPORT NavigationController
{
public:
    explicit NavigationController(AutotileEngine* engine);

    // ═══════════════════════════════════════════════════════════════════════════
    // Focus/window cycling
    // ═══════════════════════════════════════════════════════════════════════════

    void focusNext();
    void focusPrevious();
    void focusMaster();

    // ═══════════════════════════════════════════════════════════════════════════
    // Window swapping & rotation
    // ═══════════════════════════════════════════════════════════════════════════

    void swapFocusedWithMaster();
    void rotateWindowOrder(bool clockwise);
    /// `explicitWindowId` (when non-empty) overrides the state's internal
    /// focusedWindow() for the operation. The IPlacementEngine virtual-method
    /// overrides on AutotileEngine pass the CANONICALIZED `ctx.windowId`
    /// (canonicalizeForLookup) here so navigation follows the daemon's
    /// authoritative focus tracking even when the engine's per-state
    /// focusedWindow tracker is stale. The canonicalization is load-bearing:
    /// the lookups below match this id against the ids the tiling states hold,
    /// so a raw ctx.windowId in a non-canonical form would find no state and
    /// silently fall through to the focused-screen path.
    void swapFocusedInDirection(const QString& direction, const QString& action,
                                const QString& explicitWindowId = QString());
    void focusInDirection(const QString& direction, const QString& action, const QString& explicitWindowId = QString());
    void moveFocusedToPosition(int position, const QString& explicitWindowId = QString());

    // ═══════════════════════════════════════════════════════════════════════════
    // Split ratio adjustment
    // ═══════════════════════════════════════════════════════════════════════════

    void increaseMasterRatio(qreal delta);
    void decreaseMasterRatio(qreal delta);
    void setGlobalSplitRatio(qreal ratio);
    void setGlobalMasterCount(int count);

    // ═══════════════════════════════════════════════════════════════════════════
    // Master count adjustment
    // ═══════════════════════════════════════════════════════════════════════════

    void adjustMasterCount(int delta);

    /**
     * @brief The tiled window at @p screenId's entry edge facing the source when
     *        a crossing arrives in @p direction (crossing "right" enters the
     *        target's LEFT edge → the leftmost tile, etc.). Empty when the screen
     *        has no tiling state or no tiled windows. Used by the daemon to pick
     *        the cross-mode swap partner on an autotile target.
     * @note When geometry isn't computed yet OR the screen is over its maxWindows
     *       cap (calculatedZones covers only the capped subset, so it can't align
     *       1:1 with tiledWindows), this degrades to the first tiled window (the
     *       master) rather than the geometric edge tile.
     */
    QString entryWindowOnScreen(const QString& screenId, const QString& direction) const;

    /**
     * @brief The RAW window-order index of @p windowId on @p screenId's current
     *        state (counting floating windows, as TilingState::addWindow does),
     *        or -1 when not present. Lets the daemon capture a window's slot
     *        before a cross-mode swap so its counterpart lands in the same place
     *        when re-inserted via HandoffContext.insertIndex.
     */
    int windowOrderIndexOnScreen(const QString& screenId, const QString& windowId) const;

private:
    /**
     * @brief Resolve active screen for navigation feedback
     *
     * DRY helper replacing 3x duplicated pattern: if m_activeScreen is empty,
     * fall back to the first autotile screen.
     */
    QString resolveActiveScreen() const;

    /**
     * @brief Resolve the PhosphorTiles::TilingState for the currently focused screen
     *
     * Returns nullptr if no active screen or no state exists for it.
     * Sets outScreenId to the resolved screen identifier.
     */
    PhosphorTiles::TilingState* resolveActiveState(QString& outScreenId) const;

    /**
     * @brief Helper to emit focus request for a window at calculated index
     */
    void emitFocusRequestAtIndex(int indexOffset, bool useFirst = false);

    /**
     * @brief Helper to get tiled windows and state for focus operations.
     *
     * When `explicitWindowId` is non-empty, locate the state that contains
     * that window first — the daemon may know the true focused window even
     * when the engine's per-state focusedWindow() tracker is stale. Falls
     * through to the focused-screen lookup if the window isn't in any state.
     */
    QStringList tiledWindowsForFocusedScreen(QString& outScreenId, PhosphorTiles::TilingState*& outState,
                                             const QString& explicitWindowId = QString());

    /**
     * @brief Pick the tiled window spatially adjacent to @p focused in
     *        @p direction, using the engine's computed per-window geometry.
     *
     * Selection is geometric (via PhosphorGeometry::directionalNeighbor over
     * the state's calculatedZones(), which are index-aligned with @p windows),
     * not order-based — so "right" lands on the window to the right, never the
     * one below.
     *
     * @param windows the state's tiledWindows() list.
     * @param outHasGeometry set true when the surface has a computed layout
     *        (zones align 1:1 with @p windows and the focused window has a
     *        valid rect); false when geometry is not yet available, so callers
     *        can fall back to order-based cycling.
     * @return the target windowId, or an empty string when no tiled window
     *         lies in @p direction (the layout boundary — the seam where
     *         cross-surface navigation takes over) or geometry is unavailable.
     */
    QString directionalNeighborWindow(PhosphorTiles::TilingState* state, const QStringList& windows,
                                      const QString& focused, const QString& direction, bool& outHasGeometry) const;

    /**
     * @brief The window to focus when directional navigation crosses from
     *        @p sourceScreenId into the adjacent output in @p direction.
     *
     * Resolves the neighbour output via the injected cross-surface resolver,
     * then picks that output's tiled window nearest the crossing edge (the
     * directional neighbour of @p focused's rect in global coordinates),
     * falling back to its first tiled window. Returns an empty string when
     * there is no resolver, no neighbour output, or it has no tiled windows.
     */
    QString crossOutputFocusTarget(const QString& sourceScreenId, const QString& focused,
                                   const QString& direction) const;

    /**
     * @brief Move @p focused from @p sourceScreenId into the adjacent output in
     *        @p direction, migrating its tiling state and activating it there.
     * @param action "move" or "swap" — selects the cross-mode signal emitted when
     *        the neighbour output is a different (snap) mode: a "swap" defers to
     *        crossModeSwapRequested (two-way exchange), a "move" to
     *        crossModeMoveRequested (one-way insert).
     * @return true when the crossing was handled, false when the caller should
     *         fall through to its next option (cross-desktop, then no_neighbor).
     *
     * Returns true WITHOUT touching tiling state on the cross-mode branch: a
     * neighbour output in a different (snap) mode is handed to the daemon via
     * crossModeSwapRequested / crossModeMoveRequested, which performs the move.
     * Returns true after a completed migration on the same-mode (autotile)
     * branch: the window is re-keyed, migrated, both outputs are reflowed, and
     * it is activated on the neighbour.
     *
     * Returns false — leaving @p focused untouched on @p sourceScreenId — when:
     *   - there is no cross-surface resolver, or no neighbour output in
     *     @p direction;
     *   - the autotile neighbour is already at its effective maxWindows cap (a
     *     tiled window has no slot there);
     *   - @p focused would not tile on the destination (shouldTileWindow).
     * The last two refuse BEFORE any state mutation or the
     * windowOutputMoveExpected marker, so a refusal never strands the window.
     */
    bool crossOutputMove(const QString& sourceScreenId, const QString& focused, const QString& direction,
                         const QString& action);

    /**
     * @brief The window to focus when directional navigation crosses to the
     *        adjacent virtual desktop on @p sourceScreenId in @p direction.
     *
     * Resolves the neighbour desktop via the cross-surface resolver and returns
     * that (screen, desktop) state's entry window — the first tiled window for
     * a forward step (right/down), the last for a backward step. Empty when
     * there is no neighbour desktop or it has no tiled windows on this screen.
     */
    QString crossDesktopFocusTarget(const QString& sourceScreenId, const QString& direction) const;

    /**
     * @brief Move @p focused from @p sourceScreenId's current desktop onto the
     *        adjacent desktop in @p direction. Does NOT touch tiling state itself:
     *        it emits windowDesktopMoveRequested (or crossModeMoveRequested for a
     *        snap target) so the compositor moves the real window, and the reactive
     *        windowClosed/windowOpened path then reflows the source and tiles the
     *        target. Returns false when there is no neighbour desktop.
     */
    bool crossDesktopMove(const QString& sourceScreenId, const QString& focused, const QString& direction);

    /**
     * @brief The global-coordinate rect of @p windowId within @p state, or an
     *        invalid rect when the state has no computed geometry for it.
     */
    QRect rectForWindowInState(PhosphorTiles::TilingState* state, const QString& windowId) const;

    /**
     * @brief Helper to apply an operation to all screen states
     *
     * The callback receives each state's screen id so callers can skip screens
     * carrying a per-screen override of the KEY they write — setGlobalSplitRatio
     * skips a SplitRatio override, setGlobalMasterCount a MasterCount one
     * (mirroring propagateGlobalSplitRatio / propagateGlobalMasterCount). Any
     * other per-screen override on the screen is irrelevant and does not skip it.
     */
    void applyToAllStates(const std::function<void(const QString& screenId, PhosphorTiles::TilingState*)>& operation);

    AutotileEngine* m_engine = nullptr;
};

} // namespace PhosphorTileEngine
