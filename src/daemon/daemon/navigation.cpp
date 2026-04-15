// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../../config/settings.h"
#include "../../core/inavigationactions.h"
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
// Navigation handlers — single code path per operation, dispatched through
// ScreenModeRouter::navigatorFor() so there's no mode-branching in the daemon
// itself. Each handler resolves the target screen, looks up the engine's
// INavigationActions adapter, and forwards the user-intent call. Autotile
// vs. snap-specific behaviour lives inside each adapter.
// ═══════════════════════════════════════════════════════════════════════════════

// Local helper: resolve the active screen and fetch the navigation adapter
// for it. Returns nullptr if either step fails. Centralises the "no screen
// info" early return so individual handlers stay short.
static INavigationActions* navigatorForShortcut(ScreenModeRouter* router, WindowTrackingAdaptor* wta,
                                                QString& outScreenId, const char* shortcutName)
{
    outScreenId = resolveShortcutScreenId(wta);
    if (outScreenId.isEmpty()) {
        qCDebug(lcDaemon) << shortcutName << "shortcut: no screen info";
        return nullptr;
    }
    if (!router) {
        return nullptr;
    }
    return router->navigatorFor(outScreenId);
}

void Daemon::handleRotate(bool clockwise)
{
    if (m_rotateDebounce.isValid() && m_rotateDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_rotateDebounce.restart();

    QString screenId;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Rotate")) {
        nav->rotateWindows(clockwise, screenId);
    }
}

void Daemon::handleFloat()
{
    // Float toggles the active window regardless of which screen it's on.
    // We could look up the screen and dispatch through the router, but the
    // active window's screen is whatever the shadow reports — and the
    // navigator call would resolve the same answer. The router dispatch is
    // still preferable to a direct WTA call because it routes through the
    // adapter layer so each engine owns its own "toggle float" semantics.
    QString screenId;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Float")) {
        nav->toggleFocusedFloat(screenId);
    }
}

void Daemon::handleMove(NavigationDirection direction)
{
    QString screenId;
    auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Move");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
        return;
    }
    nav->moveFocusedInDirection(dirStr, screenId);
}

void Daemon::handleFocus(NavigationDirection direction)
{
    QString screenId;
    auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Focus");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown focus navigation direction:" << static_cast<int>(direction);
        return;
    }
    nav->focusInDirection(dirStr, screenId);
}

void Daemon::handlePush()
{
    QString screenId;
    if (auto* nav =
            navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "PushToEmptyZone")) {
        // Autotile adapter's impl is a deliberate no-op — empty zones don't
        // exist in autotile mode — so this shortcut is harmlessly absorbed
        // on autotile screens instead of the daemon branching at entry.
        nav->pushToEmptyZone(screenId);
    }
}

void Daemon::handleRestore()
{
    QString screenId;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Restore")) {
        // Autotile: toggle float (restore out of layout).
        // Snap: restore to captured pre-snap geometry.
        nav->restoreFocusedWindow(screenId);
    }
}

void Daemon::handleSwap(NavigationDirection direction)
{
    QString screenId;
    auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Swap");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
        return;
    }
    nav->swapFocusedInDirection(dirStr, screenId);
}

void Daemon::handleSnap(int zoneNumber)
{
    QString screenId;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "SnapToZone")) {
        nav->moveFocusedToPosition(zoneNumber, screenId);
    }
}

void Daemon::handleCycle(bool forward)
{
    QString screenId;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Cycle")) {
        nav->cycleFocus(forward, screenId);
    }
}

void Daemon::handleResnap()
{
    QString screenId;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "Resnap")) {
        nav->reapplyLayout(screenId);
    }
}

void Daemon::handleSnapAll()
{
    QString screenId;
    if (auto* nav =
            navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, screenId, "SnapAllWindows")) {
        nav->snapAllWindows(screenId);
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
    if (m_virtualScreenDebounce.isValid() && m_virtualScreenDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_virtualScreenDebounce.restart();

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
    // m_virtualScreenSwapper is constructed in Daemon::init() before any
    // shortcut signals are wired, so it's always non-null on this path —
    // no per-call assertion needed.
    const auto result = m_virtualScreenSwapper->swapInDirection(screenId, dirStr);
    const bool ok = (result == VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "SwapVirtualScreen:" << screenId << dirStr << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // On success, surface the direction (the OSD style needs a string to
        // render the arrow). On failure, surface the structured reason.
        const QString osdReason = ok ? dirStr : VirtualScreenSwapper::reasonString(result);
        m_overlayService->showNavigationOsd(ok, QStringLiteral("swap_vs"), osdReason, QString(), QString(), physId);
    }
}

void Daemon::handleRotateVirtualScreens(bool clockwise)
{
    if (m_virtualScreenDebounce.isValid() && m_virtualScreenDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_virtualScreenDebounce.restart();

    const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "RotateVirtualScreens shortcut: no screen info";
        return;
    }
    const QString physId =
        VirtualScreenId::isVirtual(screenId) ? VirtualScreenId::extractPhysicalId(screenId) : screenId;

    // Swapper is always non-null on the shortcut path — see matching
    // comment in handleSwapVirtualScreen above.
    const auto result = m_virtualScreenSwapper->rotate(physId, clockwise);
    const bool ok = (result == VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "RotateVirtualScreens:" << physId << "cw=" << clockwise << "->" << static_cast<int>(result);

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
