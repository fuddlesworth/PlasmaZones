// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../../config/settings.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/IScrollNavigation.h>
#include "../../core/logging.h"
#include "../../core/screenmoderouter.h"
#include "../../core/settings_interfaces.h"
#include "../../core/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/Swapper.h>
#include "../../dbus/snapadaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../modetracker.h"
#include <QScreen>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Engine routing
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IPlacementEngine* Daemon::engineForScreen(const QString& screenId) const
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
    return m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation handlers — single code path per operation, dispatched through
// ScreenModeRouter::engineFor() so there's no mode-branching in the daemon
// itself. Each handler resolves the target screen, looks up the engine's
// IPlacementEngine, and forwards the user-intent call. Autotile
// vs. snap-specific behaviour lives inside each engine's override.
// ═══════════════════════════════════════════════════════════════════════════════

// Local helper: build the navigation context for a shortcut handler.
// Resolves the active screen and active window from WTA's compositor-layer
// shadow, then fetches the IPlacementEngine for that screen from the
// router. Returns nullptr if either step fails. Centralises the "no screen
// info" early return and the context population so individual handlers
// stay short and all shortcut dispatches use the same resolution logic.
static PhosphorEngine::IPlacementEngine* navigatorForShortcut(ScreenModeRouter* router, WindowTrackingAdaptor* wta,
                                                              PhosphorEngine::NavigationContext& outCtx,
                                                              const char* shortcutName)
{
    outCtx.screenId = resolveShortcutScreenId(wta && wta->service() ? wta->service()->screenManager() : nullptr, wta);
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
    PhosphorEngine::IPlacementEngine* nav = router->engineFor(outCtx.screenId);
    // Global feature-toggle gate. engineFor() routes purely on the screen's
    // layout mode; it does not consult whether that mode's master toggle is
    // on. isEnabled() reports the effective on/off state for both engines
    // (SnapEngine → snappingEnabled, AutotileEngine → any-screen autotiling),
    // so one check here suppresses every snap- and autotile-mode shortcut
    // when its feature is globally disabled — the keyboard-nav counterpart of
    // the auto-snap-on-open kill-switch in SnapEngine::resolveWindowRestore.
    if (nav && !nav->isEnabled()) {
        qCDebug(lcDaemon) << shortcutName << "shortcut: ignored — engine disabled for screen" << outCtx.screenId;
        return nullptr;
    }
    return nav;
}

// Local helper: resolve the shortcut's target engine and narrow it to the
// IScrollNavigation interface. Returns nullptr when the focused screen is not
// a scroll screen — the snap and autotile engines do not implement that
// interface, so the cross-cast fails and the niri-style op is simply skipped.
//
// Layered gating, in this order:
//   1. navigatorForShortcut() resolves the engine and applies the global
//      isEnabled() check — when scroll mode is globally off, this returns
//      nullptr and we never reach the per-context check below. (That global
//      gate is also why handleToggleCenterFocusedColumn is the one scroll
//      shortcut not routed through this helper: the toggle predates having
//      a focused column and must work even from a non-scroll context, so it
//      runs its OWN context check on the resolved screenId.)
//   2. The cross-cast to IScrollNavigation skips snap and autotile engines —
//      a non-scroll screen returns nullptr without consulting settings.
//   3. The per-context disable list (per-monitor / per-desktop / per-activity)
//      for AssignmentEntry::Scroll, mirroring handleSnap and handleFloat.
//      CLAUDE.md: "isContextDisabled gates per-mode disable lists in EVERY
//      shortcut handler" — without this, a user who disabled scroll mode on
//      a specific desktop or monitor (#461 pattern) could still trigger the
//      niri column ops via shortcut because navigatorForShortcut routes by
//      mode and IPlacementEngine::isEnabled() reports global, not per-context.
//
// The settings* arg is the daemon's m_settings pointer (or nullptr in tests).
// When null, the disable check is skipped — matches navigatorForShortcut's
// fail-open behaviour for incomplete daemon construction.
static PhosphorEngine::IScrollNavigation* scrollNavigatorForShortcut(ScreenModeRouter* router,
                                                                     WindowTrackingAdaptor* wta, ISettings* settings,
                                                                     int currentDesktop, const QString& currentActivity,
                                                                     PhosphorEngine::NavigationContext& outCtx,
                                                                     const char* shortcutName)
{
    auto* nav =
        dynamic_cast<PhosphorEngine::IScrollNavigation*>(navigatorForShortcut(router, wta, outCtx, shortcutName));
    if (!nav) {
        return nullptr;
    }
    if (settings
        && isContextDisabled(settings, PhosphorZones::AssignmentEntry::Scroll, outCtx.screenId, currentDesktop,
                             currentActivity)) {
        qCDebug(lcDaemon) << shortcutName << "shortcut: ignored — scroll disabled for context" << outCtx.screenId
                          << "desktop" << currentDesktop << "activity" << currentActivity;
        return nullptr;
    }
    return nav;
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
        // Honor the per-context disable lists, same as handleSnap. Un-floating
        // a window re-runs commitSnap; without this gate that re-snaps the
        // window on a monitor / desktop / activity the user disabled
        // (discussion #461 — observed re-snapping on a disabled desktop).
        const auto mode =
            m_screenModeRouter ? m_screenModeRouter->modeFor(ctx.screenId) : PhosphorZones::AssignmentEntry::Snapping;
        if (isContextDisabled(m_settings.get(), mode, ctx.screenId, currentDesktop(), currentActivity())) {
            return;
        }
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

void Daemon::handleConsume()
{
    // niri "consume": pull the next column's focused window into the focused
    // column. Meaningful only in scroll mode — scrollNavigatorForShortcut
    // yields nullptr on snap/autotile screens (they do not implement
    // IScrollNavigation), so the shortcut is simply skipped there.
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "ConsumeWindow")) {
        nav->consumeWindowIntoColumn(ctx);
    }
}

void Daemon::handleExpel()
{
    // niri "expel": push the focused window out into its own new column.
    // Scroll-mode only; skipped on snap/autotile screens (see handleConsume).
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "ExpelWindow")) {
        nav->expelWindowFromColumn(ctx);
    }
}

void Daemon::handleCycleColumnWidth()
{
    // Cycle the focused column through the width presets. Scroll-mode only;
    // skipped on snap/autotile screens (see handleConsume).
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "CycleColumnWidth")) {
        nav->cyclePresetColumnWidth(ctx);
    }
}

void Daemon::handleCycleWindowHeight()
{
    // Cycle the focused window through the height presets. Scroll-mode only;
    // skipped on snap/autotile screens (see handleConsume).
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "CycleWindowHeight")) {
        nav->cyclePresetWindowHeight(ctx);
    }
}

void Daemon::handleToggleColumnFullWidth()
{
    // Toggle the focused column between full viewport width and its prior
    // width. Scroll-mode only; skipped on snap/autotile screens (see
    // handleConsume).
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "ToggleColumnFullWidth")) {
        nav->toggleColumnFullWidth(ctx);
    }
}

namespace {
/// Fraction of the working-area width a grow/shrink shortcut moves the
/// focused column.
constexpr qreal kColumnWidthStep = 0.1;
} // namespace

void Daemon::handleGrowColumnWidth()
{
    // Grow the focused column by one width step. Scroll-mode only; skipped on
    // snap/autotile screens (see handleConsume).
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "GrowColumnWidth")) {
        nav->adjustColumnWidth(kColumnWidthStep, ctx);
    }
}

void Daemon::handleShrinkColumnWidth()
{
    // Shrink the focused column by one width step. Scroll-mode only; skipped
    // on snap/autotile screens (see handleConsume).
    NavigationContext ctx;
    if (auto* nav = scrollNavigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_settings.get(),
                                               currentDesktop(), currentActivity(), ctx, "ShrinkColumnWidth")) {
        nav->adjustColumnWidth(-kColumnWidthStep, ctx);
    }
}

void Daemon::handleToggleCenterFocusedColumn()
{
    // Scroll mode: toggle the persisted viewport-centering setting. Phase 5
    // makes the centering mode a setting, so the shortcut flips it — the
    // change signal re-pushes it to ScrollEngine and re-resolves every scroll
    // strip via refreshScrollConfigFromSettings().
    //
    // The focused screen may carry a per-screen CenterFocusedColumn override,
    // which shadows the global value (ScrollEngine::effectiveViewportMode).
    // Flipping the global on such a screen would be a silent no-op, so the
    // shortcut targets whichever level is actually in effect for that screen:
    // the per-screen override when one exists, the global setting otherwise.
    if (!m_settings) {
        return;
    }
    const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    // No resolvable focused screen → no scroll target → whole shortcut no-ops.
    // Mirrors the other scroll-only shortcut handlers (which bail out when
    // scrollNavigatorForShortcut returns nullptr). Without this guard the
    // global-toggle fall-through below silently mutates a scroll setting from
    // a context where no scroll target exists — most often a snap or autotile
    // session with no focused window — turning the keybind into an
    // out-of-mode setting changer.
    if (screenId.isEmpty()) {
        return;
    }
    // Honor the per-context disable lists, same as the other scroll handlers.
    // Without this gate the toggle would still fire on a desktop/monitor/
    // activity where the user has scroll mode disabled — silently mutating
    // global or per-screen scroll settings via shortcut on a context that
    // shouldn't accept scroll input.
    if (isContextDisabled(m_settings.get(), PhosphorZones::AssignmentEntry::Scroll, screenId, currentDesktop(),
                          currentActivity())) {
        return;
    }
    const QVariantMap overrides = m_settings->getPerScreenScrollSettings(screenId);
    const auto it = overrides.constFind(QLatin1String(PerScreenScrollKey::CenterFocusedColumn));
    if (it != overrides.constEnd()) {
        m_settings->setPerScreenScrollSetting(screenId, QLatin1String(PerScreenScrollKey::CenterFocusedColumn),
                                              !it.value().toBool());
        return;
    }
    m_settings->setScrollCenterFocusedColumn(!m_settings->scrollCenterFocusedColumn());
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
        // Honor the per-context disable lists. engineFor() routes purely on
        // mode and never consults them, so a keyboard snap-to-zone would
        // otherwise place a window on a monitor / desktop / activity the user
        // disabled (discussion #461). Take the screen's mode from the router
        // (the single source of truth) rather than inferring it from the
        // engine-id string, which a future third engine would misroute.
        const auto mode =
            m_screenModeRouter ? m_screenModeRouter->modeFor(ctx.screenId) : PhosphorZones::AssignmentEntry::Snapping;
        if (isContextDisabled(m_settings.get(), mode, ctx.screenId, currentDesktop(), currentActivity())) {
            return;
        }
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
    const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty() || !isAutotileScreen(screenId))
        return;
    if (isContextDisabled(m_settings.get(), PhosphorZones::AssignmentEntry::Autotile, screenId, currentDesktop(),
                          currentActivity()))
        return;
    const qreal step = m_autotileEngine->effectiveSplitRatioStep(screenId);
    m_autotileEngine->increaseMasterRatio(step);
}

void Daemon::handleDecreaseMasterRatio()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled())
        return;
    const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty() || !isAutotileScreen(screenId))
        return;
    if (isContextDisabled(m_settings.get(), PhosphorZones::AssignmentEntry::Autotile, screenId, currentDesktop(),
                          currentActivity()))
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
        QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty() && m_screenModeRouter) {
            QStringList autotile =
                m_screenModeRouter
                    ->partitionByMode(m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList{})
                    .autotile;
            autotile.sort();
            if (!autotile.isEmpty()) {
                screenId = autotile.first();
            }
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
        if (isAutotileScreen(screenId)) {
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
        if (m_screenModeRouter && m_screenManager) {
            const auto parts = m_screenModeRouter->partitionByMode(m_screenManager->effectiveScreenIds());
            autotileScreens = QSet<QString>(parts.autotile.begin(), parts.autotile.end());
        }
        // Restrict the resnap to the current virtual desktop. Cycle/picker /
        // zone-selector all change a single (screen, desktop, activity)
        // assignment — without the desktop filter the resnap would also
        // physically reposition windows on other desktops to the
        // just-cycled layout's zones, which the user perceives as
        // "every desktop got the same layout".
        const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(autotileScreens, {}, currentDesktop);
    }
    m_suppressResnapOsd = 1;
    if (m_snapAdaptor) {
        m_snapAdaptor->resnapToNewLayout();
    }
}

void Daemon::handleSwapVirtualScreen(NavigationDirection direction)
{
    if (m_virtualScreenDebounce.isValid() && m_virtualScreenDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_virtualScreenDebounce.restart();

    const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "SwapVirtualScreen shortcut: no screen info";
        return;
    }
    // VS swap is a monitor-scope action, so the OSD should render on the
    // physical monitor's full geometry rather than inside one virtual screen.
    const QString physId = PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
        : screenId;

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
    const bool ok = (result == Phosphor::Screens::VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "SwapVirtualScreen:" << screenId << dirStr << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // On success, surface the direction (the OSD style needs a string to
        // render the arrow). On failure, surface the structured reason.
        const QString osdReason = ok ? dirStr : Phosphor::Screens::VirtualScreenSwapper::reasonString(result);
        m_overlayService->showNavigationOsd(ok, QStringLiteral("swap_vs"), osdReason, QString(), QString(), physId);
    }
}

void Daemon::handleRotateVirtualScreens(bool clockwise)
{
    if (m_virtualScreenDebounce.isValid() && m_virtualScreenDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_virtualScreenDebounce.restart();

    const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "RotateVirtualScreens shortcut: no screen info";
        return;
    }
    const QString physId = PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
        : screenId;

    // Swapper is always non-null on the shortcut path — see matching
    // comment in handleSwapVirtualScreen above.
    const auto result = m_virtualScreenSwapper->rotate(physId, clockwise);
    const bool ok = (result == Phosphor::Screens::VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "RotateVirtualScreens:" << physId << "cw=" << clockwise << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // VS rotate is a monitor-scope action — show the OSD on the physical
        // monitor, not inside whichever VS held focus. On success surface the
        // rotation direction; on failure surface the structured reason so the
        // user sees "no_subdivision" instead of an ambiguous "clockwise" fail.
        const QString osdReason = ok ? (clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                                     : Phosphor::Screens::VirtualScreenSwapper::reasonString(result);
        m_overlayService->showNavigationOsd(ok, QStringLiteral("rotate_vs"), osdReason, QString(), QString(), physId);
    }
}

} // namespace PlasmaZones
