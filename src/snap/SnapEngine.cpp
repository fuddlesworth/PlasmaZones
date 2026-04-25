// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SnapEngine.h"
#include "SnapState.h"
#include <PhosphorTileEngine/AutotileEngine.h>
#include "dbus/snapnavigationtargets.h"
#include "dbus/windowtrackingadaptor.h"
#include "dbus/zonedetectionadaptor.h"
#include "core/isettings.h"
#include "core/logging.h"

namespace PlasmaZones {

// In production (Daemon::start) all dependencies are non-null. Headless unit
// tests deliberately pass nullptr to construct an engine with minimal parents
// for testing peripheral classes (adaptors, bridges) — every method that
// dereferences a dependency guards it locally. Do not Q_ASSERT here.
SnapEngine::SnapEngine(PhosphorZones::LayoutRegistry* layoutManager,
                       PhosphorEngineApi::IWindowTrackingService* windowTracker,
                       PhosphorZones::IZoneDetector* zoneDetector, PhosphorEngineApi::IVirtualDesktopManager* vdm,
                       QObject* parent)
    : PlacementEngineBase(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_snapState(new PhosphorZones::SnapState(QString(), this))
    , m_zoneDetector(zoneDetector)
    , m_virtualDesktopManager(vdm)
{
}

ISettings* SnapEngine::snapSettings() const
{
    return qobject_cast<ISettings*>(engineSettings());
}

// Out-of-line so unique_ptr<SnapNavigationTargetResolver> can destroy the
// pimpl-style owned resolver without its full type being visible in the
// header (forward-declared in SnapEngine.h).
SnapEngine::~SnapEngine() = default;

void SnapEngine::onWindowClaimed(const QString& windowId)
{
    Q_UNUSED(windowId)
    // PlacementEngineBase is the single store for unmanaged geometry.
    // No WTS propagation needed.
}

void SnapEngine::onWindowReleased(const QString& windowId)
{
    Q_UNUSED(windowId)
    // PlacementEngineBase is the single store for unmanaged geometry.
    // No WTS propagation needed.
}

void SnapEngine::onWindowFloated(const QString& windowId)
{
    Q_UNUSED(windowId)
}

void SnapEngine::onWindowUnfloated(const QString& windowId)
{
    Q_UNUSED(windowId)
}

void SnapEngine::saveSnapFloating(const QString& windowId)
{
    m_savedSnapFloatingWindows.insert(windowId);
}

bool SnapEngine::restoreSnapFloating(const QString& windowId)
{
    return m_savedSnapFloatingWindows.remove(windowId);
}

void SnapEngine::clearSavedSnapFloating()
{
    m_savedSnapFloatingWindows.clear();
}

void SnapEngine::markWindowReported(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_effectReportedWindows.insert(windowId);
    }
}

int SnapEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    for (auto it = m_savedSnapFloatingWindows.begin(); it != m_savedSnapFloatingWindows.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_savedSnapFloatingWindows.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    for (auto it = m_effectReportedWindows.begin(); it != m_effectReportedWindows.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_effectReportedWindows.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    return pruned;
}

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
    // Sever the resolver's raw pointer when the adaptor is destroyed
    // out-of-band. Production always destroys the adaptor before the
    // engine, but tests and shutdown orderings can diverge; without this
    // the resolver would hold a dangling ZoneDetectionAdaptor*.
    if (adaptor) {
        connect(adaptor, &QObject::destroyed, this, [this]() {
            if (m_targetResolver) {
                m_targetResolver->setZoneDetector(nullptr);
            }
        });
    }
}

SnapNavigationTargetResolver* SnapEngine::ensureTargetResolver(const QString& action)
{
    if (m_targetResolver) {
        return m_targetResolver.get();
    }
    if (!m_windowTracker || !m_layoutManager) {
        qCWarning(lcCore) << "ensureTargetResolver: missing deps "
                          << "windowTracker=" << static_cast<void*>(m_windowTracker)
                          << "layoutManager=" << static_cast<void*>(m_layoutManager);
        if (!action.isEmpty()) {
            // Surface a specific reason so the OSD doesn't silently swallow
            // the shortcut. engine_unavailable is the canonical tag for
            // "engine couldn't be built" — distinct from no_window /
            // invalid_direction / excluded etc.
            Q_EMIT navigationFeedback(false, action, QStringLiteral("engine_unavailable"), QString(), QString(),
                                      QString());
        }
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
// IPlacementEngine — lifecycle
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
    m_effectReportedWindows.remove(windowId);
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

// SnapEngine::assignToZones was removed — its two callers (windowOpened
// in lifecycle.cpp, unfloatToZone in float.cpp) now go through
// WindowTrackingService::commitSnap / commitMultiZoneSnap which run the
// full snap orchestration (clear floating, assign zone, emit state
// change). The raw-assign path was the last thin wrapper that bypassed
// the orchestration layer.

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

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — navigation overrides (thin delegates)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    rotateWindowsInLayout(clockwise, ctx.screenId);
}

void SnapEngine::reapplyLayout(const NavigationContext& /*ctx*/)
{
    resnapToNewLayout();
}

void SnapEngine::snapAllWindows(const NavigationContext& ctx)
{
    snapAllWindows(ctx.screenId);
}

void SnapEngine::pushToEmptyZone(const NavigationContext& ctx)
{
    pushFocusedToEmptyZone(ctx);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — state access
//
// Returns the single SnapState instance wired by Daemon::init(). Currently a
// global state (not per-screen); a future PR will introduce per-screen
// ownership. Callers should null-check — headless unit tests may not wire a
// SnapState.
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngineApi::IPlacementState* SnapEngine::stateForScreen(const QString& screenId)
{
    Q_UNUSED(screenId)
    return m_snapState;
}

const PhosphorEngineApi::IPlacementState* SnapEngine::stateForScreen(const QString& screenId) const
{
    Q_UNUSED(screenId)
    return m_snapState;
}

} // namespace PlasmaZones
