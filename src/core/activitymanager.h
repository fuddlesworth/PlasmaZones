// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

class LayoutManager;
class VirtualDesktopManager;

/**
 * @brief Manages KDE Activities integration for activity-based layouts
 *
 * Handles activity changes and automatically switches layouts
 * based on assignments. 
 *
 * Note: KActivities is optional - if not available, activity support
 * will be limited but the system will still work.
 */
class PLASMAZONES_EXPORT ActivityManager : public QObject
{
    Q_OBJECT

public:
    explicit ActivityManager(LayoutManager* layoutManager, QObject* parent = nullptr);
    ~ActivityManager() override;

    /**
     * @brief Set the VirtualDesktopManager for desktop coordination
     * @param vdm Pointer to VirtualDesktopManager (not owned)
     */
    void setVirtualDesktopManager(VirtualDesktopManager* vdm);

    /**
     * @brief Initialize activity monitoring
     * @return true if successful (false if KActivities not available)
     */
    bool init();

    /**
     * @brief Start monitoring activities
     */
    void start();

    /**
     * @brief Stop monitoring activities
     */
    void stop();

    /**
     * @brief Get current activity ID
     * @return Activity ID or empty string if unavailable
     */
    QString currentActivity() const;

    /**
     * @brief Check if KActivities is available
     * @return true if KActivities runtime is available
     */
    static bool isAvailable();

    /**
     * @brief Get list of all activity IDs
     * @return List of activity UUIDs, empty if KActivities unavailable
     */
    QStringList activities() const;

    /**
     * @brief Get the human-readable name of an activity
     * @param activityId Activity UUID
     * @return Activity name, or empty string if not found
     */
    QString activityName(const QString& activityId) const;

    /**
     * @brief Get icon name for an activity
     * @param activityId Activity UUID
     * @return Icon name (Breeze icon naming), or empty string if not found
     */
    QString activityIcon(const QString& activityId) const;

public Q_SLOTS:
    /**
     * @brief Re-evaluate and update the active layout based on current activity/desktop
     * Called when activity assignments change to refresh the layout
     */
    void updateActiveLayout();

Q_SIGNALS:
    /**
     * @brief Emitted when activity changes
     * @param activityId New activity ID
     */
    void currentActivityChanged(const QString& activityId);

    /**
     * @brief Emitted when the list of activities changes
     */
    void activitiesChanged();

private Q_SLOTS:
    void onCurrentActivityChanged(const QString& activityId);
    void onActivityAdded(const QString& activityId);
    void onActivityRemoved(const QString& activityId);

private:
    void connectSignals();
    void disconnectSignals();

    LayoutManager* m_layoutManager = nullptr;
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;
    QObject* m_controller = nullptr; // KActivities::Controller*, stored as QObject* for optional dependency
    bool m_running = false;
    QString m_currentActivity;
    bool m_activitiesAvailable = false;
};

} // namespace PlasmaZones
