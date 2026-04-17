// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/constants.h"
#include "../core/layoutmanager.h"
#include <QObject>
#include <QString>

namespace PlasmaZones {

class Settings;
class LayoutManager;

/**
 * @brief Tiling mode: manual zone layouts or automatic tiling algorithms
 */
enum class TilingMode {
    Manual = 0, ///< Traditional zone-based layout
    Autotile = 1 ///< Dynamic auto-tiling algorithm
};

/**
 * @brief Thin convenience wrapper over LayoutManager's per-context AssignmentEntry.
 *
 * All state queries delegate to LayoutManager::assignmentEntryForScreen().
 * Mutation methods write to AssignmentEntry directly. No global [ModeTracking]
 * KConfig group is used — per-context state lives in [Assignment:*] groups.
 *
 * Callers must set the current context (screen, desktop, activity) before
 * querying or mutating state.
 */
class ModeTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TilingMode currentMode READ currentMode NOTIFY currentModeChanged)

public:
    explicit ModeTracker(Settings* settings, LayoutManager* layoutManager, QObject* parent = nullptr);
    ~ModeTracker() override;

    /**
     * @brief Set the context for subsequent queries/mutations
     */
    void setContext(const QString& screenId, int desktop, const QString& activity);

    // ═══════════════════════════════════════════════════════════════════════════
    // Current mode (reads from AssignmentEntry for current context)
    // ═══════════════════════════════════════════════════════════════════════════

    TilingMode currentMode() const;
    bool isAutotileMode() const
    {
        return currentMode() == TilingMode::Autotile;
    }
    bool isManualMode() const
    {
        return currentMode() == TilingMode::Manual;
    }

    /**
     * @brief Check if any screen on the given desktop/activity is in autotile mode.
     *
     * @param desktop  Virtual desktop number (1-based). Pass -1 to use the stored m_desktop.
     * @param activity Activity ID. Pass an empty string to use the stored m_activity.
     *
     * Callers that have a fresh desktop/activity should pass them explicitly. Callers that
     * rely on the stored context should ensure setContext() was called recently with current
     * values to avoid stale results.
     */
    bool isAnyScreenAutotile(int desktop = -1, const QString& activity = QString()) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Layout tracking (reads from AssignmentEntry for current context)
    // ═══════════════════════════════════════════════════════════════════════════

    QString lastManualLayoutId() const;
    QString lastAutotileAlgorithm() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence (no-ops — state lives in LayoutManager's KConfig)
    // ═══════════════════════════════════════════════════════════════════════════

    void load()
    {
    }
    void save()
    {
    }

Q_SIGNALS:
    void currentModeChanged(TilingMode mode);

private:
    Settings* m_settings = nullptr;
    LayoutManager* m_layoutManager = nullptr;
    QString m_screenId;
    int m_desktop = 0;
    QString m_activity;
};

} // namespace PlasmaZones
