// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QJsonArray>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace PlasmaZones {

class AutotileEngine;
class Settings;

/**
 * @brief Bridges autotile engine with Settings and session persistence
 *
 * SettingsBridge handles:
 * - Synchronizing AutotileConfig from Settings (syncFromSettings)
 * - Connecting to Settings change signals for live updates (connectToSettings)
 * - Debouncing rapid settings changes into coalesced retiles
 * - Session persistence serialization (serializeWindowOrders/deserializeWindowOrders)
 * - Encapsulating signal-blocked Settings writes (syncAlgorithmToSettings)
 *
 * Uses a back-pointer to AutotileEngine for access to config, algorithm,
 * tiling state, and retile machinery. Declared as a friend class in AutotileEngine.
 *
 * @see AutotileEngine for the owning engine
 * @see Settings for the KConfig-backed settings store
 */
class PLASMAZONES_EXPORT SettingsBridge
{
public:
    explicit SettingsBridge(AutotileEngine* engine);

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings synchronization
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply all settings from Settings to internal config
     *
     * Copies all autotile-related settings from the Settings object to the
     * internal AutotileConfig. Also sets the algorithm and enabled state.
     * Call this once during initialization.
     *
     * @param settings Settings object to read from (not owned)
     */
    void syncFromSettings(Settings* settings);

    /**
     * @brief Connect to Settings change signals for live updates
     *
     * Connects to all autotile-related Settings signals and updates the
     * internal config when they change. Uses debouncing to coalesce rapid
     * changes (e.g., slider adjustments) into a single retile operation.
     *
     * @param settings Settings object to connect to (not owned, must outlive engine)
     */
    void connectToSettings(Settings* settings);

    /**
     * @brief Write algorithm-related fields to Settings with signals blocked
     *
     * Encapsulates the signal-blocked Settings write from setAlgorithm() so the
     * engine doesn't need direct m_settings access for algorithm changes.
     *
     * @param algoId New algorithm ID
     * @param splitRatio New split ratio
     * @param maxWindows New max windows
     * @param oldMaxWindows Previous max windows (only written if changed)
     */
    void syncAlgorithmToSettings(const QString& algoId, qreal splitRatio, int maxWindows, int oldMaxWindows);

    /**
     * @brief Write shortcut-adjusted ratio/count to Settings with signals blocked
     *
     * Called after keyboard-shortcut-driven ratio/count changes to keep Settings
     * in sync. Signal-blocked to prevent feedback loops via autotileSplitRatioChanged.
     *
     * @param splitRatio Current split ratio (after shortcut adjustment)
     * @param masterCount Current master count (after shortcut adjustment)
     */
    void syncShortcutAdjustment(qreal splitRatio, int masterCount);

    // ═══════════════════════════════════════════════════════════════════════════
    // Session persistence (serialization only — persistence owned by WTA)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Serialize per-context autotile window orders to JSON
     *
     * Returns a JSON array of per-(screen, desktop, activity) entries with
     * window order and floating state. masterCount/splitRatio are NOT included
     * — they are persisted by Settings via AutotileScreen:<id> per-screen overrides.
     *
     * Called by WTA's save cycle via persistence delegate.
     */
    QJsonArray serializeWindowOrders() const;

    /**
     * @brief Deserialize per-context autotile window orders from JSON
     *
     * Restores window order as pre-seeded insertion order and floating
     * windows into the saved-floating set for re-floating on arrival.
     *
     * @param orders JSON array produced by serializeWindowOrders()
     */
    void deserializeWindowOrders(const QJsonArray& orders);

    /**
     * @brief Serialize pending autotile restore queues to JSON
     *
     * Returns a JSON object keyed by appId, each containing an array of
     * pending restore entries (position, context, floating state).
     * Called by WTA's save cycle via persistence delegate.
     */
    QJsonObject serializePendingRestores() const;

    /**
     * @brief Deserialize pending autotile restore queues from JSON
     *
     * Restores the close/reopen queue so windows reopened after daemon
     * restart return to their saved positions.
     *
     * @param obj JSON object produced by serializePendingRestores()
     */
    void deserializePendingRestores(const QJsonObject& obj);

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings access
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current Settings pointer (may be null)
     */
    Settings* settings() const
    {
        return m_settings;
    }

private:
    void scheduleSettingsRetile();
    void processSettingsRetile();

    AutotileEngine* m_engine = nullptr;
    QPointer<Settings> m_settings;
    QTimer m_settingsRetileTimer;
    QTimer m_shortcutSaveTimer; // debounced save after shortcut adjustments
    bool m_pendingSettingsRetile = false;
};

} // namespace PlasmaZones
