// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>
#include <QStringList>
#include <functional>

namespace PlasmaZones {

class AutotileEngine;
class TilingState;

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
class PLASMAZONES_EXPORT NavigationController
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
    void swapFocusedInDirection(const QString& direction, const QString& action);
    void focusInDirection(const QString& direction, const QString& action);
    void moveFocusedToPosition(int position);

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

    void increaseMasterCount();
    void decreaseMasterCount();

private:
    /**
     * @brief Resolve active screen for navigation feedback
     *
     * DRY helper replacing 3x duplicated pattern: if m_activeScreen is empty,
     * fall back to the first autotile screen.
     */
    QString resolveActiveScreen() const;

    /**
     * @brief Helper to emit focus request for a window at calculated index
     */
    void emitFocusRequestAtIndex(int indexOffset, bool useFirst = false);

    /**
     * @brief Helper to get tiled windows and state for focus operations
     */
    QStringList tiledWindowsForFocusedScreen(QString& outScreenId, TilingState*& outState);

    /**
     * @brief Helper to apply an operation to all screen states
     */
    void applyToAllStates(const std::function<void(TilingState*)>& operation);

    /**
     * @brief Null-safe check if a window is locked (guards against null m_windowTracker in tests)
     */
    bool isWindowLocked(const QString& windowId) const;

    /**
     * @brief Emit feedback and return true if the window is locked, otherwise return false
     */
    bool emitIfLocked(const QString& windowId, const QString& action, const QString& screenId);

    AutotileEngine* m_engine = nullptr;
};

} // namespace PlasmaZones
