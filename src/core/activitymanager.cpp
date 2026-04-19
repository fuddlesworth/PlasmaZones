// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "activitymanager.h"
#include <PhosphorZones/LayoutManager.h>
#include "virtualdesktopmanager.h"
#include "logging.h"
#include "utils.h"
#include <QGuiApplication>
#include <QScreen>

// PlasmaActivities/KActivities is optional - check at compile time
// Plasma 6 uses PlasmaActivities package but keeps KActivities namespace
#ifdef HAVE_KACTIVITIES
// Try Plasma 6 headers first (PlasmaActivities package)
#if __has_include(<PlasmaActivities/plasmaactivities/controller.h>)
#include <PlasmaActivities/plasmaactivities/controller.h>
#include <PlasmaActivities/plasmaactivities/info.h>
#define PLASMA_ACTIVITIES_V6
#elif __has_include(<plasmaactivities/controller.h>)
#include <plasmaactivities/controller.h>
#include <plasmaactivities/info.h>
#define PLASMA_ACTIVITIES_V6
#else
// Fall back to KActivities (KF5/KF6)
#include <KActivities/Controller>
#include <KActivities/Info>
#endif
#define KACTIVITIES_AVAILABLE
#endif

namespace PlasmaZones {

ActivityManager::ActivityManager(PhosphorZones::LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
{
    Q_ASSERT(layoutManager);
}

ActivityManager::~ActivityManager()
{
    stop();
}

void ActivityManager::setVirtualDesktopManager(VirtualDesktopManager* vdm)
{
    m_virtualDesktopManager = vdm;
}

bool ActivityManager::isAvailable()
{
#ifdef KACTIVITIES_AVAILABLE
    // Check if we have a running controller with Running status
    // Note: This is a quick check; full availability requires async init
    return true; // Compiled with support, actual status checked via D-Bus
#else
    return false;
#endif
}

bool ActivityManager::init()
{
#ifdef KACTIVITIES_AVAILABLE
    // Create our persistent controller instance
    // Important: Controller needs to be long-lived to sync with the service
    auto* controller = new KActivities::Controller(this);
    m_controller = controller;

    // Connect to serviceStatusChanged to handle async service availability
    connect(controller, &KActivities::Controller::serviceStatusChanged, this,
            [this, controller](KActivities::Controller::ServiceStatus status) {
                bool wasAvailable = m_activitiesAvailable;
                m_activitiesAvailable = (status == KActivities::Controller::Running);

                if (m_activitiesAvailable && !wasAvailable) {
                    // Service just became available - fetch current activity
                    m_currentActivity = controller->currentActivity();
                    qCInfo(lcCore) << "KActivities service now running, current activity:" << m_currentActivity << "("
                                   << activityName(m_currentActivity) << ")";

                    // Emit signals so UI can update
                    Q_EMIT activitiesChanged();
                    if (!m_currentActivity.isEmpty()) {
                        Q_EMIT currentActivityChanged(m_currentActivity);
                    }

                    // PhosphorZones::Layout resolution is handled by the daemon's
                    // syncModeFromAssignments() via currentActivityChanged signal.
                } else if (!m_activitiesAvailable && wasAvailable) {
                    qCWarning(lcCore) << "KActivities service stopped";
                    m_currentActivity.clear();
                    Q_EMIT activitiesChanged();
                }
            });

    // Check initial status
    auto status = controller->serviceStatus();
    m_activitiesAvailable = (status == KActivities::Controller::Running);

    if (m_activitiesAvailable) {
        m_currentActivity = controller->currentActivity();
        qCInfo(lcCore) << "KActivities available, current activity:" << m_currentActivity << "("
                       << activityName(m_currentActivity) << ")";
    } else if (status == KActivities::Controller::Unknown) {
        // Service status unknown - it may become available later
        qCInfo(lcCore) << "KActivities: service status unknown, waiting for connection";
    } else {
        qCInfo(lcCore) << "KActivities: service not running, activity support disabled";
    }

    return true; // Always return true - activities are optional
#else
    qCInfo(lcCore) << "KActivities: not compiled in, activity support disabled";
    m_activitiesAvailable = false;
    return true; // Return true since activities are optional
#endif
}

void ActivityManager::start()
{
    if (m_running) {
        return;
    }

    if (!m_activitiesAvailable) {
        qCDebug(lcCore) << "ActivityManager: not available, skipping start";
        return;
    }

#ifdef KACTIVITIES_AVAILABLE
    m_running = true;
    connectSignals();
    // PhosphorZones::Layout resolution is handled by the daemon via currentActivityChanged signal.
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
    qCInfo(lcCore) << "Activity changed activity=" << activityId << "name=" << activityName(activityId);

    Q_EMIT currentActivityChanged(activityId);
}

void ActivityManager::onActivityAdded(const QString& activityId)
{
    qCInfo(lcCore) << "Activity added activity=" << activityId << "name=" << activityName(activityId);
    Q_EMIT activitiesChanged();
}

void ActivityManager::onActivityRemoved(const QString& activityId)
{
    qCInfo(lcCore) << "Activity removed activity=" << activityId;
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

// NOTE: updateActiveLayout() was removed — layout resolution is handled exclusively
// by the daemon's syncModeFromAssignments() which understands autotile vs snapping mode.
// ActivityManager only tracks activity state and emits signals.

} // namespace PlasmaZones
