// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Own header
#include <PhosphorTileEngine/AutotileEngine.h>

// Project headers
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorEngine/LayerFocusSwitch.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

// Qt and std
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>
#include <algorithm>
#include <cmath>

namespace PhosphorTileEngine {

void AutotileEngine::focusInDirection(const QString& direction, const QString& action)
{
    m_navigation->focusInDirection(direction, action);
}

void AutotileEngine::moveFocusedToPosition(int position)
{
    m_navigation->moveFocusedToPosition(position);
}

void AutotileEngine::toggleFocusedWindowFloat()
{
    // Resolve the target screen through the SHARED helper rather than a local
    // copy: a hand-copied clone drifted out of sync once already, keeping the
    // focus gate that made screen-scoped operations act on the wrong monitor.
    // The focus requirement stays here, as the consumer's own check below.
    //
    // requireTiledWindows is false because this is the one caller that wants
    // the screen HOLDING THE FOCUS rather than a screen with a layout. A
    // monitor whose windows are all floating has an empty tiledWindows() and a
    // live focusedWindow(), and it is precisely the target of a "put me back
    // in the layout" press; demanding tiles there would hand back a different
    // monitor's state and float the wrong window.
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    m_navigation->tiledWindowsForFocusedScreen(screenId, state, QString(), /*requireTiledWindows=*/false);

    if (!state) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "toggleFocusedWindowFloat: no state found for focused screen"
                                                    << "- m_activeScreen=" << m_activeScreen;
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_focused_screen"), QString(),
                                  QString(), m_activeScreen);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "toggleFocusedWindowFloat: no focused window on screen" << screenId;
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_focused_window"), QString(),
                                  QString(), screenId);
        return;
    }

    performToggleFloat(state, focused, screenId);
}

void AutotileEngine::switchFocusBetweenFloatingAndTiling(const QString& screenId)
{
    // Prefer the caller's screen when it names a KNOWN autotile screen —
    // tilingStateForScreen lazily creates state, so a known screen always
    // resolves and the fallback below runs only for an empty or foreign
    // screen id. In that case resolve the screen holding the focus, with
    // requireTiledWindows false for the same reason toggleFocusedWindowFloat
    // passes it: an all-floating monitor is a legitimate press target.
    QString screen = screenId;
    PhosphorTiles::TilingState* state = nullptr;
    if (!screen.isEmpty() && isAutotileScreen(screen)) {
        state = tilingStateForScreen(screen);
    }
    if (!state) {
        screen.clear();
        m_navigation->tiledWindowsForFocusedScreen(screen, state, QString(), /*requireTiledWindows=*/false);
    }
    const QString action = QStringLiteral("float");
    if (!state) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }

    // Leg/target/refusal shape comes from the shared resolver (see the
    // scroll engine's switchFocusBetweenFloatingAndTiling for the contract).
    // Minimized windows are filtered on BOTH sides: the daemon models
    // minimize as a float, and the layer memories can name one.
    //
    // Unlike scroll, no eager bookkeeping precedes the activation: autotile
    // has no self-activation echo filter, so the compositor's answering
    // focus report flows through setFocusedWindow and updates the layer
    // memories exactly as a genuine focus would — the switch is self-healing
    // when the compositor drops the activation.
    const auto isHidden = [this](const QString& id) {
        return m_windowRegistry && m_windowRegistry->minimizedState(id).value_or(false);
    };
    PhosphorEngine::LayerSwitchSide tiledSide;
    tiledSide.candidate = state->lastTiledFocus();
    tiledSide.fallbacks = state->tiledWindows();
    tiledSide.isEligible = [state, isHidden](const QString& id) {
        return state->containsWindow(id) && !state->isFloating(id) && !isHidden(id);
    };
    PhosphorEngine::LayerSwitchSide floatingSide;
    floatingSide.candidate = state->lastFloatingFocus();
    floatingSide.fallbacks = state->floatingWindows();
    floatingSide.isEligible = [state, isHidden](const QString& id) {
        return state->isFloating(id) && !isHidden(id);
    };
    // The resolver reads focusForFeedback from the SOURCE side only, and
    // this verb's source is always the state's live focus slot — one value,
    // no per-leg distinction.
    tiledSide.focusForFeedback = state->focusedWindow();
    floatingSide.focusForFeedback = state->focusedWindow();

    announceLayerSwitch(PhosphorEngine::resolveLayerFocusSwitch(state->floatingHasFocus(), tiledSide, floatingSide),
                        action, screen);
}

void AutotileEngine::toggleWindowFloat(const QString& rawWindowId, const QString& screenId)
{
    toggleWindowFloatAs(rawWindowId, screenId, QStringLiteral("float"));
}

void AutotileEngine::toggleWindowFloatAs(const QString& rawWindowId, const QString& screenId,
                                         const QString& failureAction)
{
    if (!warnIfEmptyWindowId(rawWindowId, "toggleWindowFloat")) {
        return;
    }
    // This is the path that broke for Emby (discussion #271): the incoming
    // composite has a mutated appId, so a raw lookup in m_states
    // missed. Canonicalize resolves it back to the first-seen form.
    const QString windowId = canonicalizeWindowId(rawWindowId);

    if (screenId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "toggleWindowFloat: empty screenId for window" << windowId;
        Q_EMIT navigationFeedback(false, failureAction, QStringLiteral("no_screen"), QString(), QString(), QString());
        return;
    }

    // Try the given screen first
    QString resolvedScreen = screenId;
    PhosphorTiles::TilingState* state = nullptr;

    if (isAutotileScreen(screenId)) {
        state = tilingStateForScreen(screenId);
        if (state && !state->containsWindow(windowId)) {
            state = nullptr; // Window not on this screen
        }
    }

    // Cross-screen fallback: the window may have been moved (e.g., pre-autotile
    // geometry restore put it on a different screen). Search current desktop/activity
    // states only — states for other desktops should not be considered.
    if (!state) {
        for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
            if (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_context.currentActivity()) {
                continue;
            }
            if (it.value() && it.value()->containsWindow(windowId)) {
                state = it.value();
                resolvedScreen = it.key().screenId;
                qCInfo(PhosphorTileEngine::lcTileEngine) << "toggleWindowFloat: window" << windowId << "found on screen"
                                                         << resolvedScreen << "(caller reported" << screenId << ")";
                break;
            }
        }
    }

    if (!state) {
        // Window not tracked by autotile. The opportunistic "is this a
        // floating window I should adopt?" branch that used to live here
        // was the second-order accomplice in a class of cross-engine
        // misroute bugs: if the daemon's lastActiveScreen pointed at an
        // autotile screen while the window actually lived on a snap screen
        // (because snap had cleared its tracking on float), this branch
        // would silently grab the floating window and tile it on the wrong
        // screen.
        //
        // Cross-engine handoff now goes through the explicit
        // handoffReceive/handoffRelease contract orchestrated by the daemon
        // — this path is purely "no-op when the window isn't ours".
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "toggleWindowFloat: window" << windowId << "not found in any autotile state";
        Q_EMIT navigationFeedback(false, failureAction, QStringLiteral("window_not_tracked"), QString(), QString(),
                                  screenId);
        return;
    }

    performToggleFloat(state, windowId, resolvedScreen);
}

void AutotileEngine::performToggleFloat(PhosphorTiles::TilingState* state, const QString& windowId,
                                        const QString& screenId)
{
    // Branch on the result, like every other mutation site in the engine.
    // toggleFloating returns false for a window this state does not contain;
    // both current callers validate membership first, so this is unreachable
    // today — but ignoring it meant a future caller would emit "now tiled" for
    // an unmanaged window and clear a legitimate snap float downstream.
    if (!state->toggleFloating(windowId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "performToggleFloat: state does not contain" << windowId << "on screen" << screenId;
        return;
    }
    m_overflow.clearOverflow(windowId); // User explicitly toggled, no longer overflow

    const bool isNowFloating = state->isFloating(windowId);
    // Same staleness policy as setWindowFloat's unfloat arm: the window's
    // min size may have changed while floating, and a stale entry can
    // inflate enforceMinSizes past the user's split ratio. The effect
    // re-discovers and re-reports a live one on the next retile.
    if (!isNowFloating) {
        m_windowMinSizes.remove(windowId);
    }
    retileAfterOperation(screenId, true);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled") << "on screen" << screenId;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-engine handoff (see IPlacementEngine.h for contract)
// ═══════════════════════════════════════════════════════════════════════════════

// NOTE: autotile ignores ctx.toDesktop — arrivals always land in the
// CURRENT desktop's TilingState. Cross-desktop moves onto an autotile
// desktop must use the reactive catch-scan path instead (the cross-mode
// dispatcher's reactiveAutotileDesktopArrival branch does exactly that).
void AutotileEngine::handoffReceive(const HandoffContext& ctx)
{
    if (ctx.windowId.isEmpty() || ctx.toScreenId.isEmpty() || !isAutotileScreen(ctx.toScreenId)) {
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "AutotileEngine::handoffReceive:" << ctx.windowId << "to" << ctx.toScreenId << "from" << ctx.fromEngineId
        << "wasFloating=" << ctx.wasFloating;

    const QString windowId = canonicalizeWindowId(ctx.windowId);

    PhosphorTiles::TilingState* state = tilingStateForScreen(ctx.toScreenId);
    if (!state) {
        return;
    }

    // Already tracked on the destination screen — nothing to adopt; the float
    // toggle path is what the caller probably wants instead. No float
    // announcement needed on this return: the float bit is untouched (we
    // return before setFloating), so what subscribers last heard remains
    // accurate.
    const auto destKey = currentKeyForScreen(ctx.toScreenId);
    const auto trackedKeyIt = m_states.windowKeys().constFind(windowId);
    if (trackedKeyIt != m_states.windowKeys().constEnd() && trackedKeyIt.value() == destKey
        && state->containsWindow(windowId)) {
        // Already adopted — nothing structural to do, but a re-handoff can
        // carry a FRESHER min size; on a genuine change the layout must
        // re-validate (same contract as windowMinSizeUpdated).
        if ((ctx.minSize.width() > 0 || ctx.minSize.height() > 0)
            && storeWindowMinSize(windowId, ctx.minSize.width(), ctx.minSize.height())) {
            scheduleRetileForScreen(ctx.toScreenId);
        }
        return;
    }
    // Already tracked but on a DIFFERENT autotile state (cross-screen
    // handoff inside the same engine, or stale tracking after an aborted
    // prior handoff). Release the previous state first to avoid orphaning
    // the entry — handoffRelease is the correct primitive for "drop
    // tracking without mutating geometry" within this engine too.
    QSize preservedMin(0, 0);
    if (trackedKeyIt != m_states.windowKeys().constEnd() && trackedKeyIt.value() != destKey) {
        // Internal re-home: the release wipes m_windowMinSizes on the
        // assumption that ctx.minSize re-seeds it — untrue when the daemon
        // built the context from an engine that does not model min sizes
        // (snap's windowMinimumSize is the 0x0 default). Preserve our own
        // live value for that case; it is re-stored through
        // storeWindowMinSize AFTER the destination key is set below, so it
        // caps against the DESTINATION screen — a raw re-insert would
        // replay a possibly other-screen-capped value, exactly what
        // handoffRelease drops the entry to avoid.
        if (ctx.minSize.width() <= 0 && ctx.minSize.height() <= 0) {
            preservedMin = m_windowMinSizes.value(windowId, QSize(0, 0));
        }
        handoffRelease(windowId);
    }

    // Insert at the position dictated by the insertion-order setting (a
    // directional cross-mode move should land where new windows land), except:
    //   - a cross-mode SWAP carries an explicit insertIndex so the arriving
    //     window takes the departed partner's exact slot;
    //   - a drag-drop carrying a cursor position, which the drag-insert path
    //     places separately — there we keep the simple append so the drop wins.
    bool inserted = false;
    if (ctx.insertIndex >= 0 && ctx.dropPos.isNull()) {
        inserted = state->addWindow(windowId, ctx.insertIndex);
    } else if (ctx.dropPos.isNull()) {
        insertWindowByConfigOrder(state, windowId, ctx.toScreenId);
        inserted = state->containsWindow(windowId);
    } else {
        inserted = state->addWindow(windowId);
    }
    if (!inserted) {
        // The destination refused (MaxRuntimeTreeDepth cap). The source has
        // already released the window at every daemon call site, so keying
        // it here would strand it: present in neither engine yet reading as
        // tiled through the phantom key. Leave it unmanaged (free) instead —
        // the same outcome as a window neither engine ever tracked. The
        // float-sync announcement is deliberately withheld (the window is
        // unmanaged, not floating), but the screen still reflows: a swap
        // source that just lost its partner must not keep a hole.
        qCWarning(PhosphorTileEngine::lcTileEngine) << "handoffReceive: destination state refused" << windowId << "on"
                                                    << ctx.toScreenId << "- window left unmanaged";
        // Leave no trace: an unmanaged window keeps no min-size entry
        // either (pruneStaleWindows would sweep it eventually, but the
        // refusal branch's contract is immediate cleanliness).
        m_windowMinSizes.remove(windowId);
        retileAfterOperation(ctx.toScreenId, true);
        return;
    }
    // Autotile-engine policy on receive: a window arriving as "floating in
    // the source" stays floating here too — drag-from-snap typically falls
    // into this branch, and the user's drop position is where they want it.
    // A non-floating arrival gets tiled (the layout engine picks the slot)
    // — drag-from-another-autotile-screen typically falls here.
    state->setFloating(windowId, ctx.wasFloating);
    // Keep the memory algorithm's bookkeeping consistent for a non-floating
    // arrival — symmetric with the removal hook in handoffRelease.
    notifyAlgorithmWindowAdded(state, ctx.toScreenId, windowId);
    m_states.setKeyForWindow(windowId, destKey);
    // Seed the source engine's last-known min size so the first retile
    // clamps correctly instead of waiting a refuse/re-discover round-trip.
    if (ctx.minSize.width() > 0 || ctx.minSize.height() > 0) {
        storeWindowMinSize(windowId, ctx.minSize.width(), ctx.minSize.height());
    } else if (preservedMin.width() > 0 || preservedMin.height() > 0) {
        // Internal re-home with a min-size-less context: re-store OUR
        // preserved value through the capping path so it clamps against
        // the destination screen (see the preservation comment above).
        storeWindowMinSize(windowId, preservedMin.width(), preservedMin.height());
    }

    // Announce the received float bit on the passive channel (the snap twin
    // announces its float re-homes via windowFloatingChanged from its
    // handoffReceive; this passive signal is autotile's position-preserving
    // analogue — this engine previously set the bit silently). A cross-mode
    // move/swap of a floating window arrives with
    // wasFloating=false after the source engine's handoffRelease cleared its
    // bit without emitting — without this, subscribers that last heard
    // "floating" (the effect's FloatingCache) stay stale until a daemon
    // reconnect. Deliberately UNCONDITIONAL, not divergence-gated against
    // the WTS resolver: mid-handoff the resolver already reads the cleared
    // source bit, so it cannot tell what subscribers last heard — the
    // adaptor's last-broadcast gate owns that dedup. Passive signal, not
    // windowFloatingChanged: the window already has a valid position and
    // must not ride the pre-tile geometry restore.
    Q_EMIT windowFloatingStateSynced(windowId, ctx.wasFloating, ctx.toScreenId);

    // Trigger a retile so a non-floating arrival actually lands in a tile;
    // floating arrivals retile too because their displacement may free a
    // slot for the remaining tiled set.
    retileAfterOperation(ctx.toScreenId, true);
}

void AutotileEngine::handoffRelease(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    const QString canonical = canonicalizeWindowId(windowId);
    qCInfo(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::handoffRelease:" << canonical;

    auto it = m_states.windowKeys().constFind(canonical);
    if (it == m_states.windowKeys().constEnd()) {
        return; // Not ours; nothing to release.
    }
    const auto key = it.value();
    if (PhosphorTiles::TilingState* state = m_states.stateForKey(key)) {
        // Tracking-only release: drop from layout, drop from floating set.
        // No retile of the rest is requested here — the orchestrator will
        // call receiveWindow on the destination engine which (if also
        // autotile) will retile its own state.
        // Keep the memory algorithm's bookkeeping consistent (e.g.
        // dwindle-memory's split tree) — same lifecycle hook every other
        // removal path runs before removeWindow.
        PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(key.screenId);
        if (algo && algo->supportsLifecycleHooks()) {
            const int idx = state->tiledWindows().indexOf(canonical);
            if (idx >= 0) {
                algo->onWindowRemoved(state, idx);
            }
        }
        state->removeWindow(canonical);
    }
    m_states.removeWindow(canonical);
    // The durable slot goes with the tracking (the scroll twin carries the
    // same clear): a released window is one this engine knowingly gave up,
    // and a stale autotile TILED slot left in the unified record is not
    // memory but a false home — paired with a stale record-level screenId
    // (which an engine-miss capture can leave behind), the cross-screen
    // reclaim would later yank the window back out from under its new
    // engine. Ordinary close deliberately KEEPS the slot; only the handoff
    // clears it.
    if (m_windowTracker) {
        m_windowTracker->releaseEngineSlot(canonical, engineId());
    }
    // The min-size cache leaves with the tracking: the daemon queries
    // windowMinimumSize BEFORE calling release (the HandoffContext
    // contract), and a re-receive re-seeds from ctx.minSize — keeping the
    // entry would replay a possibly other-screen-capped value on re-entry
    // (same rationale as windowFocused's cross-screen clear).
    m_windowMinSizes.remove(canonical);
    // The mode-transition float marker must not outlive this engine's
    // tracking: the receiving engine owns the float bit from here. The
    // autotile->snapping direction is covered by the daemon's clear on
    // windowSnapStateChanged, but autotile->scrolling has no such clear, so a
    // stale entry keeps isModeSpecificFloated answering true for a window
    // this engine no longer manages — which makes presaveSnapFloats skip it
    // and loses its snap float/zone on the next return to snapping.
    // ScrollEngine::handoffRelease clears its twin for the same reason.
    m_autotileFloatedWindows.remove(canonical);
    // Overflow bookkeeping likewise: every other removal path clears it, and
    // a live entry left behind makes capturePlacement's overflow-vs-user-float
    // discriminator record a genuine later user float as tiled.
    m_overflow.clearOverflow(canonical);
    // A pending seed position or post-retile focus naming a window another
    // engine now owns must not replay on this screen's next applyTiling.
    purgeFromPendingOrders(canonical);
    purgePendingFocusForWindow(canonical);
}

void AutotileEngine::setWindowFloat(const QString& rawWindowId, bool shouldFloat, const QString& callerScreenId)
{
    // Autotile resolves the retile screen from the window's own per-window
    // tracking (m_states, read at the retile below), which is kept
    // current across monitors by the focus-driven cross-screen migration in
    // windowFocused(). That migration is autotile's analogue of the snap
    // engine's stale-screen hazard guard: it re-homes the window's tiling-state
    // membership when the window is focused on a different autotile screen, so
    // by unfloat time the tracked screen is the window's real monitor. The
    // effect-provided screen is therefore redundant for this engine; accept it
    // to satisfy the shared interface.
    Q_UNUSED(callerScreenId)
    if (!warnIfEmptyWindowId(rawWindowId, shouldFloat ? "floatWindow" : "unfloatWindow")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // floatWindow checks autotile screen membership; unfloatWindow does not
    // (window might be on a screen that was removed from autotile after it was floated)
    if (shouldFloat && !isAutotileScreen(m_states.keyForWindow(windowId).screenId)) {
        return;
    }

    // MEMBERSHIP, not just a non-null state: stateForWindow resolves through the
    // reverse key map, which a refused insert can leave pointing at a state that
    // does not hold the window. TilingState::setFloating no-ops for an unheld
    // window, so without this the write below silently did nothing while the
    // logging and the windowFloatingStateSynced emission at the tail still
    // announced a float that never happened — the effect's float cache then
    // latched at "floating" and never came back (Discussion #1028).
    PhosphorTiles::TilingState* state = stateForWindow(windowId);
    if (!state || !state->containsWindow(windowId)) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << (shouldFloat ? "floatWindow" : "unfloatWindow") << "- window not tracked=" << windowId;
        // Refusing is not enough on its own: the key that misrouted this call
        // here is still in the reverse map, so isWindowTracked keeps answering
        // true and the adaptor keeps choosing this engine over the adoption
        // handoff — turning the silent wrong write this guard was added to stop
        // into a silent permanent refusal. Drop the key (and the per-window
        // caches that follow it, matching every other sweep in this engine) so
        // the NEXT dispatch routes through adoption instead.
        const auto keyIt = m_states.windowKeys().constFind(windowId);
        if (keyIt != m_states.windowKeys().constEnd()) {
            m_states.removeWindow(windowId);
            m_windowMinSizes.remove(windowId);
            m_autotileFloatedWindows.remove(windowId);
            m_overflow.clearOverflow(windowId);
            purgeFromPendingOrders(windowId);
        }
        return;
    }

    if (state->isFloating(windowId) == shouldFloat) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << (shouldFloat ? "floatWindow: already floating" : "unfloatWindow: not floating") << "-" << windowId;
        return;
    }

    // An unfloat that the very next retile would UNDO is refused here instead of
    // being performed and silently reversed. retileAfterOperation below runs
    // synchronously, so applyTiling's overflow pass runs before this call
    // returns; the caller then reads the float state back, still sees
    // "floating", and retries — the effect's unminimize path does exactly that,
    // up to kAutotileMaxUnfloatRetries, and every attempt costs a full unfloat +
    // retile + refloat with the batch signals to match.
    //
    // The test is POSITIONAL, not a bare capacity check, because the overflow
    // pass evicts by position rather than by which window moved. applyOverflow
    // floats state->tiledWindows()[i] for i >= cap, and unfloating restores this
    // window at its own m_windowOrder position — so it lands at index
    // "however many tiled windows sort before it". Only when that index is
    // already at the cap does the pass re-float THIS window, making the unfloat
    // a no-op that can never converge. Below the cap the pass evicts a
    // different window and the unfloat genuinely succeeds, which is the
    // behaviour an unminimize wants; refusing there would strand a window the
    // engine could perfectly well tile.
    //
    // The refusal MARKS the window as overflow, which is what makes recovery
    // real rather than merely claimed: recoverIfRoom only ever returns windows
    // in m_overflow, and a minimize-float cleared that marker on its way in. So
    // without the mark the window would sit floating with nothing to bring it
    // back.
    if (!shouldFloat) {
        const QString unfloatScreen = m_states.keyForWindow(windowId).screenId;
        // The CAP, deliberately, and NOT the pass's own min(cap, zones.size()).
        // retileScreen runs recalculateLayout BEFORE applyTiling, and the
        // recalc sizes the zone vector to min(tiledCount, cap) counting the
        // windows tiled AFTER this unfloat. So on the success path the pass
        // compares against min(tiledCount + 1, cap), which wouldSortAt (built
        // only from windows already tiled) can reach only when it reaches the
        // cap itself.
        //
        // Reading calculatedZones() HERE reads the pre-unfloat count instead,
        // which equals tiledCount below the cap. Any window sorting after every
        // tiled one then has wouldSortAt == zones.size() and is refused however
        // empty the screen is: two tiled windows under a cap of eight would
        // refuse the third. The unminimize path sorts at the end, so that
        // misfires on the very case this refusal was written for.
        //
        // The one thing the cap does not predict is a FAILED recalc, where the
        // stale shorter vector stands and the pass can still re-float a window
        // admitted here. That costs one retry of an already-transient failure,
        // much the cheaper of the two errors.
        //
        // NOT COVERED BY A UNIT TEST, deliberately, and worth knowing before
        // touching this line. The engine harness wires no ScreenManager, so
        // recalculateLayout fails on the geometry check for every test in the
        // suite and the zone vector never grows. That harness therefore models
        // only the recalc-FAILURE path, in which the clamped form looks correct
        // and the cap form looks wrong, i.e. exactly backwards from production.
        // The suite passed with the clamped form in place. Reason about this
        // from retileScreen's recalc-then-apply order, not from a green run.
        const int cap = std::min(effectiveMaxWindows(unfloatScreen), PhosphorTiles::AutotileDefaults::MaxZones);
        const int myIndex = state->windowIndex(windowId);
        int wouldSortAt = 0;
        for (const QString& tiled : state->tiledWindows()) {
            const int idx = state->windowIndex(tiled);
            if (idx >= 0 && myIndex >= 0 && idx < myIndex) {
                ++wouldSortAt;
            }
        }
        if (wouldSortAt >= cap) {
            qCInfo(PhosphorTileEngine::lcTileEngine)
                << "unfloatWindow: refusing" << windowId << "on" << unfloatScreen << "— it would land at tiled position"
                << wouldSortAt << "with a cap of" << cap
                << ", so the retile would re-float it; keeping it floating and queued for recovery";
            // Queue it for the recovery pass. Without this the window is
            // floating but absent from m_overflow, which is the one set
            // recoverIfRoom consults, so nothing would ever bring it back.
            m_overflow.markOverflow(windowId, unfloatScreen);
            // Re-announce the state the window is ACTUALLY in. The caller asked
            // for false and is getting true, so without this it would keep
            // believing its request was lost.
            Q_EMIT windowFloatingStateSynced(windowId, true, unfloatScreen);
            return;
        }
    }

    state->setFloating(windowId, shouldFloat);
    m_overflow.clearOverflow(windowId);

    // Clear cached min-size when unfloating so the next retile starts fresh.
    // The window's minimum size may have changed while floating/minimized
    // (e.g. browser finished loading media, terminal resized). Stale min-sizes
    // can override the user's split ratio by inflating enforceMinSizes
    // constraints. The centering code in the KWin effect will re-discover and
    // report the actual min-size if the window can't fill its assigned zone.
    if (!shouldFloat) {
        const bool hadMinSize = m_windowMinSizes.contains(windowId);
        const QSize clearedMinSize = m_windowMinSizes.value(windowId, QSize(0, 0));
        m_windowMinSizes.remove(windowId);
        if (hadMinSize) {
            qCDebug(PhosphorTileEngine::lcTileEngine)
                << "unfloat: cleared stale minSize=" << clearedMinSize << "for" << windowId;
        }
    }

    const QString screenId = m_states.keyForWindow(windowId).screenId;
    retileAfterOperation(screenId, true);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << (shouldFloat ? "floated from" : "unfloated to") << "autotile -" << windowId;
    // Use windowFloatingStateSynced (not windowFloatingChanged): the only caller
    // of setWindowFloat is WindowTrackingAdaptor::setWindowFloatingForScreen,
    // invoked by the KWin effect for drag drops, minimize→float, and
    // unminimize→tile. None of those scenarios want the daemon to restore
    // pre-tile geometry — the effect manages drop position locally, and
    // minimize/unminimize don't show the window. Routing through
    // windowFloatingChanged would call applyGeometryForFloat and teleport
    // the window away from where the user dropped it (regression #271).
    // User float toggles (Meta+F) go through performToggleFloat, which
    // continues to emit windowFloatingChanged so geometry is restored.
    Q_EMIT windowFloatingStateSynced(windowId, shouldFloat, screenId);
}

void AutotileEngine::floatWindow(const QString& windowId)
{
    setWindowFloat(windowId, true);
}

void AutotileEngine::unfloatWindow(const QString& windowId)
{
    setWindowFloat(windowId, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public window event handlers (called by Daemon via D-Bus signals)
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PhosphorTileEngine
