// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorworkspaces_export.h>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorWorkspaces {

class PHOSPHORWORKSPACES_EXPORT ActivityManager : public QObject
{
    Q_OBJECT

public:
    explicit ActivityManager(QObject* parent = nullptr);
    ~ActivityManager() override;

    bool init();
    void start();
    void stop();

    QString currentActivity() const;
    static bool isAvailable();

    /// Returns @p manager's current activity, or an empty string when @p
    /// manager is null OR the activities backend is unavailable (headless
    /// sessions where a deref would otherwise read a never-connected D-Bus
    /// backend). Centralises the
    /// `(mgr && isAvailable()) ? mgr->currentActivity() : QString()` guard
    /// shared by the daemon's context-resolution sites.
    static QString currentActivityOrEmpty(const ActivityManager* manager);
    QStringList activities() const;
    QString activityName(const QString& activityId) const;
    QString activityIcon(const QString& activityId) const;

Q_SIGNALS:
    void currentActivityChanged(const QString& activityId);
    void activitiesChanged();

private Q_SLOTS:
    void onCurrentActivityChanged(const QString& activityId);
    void onActivityAdded(const QString& activityId);
    void onActivityRemoved(const QString& activityId);

private:
    void connectSignals();
    void disconnectSignals();

    QObject* m_controller = nullptr;
    bool m_running = false;
    QString m_currentActivity;
    bool m_activitiesAvailable = false;
};

} // namespace PhosphorWorkspaces
