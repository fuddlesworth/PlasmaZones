// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "activitymanager.h"
#include "layoutmanager.h"
#include "logging.h"
#include <QGuiApplication>
#include <QScreen>

// KActivities is optional - check at compile time
#ifdef HAVE_KACTIVITIES
#include <KActivities/Controller>
#include <KActivities/Info>
#define KACTIVITIES_AVAILABLE
#endif

namespace PlasmaZones {

ActivityManager::ActivityManager(LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
{
    Q_ASSERT(layoutManager);
}

ActivityManager::~ActivityManager()
{
    stop();
}

bool ActivityManager::isAvailable()
{
#ifdef KACTIVITIES_AVAILABLE
    // Check if KActivities runtime is available
    return KActivities::Controller().serviceStatus() == KActivities::Controller::Running;
#else
    return false;
#endif
}

bool ActivityManager::init()
{
#ifdef KACTIVITIES_AVAILABLE
    m_activitiesAvailable = (KActivities::Controller().serviceStatus() == KActivities::Controller::Running);
    if (m_activitiesAvailable) {
        m_currentActivity = KActivities::Controller().currentActivity();
        qCDebug(lcCore) << "KActivities available, current activity:" << m_currentActivity;
    } else {
        qCDebug(lcCore) << "KActivities service not running - activity support disabled";
    }
    return true; // Always return true - activities are optional
#else
    qCDebug(lcCore) << "KActivities support not compiled in - activity support disabled";
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
        qCDebug(lcCore) << "Activities not available - skipping activity manager start";
        return;
    }

#ifdef KACTIVITIES_AVAILABLE
    m_running = true;
    connectSignals();
    updateActiveLayout();
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

void ActivityManager::onCurrentActivityChanged(const QString& activityId)
{
    if (m_currentActivity == activityId) {
        return;
    }

    m_currentActivity = activityId;
    qCDebug(lcCore) << "Activity changed to:" << activityId;

    updateActiveLayout();
    Q_EMIT currentActivityChanged(activityId);
}

void ActivityManager::connectSignals()
{
#ifdef KACTIVITIES_AVAILABLE
    auto& controller = KActivities::Controller::instance();
    connect(&controller, &KActivities::Controller::currentActivityChanged, this,
            &ActivityManager::onCurrentActivityChanged);
#endif
}

void ActivityManager::disconnectSignals()
{
#ifdef KACTIVITIES_AVAILABLE
    auto& controller = KActivities::Controller::instance();
    disconnect(&controller, nullptr, this, nullptr);
#endif
}

void ActivityManager::updateActiveLayout()
{
    if (!m_layoutManager || !m_activitiesAvailable || m_currentActivity.isEmpty()) {
        return;
    }

    // Get primary screen name
    const auto* screen = qGuiApp->primaryScreen();
    if (!screen) {
        return;
    }

    // Get current virtual desktop (coordinate with VirtualDesktopManager)
    // Use 0 for all desktops to prioritize activity, or get current desktop if needed
    int currentDesktop = 0; // Could integrate with VirtualDesktopManager if needed

    // Find layout for current screen, desktop, and activity
    // Activity takes precedence over desktop in lookup
    auto* layout = m_layoutManager->layoutForScreen(screen->name(), currentDesktop, m_currentActivity);

    if (layout && layout != m_layoutManager->activeLayout()) {
        qCDebug(lcCore) << "Switching to layout" << layout->name() << "for activity" << m_currentActivity << "desktop"
                        << currentDesktop << "on screen" << screen->name();
        m_layoutManager->setActiveLayout(layout);
    }
}

} // namespace PlasmaZones
