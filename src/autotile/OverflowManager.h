// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Project headers
#include "plasmazones_export.h"

// Qt headers
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

// C++ headers
#include <functional>

namespace PlasmaZones {

/**
 * @brief Per-screen overflow window tracking (pure tracking — no PhosphorTiles::TilingState mutation)
 *
 * Manages the bookkeeping for windows that are auto-floated when the tiled
 * window count exceeds maxWindows. Uses per-screen storage
 * (QHash<screenId, QSet<windowId>>) plus a reverse index
 * (windowId -> screenId) for O(1) lookups.
 *
 * Distinguished from user-floated windows so they can be auto-unfloated
 * when room becomes available (e.g., window closed, maxWindows increased).
 *
 * This class does NOT mutate PhosphorTiles::TilingState. It returns lists of window IDs
 * that should be floated/unfloated and the caller performs the mutations
 * and signal emissions.
 */
class PLASMAZONES_EXPORT OverflowManager
{
public:
    /**
     * @brief Mark a window as overflow on a specific screen
     *
     * If the window was previously tracked on a different screen, the old
     * entry is cleaned up first to prevent ghost references.
     * No-op if windowId or screenId is empty.
     */
    void markOverflow(const QString& windowId, const QString& screenId);

    /**
     * @brief Clear overflow status for a window (any screen)
     *
     * Uses the reverse index for O(1) lookup — no screen parameter needed.
     */
    void clearOverflow(const QString& windowId);

    /**
     * @brief Check if a window is currently overflow-tracked
     * @return true if the window is in the overflow set on any screen
     */
    bool isOverflow(const QString& windowId) const;

    /**
     * @brief Identify windows beyond tileCount that should be auto-floated
     *
     * Marks the excess windows as overflow. Does NOT call state->setFloating();
     * the caller is responsible for mutating PhosphorTiles::TilingState and emitting signals.
     *
     * @param windowsInState List of all tiled windows (from PhosphorTiles::TilingState)
     * @param isFloating Predicate: returns true if a window is already floating
     * @param screenId Screen where overflow is being applied
     * @param windows Ordered window list from PhosphorTiles::TilingState::tiledWindows()
     * @param tileCount Number of available tile zones
     * @return Window IDs that should be newly auto-floated (caller mutates state)
     */
    QStringList applyOverflow(const QString& screenId, const QStringList& windows, int tileCount);

    /**
     * @brief Identify overflow windows that should be unfloated when room opens
     *
     * Also purges stale entries (windows no longer floating or not in PhosphorTiles::TilingState)
     * to prevent overflow tracking from growing unbounded.
     *
     * Does NOT call state->setFloating(); the caller performs mutations.
     *
     * @param screenId Screen to check for recovery
     * @param tiledCount Current number of tiled (non-floating) windows
     * @param maxWindows Maximum windows allowed on the screen
     * @param isFloating Predicate: returns true if window is currently floating
     * @param containsWindow Predicate: returns true if window exists in PhosphorTiles::TilingState
     * @return Window IDs that should be unfloated (caller mutates state and emits signals)
     */
    QStringList recoverIfRoom(const QString& screenId, int tiledCount, int maxWindows,
                              const std::function<bool(const QString&)>& isFloating,
                              const std::function<bool(const QString&)>& containsWindow);

    /**
     * @brief Take and remove all overflow entries for a screen
     * @return Set of overflow window IDs that were on the screen
     */
    QSet<QString> takeForScreen(const QString& screenId);

    /**
     * @brief Clear overflow status when a window migrates between screens
     *
     * Uses the internal reverse index to find the correct per-screen set,
     * ignoring the oldScreen hint (which may have diverged from the
     * overflow manager's own tracking during cross-screen migration).
     * The caller re-adds the window to the new screen's normal flow
     * via onWindowAdded().
     */
    void migrateWindow(const QString& windowId);

    /**
     * @brief Clear overflow entries for windows on screens not in the active set
     *
     * Used during screen deactivation to clean up orphaned overflow entries
     * that weren't caught by takeForScreen().
     *
     * @param activeScreens Set of currently active autotile screen names
     */
    void clearForRemovedScreens(const QSet<QString>& activeScreens);

    /**
     * @brief Check if there are any overflow windows tracked
     * @return true if no overflow windows are tracked on any screen
     */
    bool isEmpty() const;

private:
    QHash<QString, QSet<QString>> m_overflow; // screenId -> overflow window IDs
    QHash<QString, QString> m_windowToScreen; // windowId -> screenId (reverse index)
};

} // namespace PlasmaZones
