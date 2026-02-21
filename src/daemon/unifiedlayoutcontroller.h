// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/layoututils.h"
#include <QObject>
#include <QPointer>
#include <QString>

namespace PlasmaZones {

class AutotileEngine;
class LayoutManager;
class Settings;
class Layout;

/**
 * @brief Controller for unified layout management (manual layouts)
 *
 * Handles:
 * - Quick layout switching (Meta+1-9)
 * - Layout cycling (Meta+[/])
 * - ID-based layout tracking
 *
 * Usage:
 * @code
 * auto *controller = new UnifiedLayoutController(layoutManager, settings, parent);
 * controller->applyLayoutByNumber(1);
 * controller->cycleNext();
 * connect(controller, &UnifiedLayoutController::layoutApplied, this, &Daemon::showLayoutOsd);
 * @endcode
 *
 * @note Thread Safety: All methods should be called from the main thread.
 */
class UnifiedLayoutController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLayoutId READ currentLayoutId)

public:
    explicit UnifiedLayoutController(LayoutManager* layoutManager, Settings* settings,
                                     AutotileEngine* autotileEngine = nullptr, QObject* parent = nullptr);
    ~UnifiedLayoutController() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout access
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current layout ID (layout UUID).
     */
    QString currentLayoutId() const { return m_currentLayoutId; }

    /**
     * @brief Get the full unified layout list
     */
    QVector<UnifiedLayoutEntry> layouts() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout application
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply layout by number (1-based, for Meta+1-9 shortcuts)
     *
     * @param number Layout number (1 = first layout)
     * @return true if layout was applied successfully
     */
    Q_INVOKABLE bool applyLayoutByNumber(int number);

    /**
     * @brief Apply layout by ID
     *
     * @param layoutId Layout UUID
     * @return true if layout was applied successfully
     */
    Q_INVOKABLE bool applyLayoutById(const QString& layoutId);

    /**
     * @brief Apply layout by index (0-based)
     *
     * @param index Index in unified layout list
     * @return true if layout was applied successfully
     */
    Q_INVOKABLE bool applyLayoutByIndex(int index);

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout cycling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Cycle to the next layout (Meta+])
     */
    Q_INVOKABLE void cycleNext();

    /**
     * @brief Cycle to the previous layout (Meta+[)
     */
    Q_INVOKABLE void cyclePrevious();

    /**
     * @brief Cycle layouts in specified direction
     *
     * @param forward true for next, false for previous
     */
    Q_INVOKABLE void cycle(bool forward);

    // ═══════════════════════════════════════════════════════════════════════════
    // State synchronization
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Synchronize current layout ID from external state
     *
     * Call this when layout changes from other sources (zone selector, D-Bus).
     * Updates internal tracking without triggering signals.
     */
    void syncFromExternalState();

    /**
     * @brief Get current screen name
     */
    QString currentScreenName() const { return m_currentScreenName; }

    /**
     * @brief Set current screen name for per-screen visibility filtering
     */
    void setCurrentScreenName(const QString& screenName);

    /**
     * @brief Set current virtual desktop for visibility filtering
     */
    void setCurrentVirtualDesktop(int desktop);

    /**
     * @brief Set current activity for visibility filtering
     */
    void setCurrentActivity(const QString& activity);

    /**
     * @brief Set which layout types to include in cycling/shortcuts
     *
     * In manual mode: only manual layouts. In autotile mode: only dynamic layouts.
     * The autotile feature gate controls whether dynamic layouts are ever visible.
     */
    void setLayoutFilter(bool includeManual, bool includeAutotile);

Q_SIGNALS:
    /**
     * @brief Emitted when a manual layout is applied (for OSD)
     */
    void layoutApplied(Layout* layout);

    /**
     * @brief Emitted when an autotile algorithm is applied
     * @param algorithmName Display name of the algorithm
     * @param windowCount Number of currently tiled windows (0 if unknown)
     */
    void autotileApplied(const QString& algorithmName, int windowCount);

private:
    /**
     * @brief Apply a unified layout entry
     */
    bool applyEntry(const UnifiedLayoutEntry& entry);

    /**
     * @brief Update current layout ID and emit signal
     */
    void setCurrentLayoutId(const QString& layoutId);

    /**
     * @brief Find current index in layout list
     */
    int findCurrentIndex() const;

    QPointer<LayoutManager> m_layoutManager;
    QPointer<Settings> m_settings;
    QPointer<AutotileEngine> m_autotileEngine;

    QString m_currentLayoutId;
    QString m_currentScreenName;
    int m_currentVirtualDesktop = 1;
    QString m_currentActivity;
    bool m_includeManualLayouts = true;
    bool m_includeAutotileLayouts = false;
    mutable QVector<UnifiedLayoutEntry> m_cachedLayouts;
    mutable bool m_cacheValid = false;
};

} // namespace PlasmaZones
