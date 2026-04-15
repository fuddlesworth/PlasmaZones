// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SnapEngine.h"
#include "autotile/AutotileEngine.h"
#include "dbus/zonedetectionadaptor.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"
#include "core/logging.h"

namespace PlasmaZones {

SnapEngine::SnapEngine(LayoutManager* layoutManager, WindowTrackingService* windowTracker, IZoneDetector* zoneDetector,
                       ISettings* settings, VirtualDesktopManager* vdm, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_zoneDetector(zoneDetector)
    , m_settings(settings)
    , m_virtualDesktopManager(vdm)
{
}

SnapEngine::~SnapEngine() = default;

void SnapEngine::setAutotileEngine(AutotileEngine* engine)
{
    m_autotileEngine = engine;
}

void SnapEngine::setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor)
{
    m_zoneDetectionAdaptor = adaptor;
}

// ═══════════════════════════════════════════════════════════════════════════════
// IEngineLifecycle implementation
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::isActiveOnScreen(const QString& screenId) const
{
    // SnapEngine is active on any screen where AutotileEngine is NOT active
    if (m_autotileEngine) {
        return !m_autotileEngine->isAutotileScreen(screenId);
    }
    return true; // No autotile engine → all screens use snapping
}

// windowOpened is implemented in snapengine/lifecycle.cpp

void SnapEngine::windowClosed(const QString& windowId)
{
    Q_UNUSED(windowId)
    // Engine-specific cleanup only. WTS cleanup is handled by
    // WindowTrackingAdaptor::windowClosed() which owns the D-Bus contract.
    // Calling WTS here would cause double-cleanup since the adaptor always runs.
    // When SnapEngine gains its own state (e.g., pending snap queue), clean it here.
}

void SnapEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    Q_UNUSED(windowId)
    m_lastActiveScreenId = screenId;
}

// toggleWindowFloat and setWindowFloat are implemented in snapengine/float.cpp
// focusInDirection, swapInDirection, rotateWindows, moveToPosition are in snapengine/navigation.cpp

void SnapEngine::assignToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId)
{
    if (zoneIds.isEmpty()) {
        qCWarning(lcCore) << "assignToZones: empty zoneIds for" << windowId << "- skipping";
        return;
    }
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (zoneIds.size() > 1) {
        m_windowTracker->assignWindowToZones(windowId, zoneIds, screenId, currentDesktop);
    } else {
        m_windowTracker->assignWindowToZone(windowId, zoneIds.first(), screenId, currentDesktop);
    }
}

void SnapEngine::saveState()
{
    if (m_saveFn) {
        m_saveFn();
    }
}

void SnapEngine::loadState()
{
    if (m_loadFn) {
        m_loadFn();
    }
}

} // namespace PlasmaZones
