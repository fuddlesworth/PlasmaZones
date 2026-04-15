// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SnapEngine.h"
#include "autotile/AutotileEngine.h"
#include "dbus/snapnavigationtargets.h"
#include "dbus/zonedetectionadaptor.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"
#include "core/logging.h"

namespace PlasmaZones {

// In production (Daemon::start) all dependencies are non-null. Headless unit
// tests deliberately pass nullptr to construct an engine with minimal parents
// for testing peripheral classes (adaptors, bridges) — every method that
// dereferences a dependency guards it locally. Do not Q_ASSERT here.
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

// Out-of-line so unique_ptr<SnapNavigationTargetResolver> can destroy the
// pimpl-style owned resolver without its full type being visible in the
// header (forward-declared in SnapEngine.h).
SnapEngine::~SnapEngine() = default;

void SnapEngine::setAutotileEngine(AutotileEngine* engine)
{
    m_autotileEngine = engine;
}

void SnapEngine::setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor)
{
    m_zoneDetectionAdaptor = adaptor;
    // Push the zone detector into the target resolver if it exists yet.
    // The resolver constructs lazily on first navigation call; if that
    // hasn't happened yet, ensureTargetResolver() will pick up the adaptor
    // from m_zoneDetectionAdaptor when it first runs.
    if (m_targetResolver) {
        m_targetResolver->setZoneDetector(adaptor);
    }
}

SnapNavigationTargetResolver* SnapEngine::ensureTargetResolver()
{
    if (m_targetResolver) {
        return m_targetResolver.get();
    }
    if (!m_windowTracker || !m_layoutManager) {
        qCWarning(lcCore) << "ensureTargetResolver: missing deps "
                          << "windowTracker=" << static_cast<void*>(m_windowTracker)
                          << "layoutManager=" << static_cast<void*>(m_layoutManager);
        return nullptr;
    }
    // Feedback callback forwards into SnapEngine's own navigationFeedback
    // signal — SnapAdaptor relays that to WindowTrackingAdaptor's D-Bus
    // navigationFeedback signal, so external consumers see the same
    // wire format as when the resolver lived on WTA.
    m_targetResolver = std::make_unique<SnapNavigationTargetResolver>(
        m_windowTracker, m_layoutManager, m_zoneDetectionAdaptor.data(),
        [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
               const QString& targetZoneId, const QString& screenId) {
            Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
        });
    return m_targetResolver.get();
}

void SnapEngine::setWindowTrackingAdaptor(WindowTrackingAdaptor* adaptor)
{
    m_wta = adaptor;
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
// Navigation entry points (focusInDirection, moveFocusedInDirection,
// swapFocusedInDirection, moveFocusedToPosition, pushFocusedToEmptyZone,
// restoreFocusedWindow, toggleFocusedFloat, cycleFocus,
// rotateWindowsInLayout, resnapCurrentAssignments, resnapToNewLayout)
// live in snapengine/navigation_actions.cpp and call back into
// WindowTrackingAdaptor via m_wta for target resolution and shared
// bookkeeping helpers. The resnap-by-layout-switch pipeline
// (calculateResnapEntriesFromAutotileOrder, snapAllWindows etc.) lives
// in snapengine/navigation.cpp unchanged.

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
