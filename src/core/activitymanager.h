// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QString>

namespace PlasmaZones {

class LayoutManager;

/**
 * @brief Manages KDE Activities integration for activity-based layouts (SRP)
 *
 * Handles activity changes and automatically switches layouts
 * based on assignments. Follows Single Responsibility Principle.
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

Q_SIGNALS:
    /**
     * @brief Emitted when activity changes
     * @param activityId New activity ID
     */
    void currentActivityChanged(const QString& activityId);

private Q_SLOTS:
    void onCurrentActivityChanged(const QString& activityId);

private:
    void connectSignals();
    void disconnectSignals();
    void updateActiveLayout();

    LayoutManager* m_layoutManager = nullptr;
    bool m_running = false;
    QString m_currentActivity;
    bool m_activitiesAvailable = false;
};

} // namespace PlasmaZones
