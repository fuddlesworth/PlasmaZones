// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/ActivityManager.h>

#ifdef HAVE_KACTIVITIES
#if __has_include(<PlasmaActivities/plasmaactivities/controller.h>)
#include <PlasmaActivities/plasmaactivities/controller.h>
#include <PlasmaActivities/plasmaactivities/info.h>
#define PLASMA_ACTIVITIES_V6
#elif __has_include(<plasmaactivities/controller.h>)
#include <plasmaactivities/controller.h>
#include <plasmaactivities/info.h>
#define PLASMA_ACTIVITIES_V6
#else
#include <KActivities/Controller>
#include <KActivities/Info>
#endif
#define KACTIVITIES_AVAILABLE
#endif

namespace PhosphorWorkspaces {

ActivityManager::ActivityManager(QObject* parent)
    : QObject(parent)
{
}

ActivityManager::~ActivityManager()
{
    stop();
}

bool ActivityManager::isAvailable()
{
#ifdef KACTIVITIES_AVAILABLE
    return true;
#else
    return false;
#endif
}

bool ActivityManager::init()
{
#ifdef KACTIVITIES_AVAILABLE
    // A second init would create a second controller and stack a second
    // status handler on it, each announcing every service bounce.
    if (m_controller) {
        return true;
    }
    auto* controller = new KActivities::Controller(this);
    m_controller = controller;

    connect(controller, &KActivities::Controller::serviceStatusChanged, this,
            [this](KActivities::Controller::ServiceStatus status) {
                bool wasAvailable = m_activitiesAvailable;
                m_activitiesAvailable = (status == KActivities::Controller::Running);

                // A manager that was never started, or was stopped, tracks
                // availability only. It reports nothing, and start() picks up
                // whatever changed in the meantime.
                if (!m_startRequested) {
                    return;
                }

                if (m_activitiesAvailable && !wasAvailable) {
                    // A start() that ran while the service was still down
                    // returned without connecting the activity signals. Connect
                    // them now, or no later activity switch is ever seen.
                    if (!m_running) {
                        m_running = true;
                        connectSignals();
                    }
                    Q_EMIT activitiesChanged();
                    // Notifies only on a real change. A service that merely
                    // restarted reports the activity kept through its outage,
                    // so the return is not announced as a switch. The first
                    // activity known after a late start is a change, and is.
                    adoptControllerActivity();
                } else if (!m_activitiesAvailable && wasAvailable) {
                    // The last known activity is kept through the outage.
                    // Clearing it would resolve every placement made meanwhile
                    // under "every activity" while the layout registry and the
                    // engines still hold the last one, and the service's return
                    // would then announce a switch to an activity that never
                    // changed.
                    Q_EMIT activitiesChanged();
                }
            });

    auto status = controller->serviceStatus();
    m_activitiesAvailable = (status == KActivities::Controller::Running);

    if (m_activitiesAvailable) {
        m_currentActivity = controller->currentActivity();
    }

    return true;
#else
    m_activitiesAvailable = false;
    return true;
#endif
}

void ActivityManager::start()
{
    // Remembered so a service that comes up after this call still gets its
    // signals connected (see the serviceStatusChanged handler in init()).
    m_startRequested = true;
    if (m_running || !m_activitiesAvailable) {
        return;
    }

#ifdef KACTIVITIES_AVAILABLE
    m_running = true;
    connectSignals();
    // The status handler reports nothing while the manager is stopped, and
    // the activity signals were disconnected, so catch up on a switch made
    // in the meantime (or on a service that came up between init() and here).
    adoptControllerActivity();
#endif
}

void ActivityManager::adoptControllerActivity()
{
#ifdef KACTIVITIES_AVAILABLE
    auto* controller = qobject_cast<KActivities::Controller*>(m_controller);
    if (!controller) {
        return;
    }
    // An empty answer is the service not having settled on one yet. The
    // controller's own currentActivityChanged reports it once it has, so the
    // kept activity is not cleared for it.
    const QString activityId = controller->currentActivity();
    if (!activityId.isEmpty()) {
        onCurrentActivityChanged(activityId);
    }
#endif
}

void ActivityManager::stop()
{
    m_startRequested = false;
    if (!m_running) {
        return;
    }

    m_running = false;
#ifdef KACTIVITIES_AVAILABLE
    disconnectSignals();
#endif
}

QString ActivityManager::currentActivity() const
{
    return m_currentActivity;
}

QString ActivityManager::currentActivityOrEmpty(const ActivityManager* manager)
{
    return (manager && isAvailable()) ? manager->currentActivity() : QString();
}

QStringList ActivityManager::activities() const
{
#ifdef KACTIVITIES_AVAILABLE
    if (!m_activitiesAvailable || !m_controller) {
        return {};
    }
    auto* controller = qobject_cast<KActivities::Controller*>(m_controller);
    return controller ? controller->activities() : QStringList{};
#else
    return {};
#endif
}

QString ActivityManager::activityName(const QString& activityId) const
{
#ifdef KACTIVITIES_AVAILABLE
    if (!m_activitiesAvailable || activityId.isEmpty()) {
        return {};
    }
    KActivities::Info info(activityId);
    return info.name();
#else
    Q_UNUSED(activityId)
    return {};
#endif
}

QString ActivityManager::activityIcon(const QString& activityId) const
{
#ifdef KACTIVITIES_AVAILABLE
    if (!m_activitiesAvailable || activityId.isEmpty()) {
        return {};
    }
    KActivities::Info info(activityId);
    return info.icon();
#else
    Q_UNUSED(activityId)
    return {};
#endif
}

void ActivityManager::onCurrentActivityChanged(const QString& activityId)
{
    if (m_currentActivity == activityId) {
        return;
    }

    m_currentActivity = activityId;
    Q_EMIT currentActivityChanged(activityId);
}

void ActivityManager::onActivityAdded(const QString& activityId)
{
    Q_UNUSED(activityId)
    Q_EMIT activitiesChanged();
}

void ActivityManager::onActivityRemoved(const QString& activityId)
{
    Q_UNUSED(activityId)
    Q_EMIT activitiesChanged();
}

void ActivityManager::connectSignals()
{
#ifdef KACTIVITIES_AVAILABLE
    if (!m_controller) {
        return;
    }
    auto* controller = qobject_cast<KActivities::Controller*>(m_controller);
    if (!controller) {
        return;
    }
    connect(controller, &KActivities::Controller::currentActivityChanged, this,
            &ActivityManager::onCurrentActivityChanged);
    connect(controller, &KActivities::Controller::activityAdded, this, &ActivityManager::onActivityAdded);
    connect(controller, &KActivities::Controller::activityRemoved, this, &ActivityManager::onActivityRemoved);
#endif
}

void ActivityManager::disconnectSignals()
{
#ifdef KACTIVITIES_AVAILABLE
    // Only the signals connectSignals() made. A blanket disconnect would also
    // drop init()'s serviceStatusChanged handler, and a service restart after
    // stop() and start() would then go unnoticed.
    auto* controller = qobject_cast<KActivities::Controller*>(m_controller);
    if (!controller) {
        return;
    }
    disconnect(controller, &KActivities::Controller::currentActivityChanged, this,
               &ActivityManager::onCurrentActivityChanged);
    disconnect(controller, &KActivities::Controller::activityAdded, this, &ActivityManager::onActivityAdded);
    disconnect(controller, &KActivities::Controller::activityRemoved, this, &ActivityManager::onActivityRemoved);
#endif
}

} // namespace PhosphorWorkspaces
