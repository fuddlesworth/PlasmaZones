// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../../config/settings.h"
#include "../../core/logging.h"
#include "../../core/screenmoderouter.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualscreenswapper.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "shared/virtualscreenid.h"
#include "../../autotile/AutotileEngine.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../snap/SnapEngine.h"
#include "../../core/iwindowengine.h"
#include "../modetracker.h"
#include <QScreen>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Engine routing
// ═══════════════════════════════════════════════════════════════════════════════

IEngineLifecycle* Daemon::engineForScreen(const QString& screenId) const
{
    // Single source of truth. Delegates to the central router so daemon
    // navigation handlers, adaptor D-Bus entry points, and resnap paths
    // all agree on the same mode-ownership decision.
    if (m_screenModeRouter) {
        return m_screenModeRouter->engineFor(screenId);
    }
    // Fallback for very-early-startup paths where the router isn't wired
    // yet. Mirrors the legacy logic.
    if (isAutotileScreen(screenId)) {
        return m_autotileEngine.get();
    }
    if (m_snapEngine) {
        return m_snapEngine.get();
    }
    return nullptr;
}

// Local helper: mode check with nullptr-safe fallback. Every daemon
// navigation handler routes through this so the autotile-vs-snap branch
// is expressed as "does the router say autotile?" rather than inspecting
// the engine pointer directly.
bool Daemon::isAutotileScreen(const QString& screenId) const
{
    if (m_screenModeRouter) {
        return m_screenModeRouter->isAutotileMode(screenId);
    }
    return m_autotileEngine && m_autotileEngine->isAutotileScreen(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation handlers — single code path per operation (DRY/SOLID)
// Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
// ═══════════════════════════════════════════════════════════════════════════════

void Daemon::handleRotate(bool clockwise)
{
    if (m_rotateDebounce.isValid() && m_rotateDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_rotateDebounce.restart();

    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "Rotate shortcut: no screen info";
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->rotateWindows(clockwise, screenId);
    } else if (m_windowTrackingAdaptor) {
        // Route through WTA for daemon-driven geometry computation + applyGeometriesBatch
        m_windowTrackingAdaptor->rotateWindowsInLayout(clockwise, screenId);
    }
}

void Daemon::handleFloat()
{
    // toggleWindowFloat() is fully daemon-local — reads the active window
    // from the shadow, reads fresh frame geometry from the shadow, and
    // dispatches to the engine in-process. The prior 100ms debounce was
    // there because the old path bounced through D-Bus 4 times, where rapid
    // retries could double-fire. A direct call doesn't need it; rapid
    // Meta-F presses should act as rapid toggles, not be silently dropped.
    if (!m_windowTrackingAdaptor) {
        return;
    }
    m_windowTrackingAdaptor->toggleWindowFloat();
}

void Daemon::handleMove(NavigationDirection direction)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->swapFocusedInDirection(dirStr);
    } else if (m_windowTrackingAdaptor) {
        // Route through WTA for daemon-driven geometry computation + applyGeometryRequested
        m_windowTrackingAdaptor->moveWindowToAdjacentZone(dirStr);
    }
}

void Daemon::handleFocus(NavigationDirection direction)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown focus navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->focusInDirection(dirStr, QStringLiteral("focus"));
    } else if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->focusAdjacentZone(dirStr);
    }
}

void Daemon::handlePush()
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "PushToEmptyZone shortcut: no screen info";
        return;
    }
    if (isAutotileScreen(screenId)) {
        return;
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->pushToEmptyZone(screenId);
    }
}

void Daemon::handleRestore()
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        return;
    }
    if (isAutotileScreen(screenId)) {
        // In autotile mode, "restore" floats the window (equivalent to unsnapping)
        m_autotileEngine->toggleFocusedWindowFloat();
        return;
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->restoreWindowSize();
    }
}

void Daemon::handleSwap(NavigationDirection direction)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->swapInDirection(dirStr, QStringLiteral("swap"));
    } else if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->swapWindowWithAdjacentZone(dirStr);
    }
}

void Daemon::handleSnap(int zoneNumber)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "SnapToZone shortcut: no screen info";
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->moveToPosition(QString(), zoneNumber, screenId);
    } else if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->snapToZoneByNumber(zoneNumber, screenId);
    }
}

void Daemon::handleCycle(bool forward)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        return;
    }
    if (isAutotileScreen(screenId)) {
        QString dirStr = forward ? QStringLiteral("right") : QStringLiteral("left");
        m_autotileEngine->focusInDirection(dirStr, QStringLiteral("cycle"));
    } else if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->cycleWindowsInZone(forward);
    }
}

void Daemon::handleResnap()
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->retile(screenId);
    } else if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->resnapToNewLayout();
    }
}

void Daemon::handleSnapAll()
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "SnapAllWindows shortcut: no screen info";
        return;
    }
    if (isAutotileScreen(screenId)) {
        m_autotileEngine->retile(screenId);
    } else if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->snapAllWindows(screenId);
    }
}

// DRY macro invocations for identical autotile-only handlers
HANDLE_AUTOTILE_ONLY(FocusMaster, focusMaster())
HANDLE_AUTOTILE_ONLY(SwapWithMaster, swapFocusedWithMaster())
void Daemon::handleIncreaseMasterRatio()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled())
        return;
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty() || !isAutotileScreen(screenId))
        return;
    if (isContextDisabled(m_settings.get(), screenId, currentDesktop(), currentActivity()))
        return;
    const qreal step = m_autotileEngine->effectiveSplitRatioStep(screenId);
    m_autotileEngine->increaseMasterRatio(step);
}

void Daemon::handleDecreaseMasterRatio()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled())
        return;
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty() || !isAutotileScreen(screenId))
        return;
    if (isContextDisabled(m_settings.get(), screenId, currentDesktop(), currentActivity()))
        return;
    const qreal step = m_autotileEngine->effectiveSplitRatioStep(screenId);
    m_autotileEngine->decreaseMasterRatio(step);
}
HANDLE_AUTOTILE_ONLY(IncreaseMasterCount, increaseMasterCount())
HANDLE_AUTOTILE_ONLY(DecreaseMasterCount, decreaseMasterCount())

void Daemon::handleRetile()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->retile();
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty() && !m_autotileEngine->autotileScreens().isEmpty()) {
            // QSet iteration order is non-deterministic; sort to get a stable fallback
            QStringList sorted = m_autotileEngine->autotileScreens().values();
            sorted.sort();
            screenId = sorted.first();
        }
        m_overlayService->showNavigationOsd(true, QStringLiteral("retile"), QStringLiteral("retiled"), QString(),
                                            QString(), screenId);
    }
}

void Daemon::resnapIfManualMode()
{
    if (!m_snapEngine) {
        return;
    }
    // Only skip resnap when the current screen is in autotile mode.
    // Per-desktop assignments mean some screens can be autotile while
    // others are manual — a global check would block manual resnaps.
    if (m_autotileEngine && m_unifiedLayoutController) {
        const QString screenId = m_unifiedLayoutController->currentScreenName();
        if (screenId.isEmpty()) {
            return; // No screen context — can't determine mode, skip resnap
        }
        if (m_autotileEngine->isAutotileScreen(screenId)) {
            return; // This screen is autotile — engine handles retile
        }
    }
    // Populate the resnap buffer before resnapping. UnifiedLayoutController::applyEntry()
    // blocks activeLayoutChanged (QSignalBlocker) to prevent whole-screen recalculation,
    // which also prevents onLayoutChanged() from populating the resnap buffer.
    // Additionally, when the global active layout is already the target (e.g. second
    // screen cycling to the same layout), setActiveLayout is a no-op and no signal fires.
    // Explicitly populating here mirrors the KCM's assignmentChangesApplied path.
    if (m_windowTrackingAdaptor) {
        QSet<QString> autotileScreens;
        if (m_autotileEngine) {
            autotileScreens = m_autotileEngine->autotileScreens();
        }
        m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(autotileScreens);
    }
    m_suppressResnapOsd = 1;
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->resnapToNewLayout();
    }
}

void Daemon::handleSwapVirtualScreen(NavigationDirection direction)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "SwapVirtualScreen shortcut: no screen info";
        return;
    }
    // VS swap is a monitor-scope action, so the OSD should render on the
    // physical monitor's full geometry rather than inside one virtual screen.
    const QString physId =
        VirtualScreenId::isVirtual(screenId) ? VirtualScreenId::extractPhysicalId(screenId) : screenId;

    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "SwapVirtualScreen: unknown direction" << static_cast<int>(direction);
        return;
    }

    // Run the swap through the daemon-held swapper. The Result enum carries
    // the rejection reason directly, so the OSD can show a specific failure
    // (no_subdivision, no_sibling, …) instead of echoing the raw direction.
    Q_ASSERT(m_virtualScreenSwapper);
    const auto result = m_virtualScreenSwapper->swapInDirection(screenId, dirStr);
    const bool ok = (result == VirtualScreenSwapper::Result::Ok);
    qCInfo(lcDaemon) << "SwapVirtualScreen:" << screenId << dirStr << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // On success, surface the direction (the OSD style needs a string to
        // render the arrow). On failure, surface the structured reason.
        const QString osdReason = ok ? dirStr : VirtualScreenSwapper::reasonString(result);
        m_overlayService->showNavigationOsd(ok, QStringLiteral("swap_vs"), osdReason, QString(), QString(), physId);
    }
}

void Daemon::handleRotateVirtualScreens(bool clockwise)
{
    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "RotateVirtualScreens shortcut: no screen info";
        return;
    }
    const QString physId =
        VirtualScreenId::isVirtual(screenId) ? VirtualScreenId::extractPhysicalId(screenId) : screenId;

    Q_ASSERT(m_virtualScreenSwapper);
    const auto result = m_virtualScreenSwapper->rotate(physId, clockwise);
    const bool ok = (result == VirtualScreenSwapper::Result::Ok);
    qCInfo(lcDaemon) << "RotateVirtualScreens:" << physId << "cw=" << clockwise << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // VS rotate is a monitor-scope action — show the OSD on the physical
        // monitor, not inside whichever VS held focus. On success surface the
        // rotation direction; on failure surface the structured reason so the
        // user sees "no_subdivision" instead of an ambiguous "clockwise" fail.
        const QString osdReason = ok ? (clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                                     : VirtualScreenSwapper::reasonString(result);
        m_overlayService->showNavigationOsd(ok, QStringLiteral("rotate_vs"), osdReason, QString(), QString(), physId);
    }
}

} // namespace PlasmaZones
