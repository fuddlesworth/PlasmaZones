// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"
#include "macros.h"
#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "config/settings.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
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

PhosphorEngine::IPlacementEngine::LayoutSupport Daemon::layoutSupportForScreen(const QString& screenId) const
{
    if (m_screenModeRouter) {
        if (const auto* engine = m_screenModeRouter->engineFor(screenId)) {
            return engine->layoutSupport();
        }
    }
    // Only the null-ROUTER case (the shutdown window) reaches this line:
    // engineFor's mode switch is exhaustive over ctor-checked engine
    // pointers and never returns nullptr for a routed screen (the inner
    // check above is cheap defence, not a contract). Fall back to
    // Placement — same Snapping fallback as currentModeFor, and snap's
    // layouts are placement layouts. Note the three null-router fallbacks
    // in this file deliberately differ: isAutotileScreen probes the live
    // engine, currentModeFor answers Snapping, this answers Placement —
    // each is the safe default for its own consumers.
    return PhosphorEngine::IPlacementEngine::LayoutSupport::Placement;
}

bool Daemon::dragInsertSelectorForScreen(const QString& screenId) const
{
    if (m_screenModeRouter) {
        if (const auto* engine = m_screenModeRouter->engineFor(screenId)) {
            return engine->providesDragInsertSelector();
        }
    }
    // Null-router shutdown window only (see layoutSupportForScreen above).
    // False is the safe default: the drag popup falls back to zone-layout
    // semantics rather than offering strip targets no engine will commit.
    return false;
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
    // on. isEnabled() reports the effective on/off state per engine
    // (SnapEngine → snappingEnabled, AutotileEngine → any-screen autotiling,
    // ScrollEngine → any scrolling screen exists, a set-membership fact
    // rather than a user toggle), so one check here suppresses every
    // mode shortcut whose feature is globally disabled — for snap and
    // autotile the keyboard-nav counterpart of the auto-snap-on-open
    // kill-switch in SnapEngine::resolveWindowRestore. A screen the router
    // resolves to scrolling is necessarily in the scroll engine's set, so
    // the gate can never wrongly refuse a scrolling shortcut.
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

    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "Rotate")) {
        if (isFocusedContextGated(ctx.screenId)) {
            return;
        }
        // Restart only on actual dispatch so a guard-rejected press doesn't
        // consume the window and swallow a valid press that follows within
        // it. Matches handleSpan.
        m_rotateDebounce.restart();
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
        // Restart only on actual dispatch — see handleSpan.
        m_floatDebounce.restart();
        // Dispatch log: performToggleFloat's "now floating" line is emitted
        // for shortcut and engine paths alike, so without this line a user
        // Meta+F is indistinguishable in the journal from an engine float.
        qCInfo(lcDaemon) << "handleFloat: toggling float for focused window" << ctx.windowId << "screen"
                         << ctx.screenId;
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

void Daemon::handleSpan(NavigationDirection direction)
{
    // Thin keyboard auto-repeat to one span step per debounce window.
    // Unlike move, span is not monotonic: once the grow direction hits the
    // layout boundary the same keypress shrinks the opposite edge, so
    // unthrottled repeat would grow to the edge and collapse the span at
    // full repeat rate. Full prevention would need press/repeat
    // discrimination the shortcut backend doesn't expose; the window slows
    // the walk enough for the user to release the key.
    //
    // One timer covers all four directions, so a deliberate reversal inside
    // the window (grow right, then grow left within kShortcutDebounceMs) is
    // dropped too. That is accepted: the reversal corrects a press whose
    // result the user has not seen yet, and per-direction timers would let
    // auto-repeat on one arrow chain past a boundary flip while the opposite
    // arrow was still settling. m_virtualScreenDebounce reasons about
    // alternation differently because its two ops are not each other's
    // inverse.
    if (m_spanDebounce.isValid() && m_spanDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }

    NavigationContext ctx;
    auto* nav =
        navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx, "Span");
    if (!nav) {
        return;
    }
    const QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown span navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (isFocusedContextGated(ctx.screenId)) {
        return;
    }
    // Restart only when the press actually dispatches: a press rejected by
    // the guards above must not consume the window and swallow a valid
    // press that follows within it.
    m_spanDebounce.restart();
    nav->spanFocusedInDirection(dirStr, ctx);
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
        // Off snapping, empty zones don't exist; both non-snap engines
        // answer with a "push"/"not_supported" feedback emit (the policy
        // the interface documents) instead of the daemon branching at entry.
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

void Daemon::handleSwitchFocusFloatTiling()
{
    // Mode-agnostic since all three engines implement the verb on the
    // shared resolver; the router picks the focused screen's engine. Snap
    // resolves its own state for exactly that screen; scroll and autotile
    // prefer it but may re-resolve (scroll to its active/first scrolling
    // screen for a non-scrolling id, autotile to the focused screen when
    // the named screen has no state).
    //
    // No isFocusedContextGated() call: this verb only moves focus (all
    // three engine bodies emit activation + feedback, no geometry), and
    // per the gate's contract focus-only handlers keep working on a
    // "disabled" context, matching handleFocus/handleCycle.
    NavigationContext ctx;
    if (auto* nav = navigatorForShortcut(m_screenModeRouter.get(), m_windowTrackingAdaptor, m_screenManager.get(), ctx,
                                         "SwitchFocusFloatTiling")) {
        nav->switchFocusBetweenFloatingAndTiling(ctx.screenId);
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
    // hint-setting the macro does — and, for the same reason, the
    // membership gate below it.
    //
    // isAutotileScreen above is the ROUTER's answer, which is Autotile for an
    // explicit algorithm opt-out too — but updateEngineScreens leaves such a
    // screen out of the engine set, the hint is only honored for a member, and
    // NavigationController would otherwise fall back to the first
    // (hash-ordered) entry of that set and move an unrelated screen's ratio.
    // Membership, not "has a TilingState": a member with no tiled windows yet
    // still gets the engine's own feedback instead of silence.
    if (!m_autotileEngine->isActiveOnScreen(screenId)) {
        return;
    }
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
    // See handleIncreaseMasterRatio for the active-screen-hint and
    // membership rationale.
    if (!m_autotileEngine->isActiveOnScreen(screenId)) {
        return;
    }
    m_autotileEngine->setActiveScreenHint(screenId);
    const qreal step = m_autotileEngine->effectiveSplitRatioStep(screenId);
    m_autotileEngine->decreaseMasterRatio(step);
}
HANDLE_AUTOTILE_ONLY(IncreaseMasterCount, increaseMasterCount())
HANDLE_AUTOTILE_ONLY(DecreaseMasterCount, decreaseMasterCount())

void Daemon::handleRetile()
{
    // Mode-neutral: the one Retile chord re-applies whichever layout the
    // focused screen is on. A null focused screen (no resolvable focus) is a
    // silent no-op in EVERY arm, NOT a fallthrough to the legacy engine-global
    // retile. Without this, a user with no focused window — all windows
    // minimised, or focus lost mid-session — would trigger a global retile
    // across every autotile screen, ignoring the per-screen disable cascade.
    const QString focusedScreen = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (focusedScreen.isEmpty()) {
        return;
    }
    // Scrolling arm: every column back to the context's default width and
    // display, every window back to the even split. Gated on the SCROLLING
    // cascade, the way the autotile arm below gates on its own, and ahead of
    // the autotile enabled check: a scrolling screen must re-flow whether or
    // not any autotile screen exists in the session. The engine's own verb
    // handles the empty-strip case (no_windows) and the OSD.
    if (currentModeFor(focusedScreen) == PhosphorZones::AssignmentEntry::Scrolling) {
        // The member is held as the engine base; the scrolling verbs live on
        // the concrete type, the same cast scrolling_init.cpp's resolver does.
        auto* scroll = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
        if (!scroll || isFocusedContextGatedForMode(focusedScreen, PhosphorZones::AssignmentEntry::Scrolling)) {
            return;
        }
        scroll->resetStripToDefaults(focusedScreen);
        return;
    }
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    // Mirror every sister handler (HANDLE_AUTOTILE_ONLY at macros.h:29 and
    // the master-ratio handlers): silently no-op when the focused screen
    // isn't in autotile mode OR when its autotile-mode disable cascade
    // trips. retile() itself is engine-global, but a user firing the
    // shortcut from a Snapping screen (or from an autotile-disabled
    // context) expects "nothing happens on the screen I'm focused on", not
    // "every other autotile screen retiles". Fail closed on a null resolver
    // — matches the rest of this file (handleSnap, handleFloat,
    // master-ratio handlers); the resolver is null only inside the tiny
    // shutdown window where every navigation handler should be silently
    // inert anyway.
    if (!isAutotileScreen(focusedScreen)) {
        return;
    }
    if (isFocusedContextGatedForMode(focusedScreen, PhosphorZones::AssignmentEntry::Autotile)) {
        return;
    }
    // An autotile-classified screen the engine holds no state for (the
    // explicit algorithm opt-out, or a transient window) has nothing to
    // retile: the engine-global retile() iterates active screens and would
    // no-op for it, and the success card below would claim a retile that
    // never happened on the screen the user fired from.
    if (!m_autotileEngine->stateForScreen(focusedScreen)) {
        return;
    }
    m_autotileEngine->retile();
    if (navigationOsdAllowed(focusedScreen)) {
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
    // Only skip resnap when the current screen is engine-managed.
    // Per-desktop assignments mean some screens can be engine-managed while
    // others are manual — a global check would block manual resnaps.
    if (m_unifiedLayoutController) {
        const QString screenId = m_unifiedLayoutController->currentScreenName();
        if (screenId.isEmpty()) {
            return; // No screen context — can't determine mode, skip resnap
        }
        if (m_autotileEngine && isAutotileScreen(screenId)) {
            return; // This screen is autotile — engine handles retile
        }
        if (currentModeFor(screenId) == PhosphorZones::AssignmentEntry::Scrolling) {
            // A template apply changes only the strip's preset vocabulary;
            // no window placement moved, so buffering every OTHER snapping
            // screen and running resnapToNewLayout would reposition windows
            // for a no-op and burn an OSD-suppression count.
            return;
        }
    }
    // Populate the resnap buffer before resnapping. UnifiedLayoutController::applyEntry()
    // blocks activeLayoutChanged (QSignalBlocker) to prevent whole-screen recalculation,
    // which also prevents onLayoutChanged() from populating the resnap buffer.
    // Additionally, when the global active layout is already the target (e.g. second
    // screen cycling to the same layout), setActiveLayout is a no-op and no signal fires.
    // Explicitly populating here mirrors the KCM's assignmentChangesApplied path.
    if (m_windowTrackingAdaptor) {
        // Exclude EVERY engine-managed screen, not just autotile: the
        // resnap's only mode gate is this exclude set, and resnapping a
        // scroll-owned screen would reposition strip windows to stale zone
        // rects (the KCM twin in init_engines.cpp builds the same union).
        QSet<QString> engineManagedScreens;
        if (m_screenModeRouter && m_screenManager) {
            const auto parts = m_screenModeRouter->partitionByMode(m_screenManager->effectiveScreenIds());
            engineManagedScreens = QSet<QString>(parts.autotile.begin(), parts.autotile.end());
            engineManagedScreens.unite(QSet<QString>(parts.scrolling.begin(), parts.scrolling.end()));
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
        m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(engineManagedScreens, {},
                                                                              currentDesktop());
    }
    // Co-locate the suppress pre-arm with the resnap call so a null
    // m_snapAdaptor doesn't leave the counter armed for the next
    // unrelated navigationFeedback. Mirrors the other armResnapOsdSuppression
    // call sites (a line number into another TU rots on every file split).
    if (m_snapAdaptor) {
        armResnapOsdSuppression(1);
        m_snapAdaptor->resnapToNewLayout();
    }
    // Restore snap-float positions for windows the picker/cycle just released
    // from autotile — the resnap above (buffer-based) cannot cover floating
    // windows. See the helper for why only the float half is emitted here.
    emitPendingSnapFloatRestoresForResnapBuffer();
}

void Daemon::emitPendingSnapFloatRestoresForResnapBuffer(bool preserveZoneEntries)
{
    if (m_pendingSnapFloatRestores.isEmpty()) {
        return;
    }
    // A snap-float restore is a SNAPPING-mode action. An entry whose screen
    // currently resolves to a tiling-family mode is HELD, not emitted:
    // replaying it would float the window out of the live autotile grid or
    // scroll strip (observed as dolphin popping out of Aligned Grid seconds
    // after a snapping→autotile toggle — the presave for the RETURN trip
    // was drained into the mode it was saved against).
    //
    // A held entry is DROPPED, not durably queued: updateEngineScreens clears
    // the whole batch at entry, and the engines' placementChanged count gates
    // re-enter it within the same adoption burst, so a held float survives
    // milliseconds at most. That is by design — the durable restore source is
    // the window's snap slot in its placement record, which
    // handleEngineWindowsReleased re-reads on the return trip. Holding here
    // only has to stop the replay landing in the wrong mode.
    const auto snapOwnsEntryScreen = [this](const ZoneAssignmentEntry& e) {
        if (e.targetScreenId.isEmpty()) {
            return true; // unscreened: historical permissive path
        }
        // Router-backed (downgrades disabled modes to Snapping) — the same
        // verdict every shortcut dispatch uses.
        return currentModeFor(e.targetScreenId) == PhosphorZones::AssignmentEntry::Snapping;
    };
    QVector<ZoneAssignmentEntry> floatEntries;
    QVector<ZoneAssignmentEntry> heldFloatEntries;
    QVector<ZoneAssignmentEntry> zoneEntries;
    for (const ZoneAssignmentEntry& e : std::as_const(m_pendingSnapFloatRestores)) {
        if (e.targetZoneId == RestoreSentinel) {
            if (snapOwnsEntryScreen(e)) {
                floatEntries.append(e);
            } else {
                heldFloatEntries.append(e);
            }
        } else {
            zoneEntries.append(e);
        }
    }
    if (preserveZoneEntries) {
        // Tail-drain mode (updateEngineScreens): the float half is emitted
        // now — floats are excluded from every downstream resnap, so this
        // batch's window set is disjoint from anything a consumer emits
        // later — but the snap-ZONE half MUST survive for the mode-toggle
        // and autotile-disable consumers, which feed it into
        // preClaimedZoneIds / the batched restore. Clearing it here was a
        // regression that left previously-floated-then-toggled windows
        // stranded off their zones.
        m_pendingSnapFloatRestores = zoneEntries + heldFloatEntries;
    } else {
        // Full consume: the caller is (or stands in for) the final
        // consumer on its path. Remaining zone entries are deliberately
        // handed to an in-flight resnapToNewLayout when one exists, else
        // dropped (a prune-origin batch's zones reference a dead screen).
        // Held floats are carried past THIS caller for the same reason they
        // were held — it is not a snapping consumer — but see the note above:
        // the next recompute clears them, and the record is the durable source.
        m_pendingSnapFloatRestores = heldFloatEntries;
    }
    if (floatEntries.isEmpty()) {
        return;
    }
    if (!m_snapEngine) {
        qCWarning(lcDaemon) << "emitPendingSnapFloatRestoresForResnapBuffer: dropping" << floatEntries.size()
                            << "snap-float restores — no snap engine to apply them";
        return;
    }
    if (auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get())) {
        armResnapOsdSuppression(1); // the batched emit drives an additional resnap feedback
        concreteSnap->emitBatchedResnap(floatEntries);
    } else {
        qCWarning(lcDaemon) << "emitPendingSnapFloatRestoresForResnapBuffer: dropping" << floatEntries.size()
                            << "snap-float restores — snap engine is not a SnapEngine";
    }
}

void Daemon::flushPendingSnapZoneRestores()
{
    // Nested inside an outer recompute: our updateEngineScreens() call was
    // deferred to a queued re-run by the re-entrancy latch, so the batch we
    // would drain here belongs to the OUTER pass and its consumer (the
    // mode-toggle / autotile-disable paths feed the zone half into
    // preClaimedZoneIds). Draining it here strands those windows off their
    // zones — the exact regression the tail drain's preserveZoneEntries mode
    // was added to avoid.
    if (m_updateEngineScreensInProgress) {
        return;
    }
    if (m_pendingSnapFloatRestores.isEmpty()) {
        return;
    }
    QVector<ZoneAssignmentEntry> zoneEntries;
    for (const ZoneAssignmentEntry& e : std::as_const(m_pendingSnapFloatRestores)) {
        if (e.targetZoneId != RestoreSentinel) {
            zoneEntries.append(e);
        }
    }
    // The EMITTABLE float half was already emitted by the updateEngineScreens
    // tail drain on every path that reaches here; HELD float entries (the
    // RestoreSentinel ones the drain re-queues) are deliberately dropped by
    // this clear, per the drop policy documented at
    // emitPendingSnapFloatRestoresForResnapBuffer. Clear wholesale so a
    // leftover entry can never be replayed by a later unrelated consumer.
    m_pendingSnapFloatRestores.clear();
    if (zoneEntries.isEmpty()) {
        return;
    }
    if (!m_snapEngine) {
        qCWarning(lcDaemon) << "flushPendingSnapZoneRestores: dropping" << zoneEntries.size()
                            << "snap-zone restores — no snap engine to apply them";
        return;
    }
    auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get());
    if (!concreteSnap) {
        qCWarning(lcDaemon) << "flushPendingSnapZoneRestores: dropping" << zoneEntries.size()
                            << "snap-zone restores — snap engine is not a SnapEngine";
        return;
    }
    qCInfo(lcDaemon) << "flushPendingSnapZoneRestores: restoring" << zoneEntries.size()
                     << "windows to their snap zones";
    armResnapOsdSuppression(1); // the batched emit drives an additional resnap feedback
    concreteSnap->emitBatchedResnap(zoneEntries);
}

void Daemon::handleSwapVirtualScreen(NavigationDirection direction)
{
    if (m_virtualScreenDebounce.isValid() && m_virtualScreenDebounce.elapsed() < kShortcutDebounceMs) {
        return;
    }

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
    // Restart only on actual dispatch — see handleSpan.
    m_virtualScreenDebounce.restart();

    // Run the swap through the daemon-held swapper. The Result enum carries
    // the rejection reason directly, so the OSD can show a specific failure
    // (no_subdivision, no_sibling, …) instead of echoing the raw direction.
    // m_virtualScreenSwapper is constructed in Daemon::init() before any
    // shortcut signals are wired, so it's always non-null on this path —
    // no per-call assertion needed.
    const auto result = m_virtualScreenSwapper->swapInDirection(screenId, dirStr);
    const bool ok = (result == PhosphorScreens::VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "SwapVirtualScreen:" << screenId << dirStr << "->" << static_cast<int>(result);

    // GATE on the focused EFFECTIVE screen, RENDER on the physical monitor.
    // The two ids answer different questions: navigationOsdAllowed resolves the
    // per-context SetOsdEnabled rule, whose contexts are keyed by effective id,
    // and a subdivided output's physical id matches no context at all — asking
    // with it made virtual-screen rules unable to match. The card's target
    // stays physId because the ACTION is monitor-scope.
    if (navigationOsdAllowed(screenId)) {
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

    const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "RotateVirtualScreens shortcut: no screen info";
        return;
    }
    const QString physId = PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
        : screenId;
    // Restart only on actual dispatch — see handleSpan.
    m_virtualScreenDebounce.restart();

    // Swapper is always non-null on the shortcut path — see matching
    // comment in handleSwapVirtualScreen above.
    const auto result = m_virtualScreenSwapper->rotate(physId, clockwise);
    const bool ok = (result == PhosphorScreens::VirtualScreenSwapper::Result::Ok);
    qCDebug(lcDaemon) << "RotateVirtualScreens:" << physId << "cw=" << clockwise << "->" << static_cast<int>(result);

    // Gate on the focused EFFECTIVE screen, render on the physical monitor —
    // see handleSwapVirtualScreen for why the two ids differ here.
    if (navigationOsdAllowed(screenId)) {
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
