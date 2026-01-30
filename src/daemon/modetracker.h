// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/layout.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>

namespace PlasmaZones {

class Settings;

/**
 * @brief Tracks the current tiling mode and remembers last-used selections
 *
 * ModeTracker enables the "smart toggle" feature where Meta+T cycles between
 * the last-used manual layout and the last-used autotile algorithm. It persists
 * state to settings so the user's preferences survive sessions.
 *
 * Usage:
 * @code
 * auto *tracker = new ModeTracker(settings, this);
 * tracker->load();
 *
 * // User selects a manual layout
 * tracker->recordManualLayout(layoutId);
 *
 * // User enables autotile with BSP
 * tracker->recordAutotileAlgorithm("bsp");
 *
 * // Smart toggle (Meta+T)
 * TilingMode newMode = tracker->toggleMode();
 * if (newMode == TilingMode::Autotile) {
 *     engine->setAlgorithm(tracker->lastAutotileAlgorithm());
 *     engine->setEnabled(true);
 * } else {
 *     engine->setEnabled(false);
 *     layoutManager->setActiveLayout(tracker->lastManualLayoutId());
 * }
 * @endcode
 */
class ModeTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TilingMode currentMode READ currentMode WRITE setCurrentMode NOTIFY currentModeChanged)
    Q_PROPERTY(QString lastManualLayoutId READ lastManualLayoutId NOTIFY lastManualLayoutIdChanged)
    Q_PROPERTY(QString lastAutotileAlgorithm READ lastAutotileAlgorithm NOTIFY lastAutotileAlgorithmChanged)

public:
    /**
     * @brief Tiling mode enumeration
     */
    enum class TilingMode {
        Manual = 0,   // Using traditional zone-based layouts
        Autotile = 1  // Using automatic tiling algorithms
    };
    Q_ENUM(TilingMode)

    explicit ModeTracker(Settings* settings, QObject* parent = nullptr);
    ~ModeTracker() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Current mode
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current tiling mode
     */
    TilingMode currentMode() const { return m_currentMode; }

    /**
     * @brief Set the current tiling mode
     */
    void setCurrentMode(TilingMode mode);

    /**
     * @brief Toggle to the opposite mode and return the new mode
     *
     * Switches between Manual and Autotile modes.
     *
     * @return The new mode after toggling
     */
    TilingMode toggleMode();

    /**
     * @brief Check if currently in autotile mode
     */
    bool isAutotileMode() const { return m_currentMode == TilingMode::Autotile; }

    /**
     * @brief Check if currently in manual mode
     */
    bool isManualMode() const { return m_currentMode == TilingMode::Manual; }

    // ═══════════════════════════════════════════════════════════════════════════
    // Last-used tracking
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the UUID of the last manually selected layout
     */
    QString lastManualLayoutId() const { return m_lastManualLayoutId; }

    /**
     * @brief Get the ID of the last used autotile algorithm
     */
    QString lastAutotileAlgorithm() const { return m_lastAutotileAlgorithm; }

    /**
     * @brief Record a manual layout selection
     *
     * Call this when the user selects a manual layout. Updates the last-used
     * manual layout and sets the current mode to Manual.
     *
     * @param layoutId UUID of the selected layout
     */
    void recordManualLayout(const QString& layoutId);

    /**
     * @brief Record a manual layout selection (QUuid overload)
     */
    void recordManualLayout(const QUuid& layoutId);

    /**
     * @brief Record an autotile algorithm selection
     *
     * Call this when the user enables autotiling or selects an algorithm.
     * Updates the last-used algorithm and sets the current mode to Autotile.
     *
     * @param algorithmId Algorithm identifier (e.g., "master-stack", "bsp")
     */
    void recordAutotileAlgorithm(const QString& algorithmId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Load state from settings
     */
    void load();

    /**
     * @brief Save state to settings
     */
    void save();

Q_SIGNALS:
    /**
     * @brief Emitted when the current mode changes
     */
    void currentModeChanged(TilingMode mode);

    /**
     * @brief Emitted when the last manual layout ID changes
     */
    void lastManualLayoutIdChanged(const QString& layoutId);

    /**
     * @brief Emitted when the last autotile algorithm changes
     */
    void lastAutotileAlgorithmChanged(const QString& algorithmId);

    /**
     * @brief Emitted when mode is toggled via toggleMode()
     *
     * This signal provides both the new mode and the relevant ID
     * (layout UUID for Manual, algorithm ID for Autotile).
     */
    void modeToggled(TilingMode newMode, const QString& relevantId);

private:
    QPointer<Settings> m_settings;  // QPointer for safe access if Settings destroyed
    TilingMode m_currentMode = TilingMode::Manual;
    QString m_lastManualLayoutId;
    QString m_lastAutotileAlgorithm;  // Initialized in constructor to DBus::AutotileAlgorithm::MasterStack
};

} // namespace PlasmaZones
