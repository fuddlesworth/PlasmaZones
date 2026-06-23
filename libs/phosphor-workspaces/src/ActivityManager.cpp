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
    auto* controller = new KActivities::Controller(this);
    m_controller = controller;

    connect(controller, &KActivities::Controller::serviceStatusChanged, this,
            [this, controller](KActivities::Controller::ServiceStatus status) {
                bool wasAvailable = m_activitiesAvailable;
                m_activitiesAvailable = (status == KActivities::Controller::Running);

                if (m_activitiesAvailable && !wasAvailable) {
                    m_currentActivity = controller->currentActivity();
                    Q_EMIT activitiesChanged();
                    if (!m_currentActivity.isEmpty()) {
                        Q_EMIT currentActivityChanged(m_currentActivity);
                    }
                } else if (!m_activitiesAvailable && wasAvailable) {
                    m_currentActivity.clear();
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
    if (m_running || !m_activitiesAvailable) {
        return;
    }

#ifdef KACTIVITIES_AVAILABLE
    m_running = true;
    connectSignals();
#endif
}

void ActivityManager::stop()
{
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
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }
#endif
}

} // namespace PhosphorWorkspaces
