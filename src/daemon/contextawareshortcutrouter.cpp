// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contextawareshortcutrouter.h"
#include "modetracker.h"
#include "../autotile/AutotileEngine.h"
#include "../dbus/windowtrackingadaptor.h"
#include "../core/logging.h"

namespace PlasmaZones {

ContextAwareShortcutRouter::ContextAwareShortcutRouter(ModeTracker* modeTracker,
                                                         AutotileEngine* autotileEngine,
                                                         WindowTrackingAdaptor* windowTrackingAdaptor,
                                                         QObject* parent)
    : QObject(parent)
    , m_modeTracker(modeTracker)
    , m_autotileEngine(autotileEngine)
    , m_windowTrackingAdaptor(windowTrackingAdaptor)
{
}

ContextAwareShortcutRouter::~ContextAwareShortcutRouter() = default;

void ContextAwareShortcutRouter::cycleWindows(bool forward)
{
    if (!m_modeTracker) {
        // Fallback to manual mode behavior if no tracker
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->cycleWindowsInZone(forward);
        }
        return;
    }

    if (m_modeTracker->isAutotileMode() && m_autotileEngine && m_autotileEngine->isEnabled()) {
        // Autotile mode: use focusNext/focusPrevious
        qCDebug(lcDaemon) << "Routing cycleWindows to autotile focus" << (forward ? "next" : "previous");
        if (forward) {
            m_autotileEngine->focusNext();
        } else {
            m_autotileEngine->focusPrevious();
        }
    } else {
        // Manual mode: cycle windows in zone
        qCDebug(lcDaemon) << "Routing cycleWindows to zone cycling" << (forward ? "forward" : "backward");
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->cycleWindowsInZone(forward);
        }
    }
}

void ContextAwareShortcutRouter::rotateWindows(bool clockwise)
{
    if (!m_modeTracker) {
        // Fallback to manual mode behavior if no tracker
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->rotateWindowsInLayout(clockwise);
        }
        return;
    }

    if (m_modeTracker->isAutotileMode() && m_autotileEngine && m_autotileEngine->isEnabled()) {
        // Autotile mode: rotate window order in tiling stack
        qCDebug(lcDaemon) << "Routing rotateWindows to autotile" << (clockwise ? "clockwise" : "counterclockwise");
        m_autotileEngine->rotateWindowOrder(clockwise);
    } else {
        // Manual mode: rotate windows through zones
        qCDebug(lcDaemon) << "Routing rotateWindows to zone rotation" << (clockwise ? "clockwise" : "counterclockwise");
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->rotateWindowsInLayout(clockwise);
        }
    }
}

void ContextAwareShortcutRouter::toggleFloat()
{
    if (!m_modeTracker) {
        // Fallback to manual mode behavior if no tracker
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->toggleWindowFloat();
        }
        return;
    }

    if (m_modeTracker->isAutotileMode() && m_autotileEngine && m_autotileEngine->isEnabled()) {
        // Autotile mode: toggle floating state in tiling
        qCDebug(lcDaemon) << "Routing toggleFloat to autotile";
        m_autotileEngine->toggleFocusedWindowFloat();
    } else {
        // Manual mode: unsnap window from zone
        qCDebug(lcDaemon) << "Routing toggleFloat to zone unsnap";
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->toggleWindowFloat();
        }
    }
}

} // namespace PlasmaZones
