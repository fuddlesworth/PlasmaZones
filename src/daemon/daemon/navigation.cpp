// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../../config/settings.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/screenmoderouter.h"
#include "../../core/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "../../dbus/snapadaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../modetracker.h"
#include <QScreen>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
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

PhosphorZones::AssignmentEntry::Mode Daemon::currentModeFor(const QString& screenId) const
{
    if (m_screenModeRouter) {
        return m_screenModeRouter->modeFor(screenId);
    }
    // Same Snapping fallback DaemonScreenModeAdapter applies — see
    // contextresolverwiring.cpp.
    return PhosphorZones::AssignmentEntry::Snapping;
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
                                                              PhosphorScreens::ScreenManager* screenManager,
                                                              PhosphorEngine::NavigationContext& outCtx,
                                                              const char* shortcutName)
{
    outCtx.screenId = resolveShortcutScreenId(screenManager, wta);
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

// Local helper: per-context disable cascade gate for navigation shortcuts
// that commit window-geometry side effects on the focused screen.
//
// Returns true when the handler should silently no-op (resolver null
// during the daemon's shutdown window, or the focused (monitor, desktop,
// activity) is on the user's disable list). Mirrors the inline check
// every gated handler used to carry. Centralising it makes the bug class
// from discussion #461 — "shortcut still fires on a disabled context" —
// a single line per handler to opt into.
//
// Handlers that only manipulate focus (handleFocus / handleCycle) do NOT
// use this gate because focus changes are not a geometry side effect and
// the user expects them to keep working on a "disabled" context.
bool Daemon::isFocusedContextGated(const QString& screenId) const
{
    return !m_contextResolver || m_contextResolver->isDisabled(m_contextResolver->handleFor(screenId));
}

bool Daemon::isFocusedContextGatedForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const
{
    return !m_contextResolver || m_contextResolver->isDisabled(m_contextResolver->handleForMode(screenId, mode));
}

void Daemon::handleRotate(bool clockwise)
{
    if (m_rotateDebounce.isValid() && m_rotateDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_rotateDebounce.restart();

    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "Rotate")) {
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        nav->rotateWindows(clockwise, ctx);
    }
}

void Daemon::handleFloat()
{
    // Debounce keyboard auto-repeat — float toggling kicks unsnap →
    // float → resnap on un-float, each of which is a real geometry
    // commit. Mirrors the rotate handler's pile-up guard.
    if (m_floatDebounce.isValid() && m_floatDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }
    m_floatDebounce.restart();

    // Float toggles the active window regardless of which screen it's on.
    // The navigatorForShortcut helper pulls both windowId and screenId
    // from the WTA shadow, so the engine call is fully resolved without
    // reaching back into WTA state.
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "Float")) {
        // Honor the per-context disable lists, same as handleSnap. Un-floating
        // a window re-runs commitSnap; without this gate that re-snaps the
        // window on a monitor / desktop / activity the user disabled
        // (discussion #461 — observed re-snapping on a disabled desktop).
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        nav->toggleFocusedFloat(ctx);
    }
}

void Daemon::handleMove(NavigationDirection direction)
{
    NavigationContext ctx;
    auto* nav =
        navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx, "Move");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (isFocusedContextGated(ctx.screenId)) {
        return;
    }
    nav->moveFocusedInDirection(dirStr, ctx);
}

void Daemon::handleFocus(NavigationDirection direction)
{
    NavigationContext ctx;
    auto* nav =
        navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx, "Focus");
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
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "PushToEmptyZone")) {
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        // Autotile adapter's impl is a deliberate no-op — empty zones don't
        // exist in autotile mode — so this shortcut is harmlessly absorbed
        // on autotile screens instead of the daemon branching at entry.
        nav->pushToEmptyZone(ctx);
    }
}

void Daemon::handleRestore()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "Restore")) {
        // Autotile: toggle float (restore out of layout).
        // Snap: restore to captured pre-snap geometry.
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        nav->restoreFocusedWindow(ctx);
    }
}

void Daemon::handleSwap(NavigationDirection direction)
{
    NavigationContext ctx;
    auto* nav =
        navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx, "Swap");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (isFocusedContextGated(ctx.screenId)) {
        return;
    }
    nav->swapFocusedInDirection(dirStr, ctx);
}

void Daemon::handleSnap(int zoneNumber)
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "SnapToZone")) {
        // Honor the per-context disable lists. engineFor() routes purely on
        // mode and never consults them, so a keyboard snap-to-zone would
        // otherwise place a window on a monitor / desktop / activity the user
        // disabled (discussion #461).
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        nav->moveFocusedToPosition(zoneNumber, ctx);
    }
}

void Daemon::handleCycle(bool forward)
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "Cycle")) {
        nav->cycleFocus(forward, ctx);
    }
}

void Daemon::handleResnap()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "Resnap")) {
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        nav->reapplyLayout(ctx);
    }
}

void Daemon::handleSnapAll()
{
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "SnapAllWindows")) {
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
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
    if (isFocusedContextGatedForMode(screenId, PhosphorZones::AssignmentEntry::Autotile))
        return;
    // Set the engine's active-screen hint before the parameterless engine
    // call — the engine's NavigationController resolves the target screen
    // from `m_activeScreen`, falling back to the first entry of
    // `m_autotileScreens` (hash-ordered) when the hint is unset. Without
    // the hint, a Meta+Plus on screen B with B's last focus event stale
    // would silently bump screen A's master ratio. The
    // HANDLE_AUTOTILE_ONLY macro sets this hint for every other autotile
    // shortcut; these two handlers exist out-of-line only to thread the
    // per-screen `effectiveSplitRatioStep`, so they must replicate the
    // hint-setting the macro does.
    m_autotileEngine->setActiveScreenHint(screenId);
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
    if (isFocusedContextGatedForMode(screenId, PhosphorZones::AssignmentEntry::Autotile))
        return;
    // See handleIncreaseMasterRatio for the active-screen-hint rationale.
    m_autotileEngine->setActiveScreenHint(screenId);
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
    // Mirror every sister handler (HANDLE_AUTOTILE_ONLY at macros.h:29 and
    // the master-ratio handlers): silently no-op when the focused screen
    // isn't in autotile mode OR when its autotile-mode disable cascade
    // trips. retile() itself is engine-global, but a user firing the
    // shortcut from a Snapping/Scrolling screen (or from an
    // autotile-disabled context) expects "nothing happens on the screen
    // I'm focused on", not "every other autotile screen retiles". Fail
    // closed on a null resolver — matches the rest of this file
    // (handleSnap, handleFloat, master-ratio handlers); the resolver is
    // null only inside the tiny shutdown window where every navigation
    // handler should be silently inert anyway. A null focused screen
    // (no resolvable focus) is treated the same as the macro at
    // macros.h:29 does: silent no-op, NOT a fallthrough to the legacy
    // engine-global retile. Without this symmetry, a user with no
    // focused window — e.g. all windows minimised, or focus lost mid-
    // session — would trigger a global retile across every autotile
    // screen, ignoring the per-screen disable cascade.
    const QString focusedScreen = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (focusedScreen.isEmpty()) {
        return;
    }
    if (!isAutotileScreen(focusedScreen)) {
        return;
    }
    if (isFocusedContextGatedForMode(focusedScreen, PhosphorZones::AssignmentEntry::Autotile)) {
        return;
    }
    m_autotileEngine->retile();
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // focusedScreen is guaranteed non-empty here — the early-return
        // above the retile call rejects the empty case.
        m_overlayService->showNavigationOsd(true, QStringLiteral("retile"), QStringLiteral("retiled"), QString(),
                                            QString(), focusedScreen);
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
        // Use the `Daemon::currentDesktop()` helper (defined in
        // osd.cpp) for the null-safe VDM read — the same pattern used
        // by every daemon-side site that needs the current desktop
        // (autotile.cpp, signals.cpp, osd.cpp, start.cpp).
        m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(autotileScreens, {}, currentDesktop());
    }
    // Co-locate the suppress pre-arm with the resnap call so a null
    // m_snapAdaptor doesn't leave the counter armed for the next
    // unrelated navigationFeedback. Mirrors the daemon.cpp:1249 site.
    if (m_snapAdaptor) {
        m_suppressResnapOsd = 1;
        m_snapAdaptor->resnapToNewLayout();
    }
    // Restore snap-float positions for windows the picker/cycle just released
    // from autotile — the resnap above (buffer-based) cannot cover floating
    // windows. See the helper for why only the float half is emitted here.
    emitPendingSnapFloatRestoresForResnapBuffer();
}

void Daemon::emitPendingSnapFloatRestoresForResnapBuffer()
{
    if (m_pendingSnapFloatRestores.isEmpty()) {
        return;
    }
    QVector<ZoneAssignmentEntry> floatEntries;
    for (const ZoneAssignmentEntry& e : std::as_const(m_pendingSnapFloatRestores)) {
        if (e.targetZoneId == RestoreSentinel) {
            floatEntries.append(e);
        }
    }
    // Consume the whole buffer: the float entries are emitted below; the
    // snap-ZONE entries are deliberately handed to the in-flight
    // resnapToNewLayout (new-layout zones), not re-applied here against the
    // old layout. Clearing prevents them leaking into the next windowsReleased.
    m_pendingSnapFloatRestores.clear();
    if (floatEntries.isEmpty() || !m_snapEngine) {
        return;
    }
    if (auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get())) {
        ++m_suppressResnapOsd; // the batched emit drives an additional resnap feedback
        concreteSnap->emitBatchedResnap(floatEntries);
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
    const bool ok = (result == PhosphorScreens::VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "SwapVirtualScreen:" << screenId << dirStr << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // On success, surface the direction (the OSD style needs a string to
        // render the arrow). On failure, surface the structured reason.
        const QString osdReason = ok ? dirStr : PhosphorScreens::VirtualScreenSwapper::reasonString(result);
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
    const bool ok = (result == PhosphorScreens::VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "RotateVirtualScreens:" << physId << "cw=" << clockwise << "->" << static_cast<int>(result);

    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        // VS rotate is a monitor-scope action — show the OSD on the physical
        // monitor, not inside whichever VS held focus. On success surface the
        // rotation direction; on failure surface the structured reason so the
        // user sees "no_subdivision" instead of an ambiguous "clockwise" fail.
        const QString osdReason = ok ? (clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                                     : PhosphorScreens::VirtualScreenSwapper::reasonString(result);
        m_overlayService->showNavigationOsd(ok, QStringLiteral("rotate_vs"), osdReason, QString(), QString(), physId);
    }
}

} // namespace PlasmaZones
