// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/layoututils.h"
#include <QObject>
#include <QPointer>
#include <QString>

namespace PlasmaZones {

class LayoutManager;
class AutotileEngine;
class Settings;
class Layout;

/**
 * @brief Controller for unified layout management (manual layouts + autotile algorithms)
 *
 * Extracted from Daemon to follow SRP. Handles:
 * - Quick layout switching (Meta+1-9)
 * - Layout cycling (Meta+[/])
 * - ID-based layout tracking (more robust than index-based)
 *
 * Usage:
 * @code
 * auto *controller = new UnifiedLayoutController(layoutManager, autotileEngine, settings, parent);
 *
 * // Quick layout switch
 * controller->applyLayoutByNumber(1);  // First layout
 *
 * // Cycling
 * controller->cycleNext();
 * controller->cyclePrevious();
 *
 * // Connect OSD signals
 * connect(controller, &UnifiedLayoutController::layoutApplied, this, &Daemon::showLayoutOsd);
 * connect(controller, &UnifiedLayoutController::autotileApplied, this, &Daemon::showAutotileOsd);
 * @endcode
 *
 * @note Thread Safety: All methods should be called from the main thread.
 */
class UnifiedLayoutController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLayoutId READ currentLayoutId NOTIFY currentLayoutIdChanged)

public:
    explicit UnifiedLayoutController(LayoutManager* layoutManager, AutotileEngine* autotileEngine,
                                     Settings* settings, QObject* parent = nullptr);
    ~UnifiedLayoutController() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout access
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current layout ID
     *
     * Returns either a layout UUID or "autotile:<algorithm-id>".
     */
    QString currentLayoutId() const { return m_currentLayoutId; }

    /**
     * @brief Get the current layout entry
     *
     * @return Pointer to entry if found, nullptr otherwise
     */
    const UnifiedLayoutEntry* currentLayout() const;

    /**
     * @brief Get the full unified layout list
     */
    QVector<UnifiedLayoutEntry> layouts() const;

    /**
     * @brief Get unified layouts as QVariantList for QML
     */
    QVariantList layoutsAsVariantList() const;

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
     * @param layoutId Layout UUID or "autotile:<algorithm-id>"
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

Q_SIGNALS:
    /**
     * @brief Emitted when current layout ID changes
     */
    void currentLayoutIdChanged(const QString& layoutId);

    /**
     * @brief Emitted when a manual layout is applied (for OSD)
     */
    void layoutApplied(Layout* layout);

    /**
     * @brief Emitted when an autotile algorithm is applied (for OSD)
     */
    void autotileApplied(const QString& algorithmId);

    /**
     * @brief Emitted when layout list changes
     */
    void layoutsChanged();

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
    QPointer<AutotileEngine> m_autotileEngine;
    QPointer<Settings> m_settings;

    QString m_currentLayoutId;
    mutable QVector<UnifiedLayoutEntry> m_cachedLayouts;
    mutable bool m_cacheValid = false;
};

} // namespace PlasmaZones
