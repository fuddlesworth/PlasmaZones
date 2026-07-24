// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
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

void AutotileEngine::toggleWindowFloat(const QString& rawWindowId, const QString& screenId)
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
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_screen"), QString(), QString(),
                                  QString());
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
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("window_not_tracked"), QString(),
                                  QString(), screenId);
        return;
    }

    performToggleFloat(state, windowId, resolvedScreen);
}

void AutotileEngine::performToggleFloat(PhosphorTiles::TilingState* state, const QString& windowId,
                                        const QString& screenId)
{
    state->toggleFloating(windowId);
    m_overflow.clearOverflow(windowId); // User explicitly toggled, no longer overflow
    retileAfterOperation(screenId, true);

    const bool isNowFloating = state->isFloating(windowId);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled") << "on screen" << screenId;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-engine handoff (see IPlacementEngine.h for contract)
// ═══════════════════════════════════════════════════════════════════════════════

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
        return;
    }
    // Already tracked but on a DIFFERENT autotile state (cross-screen
    // handoff inside the same engine, or stale tracking after an aborted
    // prior handoff). Release the previous state first to avoid orphaning
    // the entry — handoffRelease is the correct primitive for "drop
    // tracking without mutating geometry" within this engine too.
    if (trackedKeyIt != m_states.windowKeys().constEnd() && trackedKeyIt.value() != destKey) {
        handoffRelease(windowId);
    }

    // Insert at the position dictated by the insertion-order setting (a
    // directional cross-mode move should land where new windows land), except:
    //   - a cross-mode SWAP carries an explicit insertIndex so the arriving
    //     window takes the departed partner's exact slot;
    //   - a drag-drop carrying a cursor position, which the drag-insert path
    //     places separately — there we keep the simple append so the drop wins.
    if (ctx.insertIndex >= 0 && ctx.dropPos.isNull()) {
        state->addWindow(windowId, ctx.insertIndex);
    } else if (ctx.dropPos.isNull()) {
        insertWindowByConfigOrder(state, windowId, ctx.toScreenId);
    } else {
        state->addWindow(windowId);
    }
    // Autotile-engine policy on receive: a window arriving as "floating in
    // the source" stays floating here too — drag-from-snap typically falls
    // into this branch, and the user's drop position is where they want it.
    // A non-floating arrival gets tiled (the layout engine picks the slot)
    // — drag-from-another-autotile-screen typically falls here.
    state->setFloating(windowId, ctx.wasFloating);
    // Keep the memory algorithm's bookkeeping consistent for a non-floating
    // arrival — symmetric with the removal hook in handoffRelease. Floating
    // arrivals are not in tiledWindows(), so indexOf misses and the hook is
    // correctly skipped.
    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(ctx.toScreenId);
    if (algo && algo->supportsLifecycleHooks()) {
        const int idx = state->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            algo->onWindowAdded(state, idx);
        }
    }
    m_states.setKeyForWindow(windowId, destKey);

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

    PhosphorTiles::TilingState* state = stateForWindow(windowId);
    if (!state) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << (shouldFloat ? "floatWindow" : "unfloatWindow") << "- window not tracked=" << windowId;
        return;
    }

    if (state->isFloating(windowId) == shouldFloat) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << (shouldFloat ? "floatWindow: already floating" : "unfloatWindow: not floating") << "-" << windowId;
        return;
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
