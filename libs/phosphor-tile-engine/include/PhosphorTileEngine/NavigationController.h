// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortileengine_export.h>
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
    /// overrides on AutotileEngine pass `ctx.windowId` here so navigation
    /// follows the daemon's authoritative focus tracking even when the
    /// engine's per-state focusedWindow tracker is stale.
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
     * @brief Helper to apply an operation to all screen states
     */
    void applyToAllStates(const std::function<void(PhosphorTiles::TilingState*)>& operation);

    AutotileEngine* m_engine = nullptr;
};

} // namespace PhosphorTileEngine
