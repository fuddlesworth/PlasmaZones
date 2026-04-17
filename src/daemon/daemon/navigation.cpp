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
#include <PhosphorTiles/AlgorithmRegistry.h>
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

// Local helper: build the navigation context for a shortcut handler.
// Resolves the active screen and active window from WTA's compositor-layer
// shadow, then fetches the navigation adapter for that screen from the
// router. Returns nullptr if either step fails. Centralises the "no screen
// info" early return and the context population so individual handlers
// stay short and all shortcut dispatches use the same resolution logic.
static INavigationActions* navigatorForShortcut(ScreenModeRouter* router, WindowTrackingAdaptor* wta,
                                                NavigationContext& outCtx, const char* shortcutName)
{
    outCtx.screenId = resolveShortcutScreenId(wta);
    if (outCtx.screenId.isEmpty()) {
        qCDebug(lcDaemon) << shortcutName << "shortcut: no screen info";
        return nullptr;
    }
    if (wta) {
        outCtx.windowId = wta->lastActiveWindowId();
    }
    if (!router) {
        return nullptr;
    }
    return router->navigatorFor(outCtx.screenId);
}

void Daemon::handleRotate(bool clockwise)
{
    if (m_rotateDebounce.isValid() && m_rotateDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_rotateDebounce.restart();

    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Rotate")) {
        nav->rotateWindows(clockwise, ctx);
    }
}

void Daemon::handleFloat()
{
    // Float toggles the active window regardless of which screen it's on.
    // The navigatorForShortcut helper pulls both windowId and screenId
    // from the WTA shadow, so the engine call is fully resolved without
    // reaching back into WTA state.
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Float")) {
        nav->toggleFocusedFloat(ctx);
    }
}

void Daemon::handleMove(NavigationDirection direction)
{
    NavigationContext ctx;
    auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Move");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
        return;
    }
    nav->moveFocusedInDirection(dirStr, ctx);
}

void Daemon::handleFocus(NavigationDirection direction)
{
    NavigationContext ctx;
    auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Focus");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown focus navigation direction:" << static_cast<int>(direction);
        return;
    }
    nav->focusInDirection(dirStr, ctx);
}

void Daemon::handlePush()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "PushToEmptyZone")) {
        // Autotile adapter's impl is a deliberate no-op — empty zones don't
        // exist in autotile mode — so this shortcut is harmlessly absorbed
        // on autotile screens instead of the daemon branching at entry.
        nav->pushToEmptyZone(ctx);
    }
}

void Daemon::handleRestore()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Restore")) {
        // Autotile: toggle float (restore out of layout).
        // Snap: restore to captured pre-snap geometry.
        nav->restoreFocusedWindow(ctx);
    }
}

void Daemon::handleSwap(NavigationDirection direction)
{
    NavigationContext ctx;
    auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Swap");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
        return;
    }
    nav->swapFocusedInDirection(dirStr, ctx);
}

void Daemon::handleSnap(int zoneNumber)
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "SnapToZone")) {
        nav->moveFocusedToPosition(zoneNumber, ctx);
    }
}

void Daemon::handleCycle(bool forward)
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Cycle")) {
        nav->cycleFocus(forward, ctx);
    }
}

void Daemon::handleResnap()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "Resnap")) {
        nav->reapplyLayout(ctx);
    }
}

void Daemon::handleSnapAll()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, ctx, "SnapAllWindows")) {
        nav->snapAllWindows(ctx);
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
