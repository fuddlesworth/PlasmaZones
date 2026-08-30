// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Cross-engine handoff: handoffRelease / handoffReceive and the unified
// placement capture. Split from engine_lifecycle.cpp, which crossed the
// file-size ceiling a second time (the min-size re-reporting landed on top
// of the first split that produced engine_float.cpp) — same seam, same
// pattern.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

void ScrollEngine::handoffRelease(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // A preview naming this window must not survive its tracking: commit
    // would re-insert into a strip another engine has since adopted the
    // window from (shared contract gap with autotile's twin, closed here).
    dropClosedWindowFromDragPreview(windowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    // Tracking-only clear: the receiving engine places the window; this
    // screen's remaining columns close up on the scheduled retile.
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    state->strip().takeWindow(windowId, params);
    state->removeFloating(windowId);
    m_states.removeWindow(windowId);
    // The durable slot goes with the tracking: a released window is one this
    // engine knowingly gave up, and a stale scrolling TILED slot left in the
    // unified record is not memory but a false home — paired with a stale
    // record-level screenId (which an engine-miss capture can leave behind),
    // the cross-screen reclaim would later yank the window back out from
    // under its new engine, and that engine's defer gate would read the same
    // stale record and stand down. Ordinary close deliberately KEEPS the
    // slot; only the handoff clears it.
    if (m_windowTracker) {
        m_windowTracker->releaseEngineSlot(windowId, engineId());
    }
    // A released window's queued echo can never be answered — the stale
    // entry would eat the first genuine focus when the window comes back
    // (releaseScreenState documents the same sweep). The declined-open mark
    // goes for the identical reason: its consume site swallows exactly one
    // report, and a mark surviving the release would eat the first genuine
    // focus after re-adoption.
    m_pendingSelfActivations.removeAll(windowId);
    m_declinedOpenFocus.remove(windowId);
    // m_lastAppliedRect deliberately retained (same rationale as
    // windowClosed: a close/capture racing the handoff still needs the
    // poison-guard memory; pruneStaleWindows reclaims it).
    m_floatRestore.remove(windowId);
    // The mode-transition float marker must not outlive this engine's
    // tracking: the receiving engine owns the float bit from here, and a
    // stale entry would keep isModeSpecificFloated answering true.
    m_scrollFloatedWindows.remove(windowId);
    // Same orphan rule as the float path: the window leaves this engine
    // alive, so the park-edge memory has to go here or it survives to
    // mis-anchor the first arrival after a later re-adoption. The
    // windowed-fullscreen apply memory goes with it — a stale true is
    // benign (one redundant emit on re-adoption) but the symmetry keeps
    // the eviction set identical across the exit paths.
    m_parkedScrollEdge.remove(windowId);
    m_lastAppliedWindowedFs.remove(windowId);
    m_lastAppliedMaximizedToEdges.remove(windowId);
    // Background-context guard, as windowClosed and the float paths carry: a
    // release out of another desktop's state must not retile the strip that
    // is on screen right now. The switch back retiles the mutated one.
    if (key == currentKeyForScreen(key.screenId)) {
        scheduleRetileForScreen(key.screenId);
    }
}

void ScrollEngine::handoffReceive(const HandoffContext& ctx)
{
    const QString windowId = canonicalizeForLookup(ctx.windowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(ctx.toScreenId)) {
        return;
    }
    // Same preview hygiene as handoffRelease: an arriving window that a
    // live preview still names would be double-placed at commit.
    dropClosedWindowFromDragPreview(windowId);
    PhosphorEngine::PlacementStateKey key = currentKeyForScreen(ctx.toScreenId);
    if (ctx.toDesktop > 0) {
        key.desktop = ctx.toDesktop;
    }
    // Defence-in-depth single-owner guard: the daemon releases the source
    // first on every current path, but a window still tracked in ANOTHER
    // scroll context here would end up held by two states with the reverse
    // map pointing at only one. Migrate it out (same sweep as
    // windowOpened's context migration) before inserting.
    PhosphorEngine::PlacementStateKey staleKey;
    bool migratedWindowedFs = false;
    if (ScrollState* staleState = stateForWindow(windowId, &staleKey); staleState && staleKey != key) {
        const ScrollLayoutParams staleParams = layoutParamsForScreen(staleKey.screenId);
        const bool staleWasFloating = staleState->isFloating(windowId);
        // Capture before takeWindow destroys the tile, mirroring
        // windowOpened's migration: the receive re-applies it after its own
        // insert so the presentation survives this defence-in-depth hop too.
        migratedWindowedFs = staleState->strip().isWindowedFullscreen(windowId);
        staleState->strip().takeWindow(windowId, staleParams);
        staleState->removeFloating(windowId);
        // Track BEFORE emitting, the doctrine windowOpened's migration states:
        // the two emits below are synchronous, and a subscriber that queries
        // back during them must already see the window as this handoff's —
        // with the reverse map still naming staleKey, heldScreenForWindow
        // answers empty and screenForTrackedWindow answers the OLD screen.
        // Every subsequent path re-stamps the same key unconditionally, so
        // stamping it here changes no end state.
        m_states.setKeyForWindow(windowId, key);
        m_lastAppliedRect.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        if (staleWasFloating) {
            // Same announcement as windowOpened's migration: a silently
            // dropped float bit leaves signal-driven subscribers believing
            // the window floats while the receive tiles it (the
            // wasFloating branch below re-announces true when it applies).
            Q_EMIT windowFloatingStateSynced(windowId, false, staleKey.screenId);
        }
        // Background-context guard, same terms as the sibling sites: the
        // stale context is usually NOT the one on screen.
        if (staleKey == currentKeyForScreen(staleKey.screenId)) {
            scheduleRetileForScreen(staleKey.screenId);
        }
        Q_EMIT placementChanged(staleKey.screenId);
    }
    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    if (state->containsWindow(windowId)) {
        // Already here — nothing to insert, but the reverse map may still
        // name the stale context the migration above just emptied, which
        // would leave the window tracked at a key that no longer holds it.
        m_states.setKeyForWindow(windowId, key);
        // Honour the context payload this arm would otherwise discard: the
        // min-size clamp routes through the ordinary update entry (which
        // handles tile and float shapes plus the background-context guard,
        // and — like every min-size entry — re-runs the oversized verdict,
        // so an oversized clamp floats the tile right here; that is this
        // engine's own policy, not a ctx-driven mutation). The ctx float
        // verdict itself stays log-only — honouring ctx.wasFloating in a
        // defence-in-depth arm with no constructed producer risks more than
        // it fixes, and the daemon's float record re-drives the verdict.
        if (!ctx.minSize.isEmpty()) {
            windowMinSizeUpdated(windowId, ctx.minSize.width(), ctx.minSize.height());
        }
        if (ctx.wasFloating != state->isFloating(windowId)) {
            qCWarning(lcScrollEngine) << "handoffReceive: already-tracked window" << windowId
                                      << "float verdict disagrees with context (ctx.wasFloating=" << ctx.wasFloating
                                      << ")";
        }
        return;
    }
    // Re-adoption starts from a blank rect memory: handoffRelease/windowClosed
    // only retain m_lastAppliedRect long enough to survive the close/capture
    // window, and a leftover entry would defeat applyLayout's emit-on-change
    // gate so no windowsTiled batch ever fires for the re-adopted window. A
    // leftover parked edge is equally foreign to the adopting strip.
    m_lastAppliedRect.remove(windowId);
    m_parkedScrollEdge.remove(windowId);
    if (ctx.wasFloating) {
        state->addFloating(windowId);
        // The window arrives floating and so is never a strip tile here: the
        // FloatRestore entry is the only place its clamp can live, and the
        // source engine just handed it over in ctx.minSize. Without the seed
        // this engine answers "unknown" for a window it manages, and a later
        // unfloat re-inserts it unclamped.
        seedFloatRestoreForOpen(windowId, ctx.minSize.width(), ctx.minSize.height());
        // The float is scroll-managed from here (autotile's receive marks the
        // same way, through the daemon's passive float sync): without the
        // marker a later mode transition treats it as a snap float and
        // poisons the snap slot with the arrival frame.
        m_scrollFloatedWindows.insert(windowId);
        m_states.setKeyForWindow(windowId, key);
        if (ctx.heldFocus) {
            // The arrival holds compositor focus and keeps it across the
            // handoff, so no focus report will arrive to record the side
            // change — seed the pair like floatWindowInternal's active-tile
            // arm, or moveFocusedToTiling answers "Nothing to restore" for
            // the very float the user is looking at, and the focus switch
            // activates an arbitrary sorted-order float instead.
            state->setLastFloatingFocus(windowId);
            state->setFloatingHasFocus(true);
        }
        Q_EMIT windowFloatingStateSynced(windowId, true, ctx.toScreenId);
        // The screen's placement changed too (managed set grew), even
        // though no strip geometry moved.
        Q_EMIT placementChanged(ctx.toScreenId);
        return;
    }
    // Resolved against the POST-insert column count: this path always creates
    // a new column, and the live count still excludes it, so live params on a
    // single-column strip would run the smart-gaps zero-gap regime and bake a
    // Fixed default height — persisted intent, no relayout self-heal — against
    // a work area the received window never occupies. Same discipline as
    // insertOpenedWindow's re-resolve and commitDragInsertPreview's predicted
    // count; unlike the cross-monitor move, nothing re-stamps the height after
    // this insert, so the params have to be right the first time.
    const ScrollLayoutParams params = layoutParamsForScreen(ctx.toScreenId, state->strip().columnCount() + 1);
    // params already resolved this screen's default width one line up —
    // re-fetching the override map for the same value is the duplicate
    // resolve this params-first convention exists to avoid.
    ColumnWidth width = params.defaultColumnWidth;
    if (ctx.sourceGeometry.isValid()) {
        // sourceGeometry is the window's PHYSICAL frame at handoff time, so
        // decode it by the TARGET strip's role: the value being minted is an
        // intent for the strip receiving the window. Reading .width() feeds a
        // cross extent into a main-axis intent on a vertical target.
        // Bounded like every other Fixed intent this engine mints: the source
        // geometry is a compositor-reported frame, so a degenerate or absurd
        // extent would otherwise become the column's standing width intent.
        const int sourceMain = params.axis.mainSize(ctx.sourceGeometry.size());
        width = ColumnWidth::makeFixed(qBound(1, sourceMain, static_cast<int>(kMaxFixedExtentPx)));
    }
    // Entry position comes from the CALLER: the cross-mode dispatcher
    // derives insertIndex from the crossing direction (0 when entering from
    // the strip's left edge), and -1 appends at the right end. This
    // function has no direction of its own to derive an edge from.
    const int columnIdx = (ctx.insertIndex >= 0) ? ctx.insertIndex : state->strip().columnCount();
    if (state->strip().insertWindowAt(columnIdx, windowId, width, effectiveDefaultColumnDisplay(ctx.toScreenId),
                                      params)) {
        // Seed the source engine's last-known min size so the first relayout
        // clamps correctly instead of waiting a refuse/re-discover round-trip.
        // Clamped per axis rather than OR-gated through: a mixed pair like
        // (-1, 500) must not pass a negative floor into the slack math.
        const int handoffMinW = qMax(0, ctx.minSize.width());
        const int handoffMinH = qMax(0, ctx.minSize.height());
        if (handoffMinW > 0 || handoffMinH > 0) {
            state->strip().setWindowMinimumSize(windowId, handoffMinW, handoffMinH);
        }
        // Re-apply the migrated flag, mirroring windowOpened's migration.
        if (migratedWindowedFs) {
            state->strip().setWindowedFullscreen(windowId, true);
        }
        m_states.setKeyForWindow(windowId, key);
        const bool isCurrentContext = key == currentKeyForScreen(ctx.toScreenId);
        if (isCurrentContext) {
            state->strip().focusWindow(windowId, params);
            applyLayout(ctx.toScreenId, false);
        }
        Q_EMIT placementChanged(ctx.toScreenId);
        return;
    }
    // The insert refused (an empty id, or a window this strip already holds —
    // both ruled out above, so this is a real inconsistency). Every sibling
    // insert site logs its refusal; a silent one here leaves the window
    // released by the source engine and adopted by nobody, with no trace.
    qCWarning(lcScrollEngine) << "handoffReceive: insert refused for" << windowId << "on" << ctx.toScreenId
                              << "— the window is released by its source engine, unadopted here, and its"
                              << "tracking key has been dropped";
    // Do not leave a reverse-map key for a window no structure holds — the
    // stale-context migration above already ran takeWindow, so the key would
    // name a context that no longer contains it. Same removal, and the same
    // reason, as insertOpenedWindow's refusal path.
    m_states.removeWindow(windowId);
}

// ── Unified placement capture ───────────────────────────────────────────────

std::optional<PhosphorEngine::WindowPlacement> ScrollEngine::capturePlacement(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);

    // A window mid-drag-insert has NO capturable placement, and answering
    // anyway silently destroys the one it had. Under DETACH-ONCE, begin drops
    // the window from the floating set and out of the strip while KEEPING it
    // tracked — so the else arm below would read isFloating()==false, take the
    // tiled branch, and record columnOfWindow() == -1. A pre-drag FLOATING
    // window would have its floating record overwritten with tiled/order=-1;
    // insertOpenedWindow's restore ladder then never reaches the floating arm,
    // its `order >= 0` test fails too, and the window reopens tiled with its
    // remembered float-back gone.
    //
    // This is reachable on an ordinary hold: any OTHER window's
    // placementChanged (or a periodic/shutdown save) restarts the save
    // timer, which can then fire mid-hold. (begin's own same-screen
    // applyLayout does NOT emit placementChanged for this — the anchorMoved
    // gate is skipped under dragPreviewSteersView — so the trigger is
    // always external to the drag.) Returning nullopt leaves the pre-drag
    // record intact, which is the same answer the adaptor already
    // documents for an unmanaged window.
    if (m_dragInsertPreview && m_dragInsertPreview->windowId == windowId) {
        return std::nullopt;
    }

    PhosphorEngine::PlacementStateKey key;
    const ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return std::nullopt;
    }
    PhosphorEngine::WindowPlacement placement;
    placement.windowId = windowId;
    // Registry answer, not a parse of the canonical id: the canonical is
    // frozen at first contact, so a window KWin had not yet classed carries an
    // appId-less id for its whole life, and WindowPlacementStore::record
    // REJECTS a record with an empty appId — the capture would vanish and the
    // window would have nothing to restore on reopen. This narrows that hole
    // rather than closing it: currentAppIdFor falls back to the same parse
    // when the registry record ALSO has no class yet, so a capture taken
    // before the class-change push still produces an empty appId and is still
    // dropped. It converges once the class arrives, which the parse never did.
    placement.appId = currentAppIdFor(windowId);
    placement.screenId = key.screenId;
    placement.virtualDesktop = key.desktop;
    placement.activity = key.activity;

    PhosphorEngine::EngineSlot slot;
    if (state->isFloating(windowId)) {
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
    } else {
        slot.state = PhosphorEngine::WindowPlacement::stateTiled();
        // The COLUMN index at capture time, recorded as context only. Nothing
        // consumes it for placement: the reopen path takes floating slots
        // only, and a tiled slot's job is to stand as the exact-final
        // evidence that the window closed tiled.
        slot.order = state->strip().columnOfWindow(windowId);
    }
    placement.engines.insert(engineId(), slot);
    return placement;
}

} // namespace PhosphorScrollEngine
