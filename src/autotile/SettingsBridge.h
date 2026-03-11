// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
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
 * - Session persistence via KConfig (saveState/loadState)
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Session persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Save tiling state to KConfig for session persistence
     */
    void saveState();

    /**
     * @brief Load tiling state from KConfig
     */
    void loadState();

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
    bool m_pendingSettingsRetile = false;
};

} // namespace PlasmaZones
