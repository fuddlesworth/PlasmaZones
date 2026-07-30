// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

void ScrollEngine::insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId,
                                      int minWidth, int minHeight)
{
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);

    // Fixed-size / oversized windows cannot honour a column slot: float them
    // at their native size instead of forcing a tile (the min size is the
    // only constraint the compositor reports; a rule `float` action covers
    // the rest).
    const bool oversized =
        params.workArea.isValid() && (minWidth > params.workArea.width() || minHeight > params.workArea.height());
    const bool ruleFloated = m_floatPredicate && m_floatPredicate(windowId);
    if (oversized || ruleFloated) {
        state->addFloating(windowId);
        // Engine-decided float, so it carries the mode marker like every
        // other float this engine makes: isModeSpecificFloated has to answer
        // true or the daemon captures the scroll-mode float into the snap
        // slot at the next mode transition (presaveSnapFloats skips exactly
        // the marked windows).
        m_scrollFloatedWindows.insert(windowId);
        // A floated arrival consumes its seed entry too, or the screen's
        // list never empties and the stale entry survives every later mode
        // transition.
        consumePendingInitialOrder(screenId, windowId);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        return;
    }

    // Unified-placement restore: a window recorded tiled in scrolling mode
    // reopens at its recorded column slot; a floating record reopens
    // floating (the shared free geometry is restored by the common layer).
    int restoreColumn = -1;
    if (m_windowTracker) {
        if (const auto record = m_windowTracker->placementStore().peekExact(windowId)) {
            const PhosphorEngine::EngineSlot slot = record->slotFor(engineId());
            if (slot.state == PhosphorEngine::WindowPlacement::stateFloating()) {
                state->addFloating(windowId);
                // The record's SCROLL slot says floating, so this float is
                // this engine's own — marked like the rule-float exit above.
                m_scrollFloatedWindows.insert(windowId);
                consumePendingInitialOrder(screenId, windowId); // same rationale as the rule-float exit
                Q_EMIT windowFloatingChanged(windowId, true, screenId);
                return;
            }
            if (slot.state == PhosphorEngine::WindowPlacement::stateTiled() && slot.order >= 0) {
                restoreColumn = slot.order;
            }
        }
    }

    ColumnWidth width = effectiveDefaultColumnWidth(screenId);
    ColumnDisplay display = effectiveDefaultColumnDisplay(screenId);
    if (m_defaultWidthClientDecides && m_windowTracker) {
        // "Client decides": open at the client's own size when one is on
        // record; the first client resize reconciles it afterwards.
        if (const auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId)) {
            width = ColumnWidth::makeFixed(geo->width());
        }
    }

    // Per-window open rules layer over the context/config defaults.
    ScrollOpenParams openParams;
    if (m_openParamsResolver) {
        openParams = m_openParamsResolver(windowId);
    }
    if (openParams.widthFraction) {
        width = ColumnWidth::makeProportion(qBound<qreal>(0.05, *openParams.widthFraction, 1.0));
    }
    if (openParams.tabbed) {
        display = *openParams.tabbed ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;
    }
    if (openParams.consume && *openParams.consume && !state->strip().isEmpty()) {
        const std::optional<ColumnDisplay> displayOverride =
            openParams.tabbed ? std::optional<ColumnDisplay>(display) : std::nullopt;
        if (state->strip().insertWindowIntoActiveColumn(windowId, width, displayOverride, params, minWidth,
                                                        minHeight)) {
            // Consume this id from the mode-transition seed too — leaving
            // it would let a stale entry re-position an unrelated later
            // open (the block below documents exactly that hazard).
            consumePendingInitialOrder(screenId, windowId);
            return;
        }
    }

    // Mode-round-trip structure restore FIRST: a strip stashed at the last
    // reassignment away from Scrolling rebuilds exactly (stacks, widths,
    // display, heights), which is strictly stronger than the order seed's
    // position-only verdict. The seed entry is still consumed so it cannot
    // linger past the adoption.
    bool inserted = false;
    if (restoreFromStripStash(state, currentKeyForScreen(screenId), windowId, screenId, minWidth, minHeight)) {
        inserted = true;
        consumePendingInitialOrder(screenId, windowId);
    }

    // Deterministic mode-transition seeding: when the previous engine's
    // window order was captured for this screen, insert each arriving window
    // at its recorded relative position instead of next-to-focus. Each id is
    // CONSUMED on use and the entry is dropped once empty — the header's
    // "consumed as windows arrive" contract. Without consumption a stale
    // seed would re-position an unrelated later open that happens to share
    // an id with the captured list.
    const auto pendingIt = m_pendingInitialOrder.constFind(screenId);
    if (pendingIt != m_pendingInitialOrder.constEnd()) {
        const int orderIdx = pendingIt->indexOf(windowId);
        // A consumed id must not re-enter the seed branch: a later unrelated
        // open reusing the id would otherwise be re-positioned by the stale
        // entry (the list keeps consumed ids to preserve positions).
        if (orderIdx >= 0 && !m_consumedInitialOrder.value(screenId).contains(windowId)) {
            int columnIdx = 0;
            const QStringList present = state->strip().windowsInOrder();
            for (const QString& earlier : pendingIt->mid(0, orderIdx)) {
                if (present.contains(earlier)) {
                    ++columnIdx;
                }
            }
            inserted = state->strip().insertWindowAt(columnIdx, windowId, width, display, params);
            if (inserted) {
                state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
            }
            // Through the shared consume helper — it drops the screen's entry
            // once the list empties. pendingIt is dangling from here.
            consumePendingInitialOrder(screenId, windowId);
        }
    }
    if (!inserted && restoreColumn >= 0) {
        inserted = state->strip().insertWindowAt(restoreColumn, windowId, width, display, params);
        if (inserted) {
            state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
        }
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, width, display, params, minWidth, minHeight);
    }
    if (!inserted) {
        qCWarning(lcScrollEngine) << "insertOpenedWindow: duplicate window" << windowId;
        // Do not leave a reverse-map key for a window no structure holds —
        // that is the exact inconsistency floatWindowInternal warns about.
        // (Keyed-but-present is fine: the insert also fails when the strip
        // already contains the window, and unkeying a live tile would just
        // create the mirror inconsistency.)
        if (!state->strip().containsWindow(windowId) && !state->isFloating(windowId)) {
            m_states.removeWindow(windowId);
        }
    }
}

void ScrollEngine::windowOpened(const QString& rawWindowId, const QString& screenId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }

    PhosphorEngine::PlacementStateKey oldKey;
    ScrollState* oldState = stateForWindow(windowId, &oldKey);
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    if (oldState && oldKey == key) {
        return; // re-announce of a window we already track here
    }

    // Cross-screen snap-restore defer, the reciprocal of
    // SnapEngine::resolveWindowRestore's recorded-screen gate and the twin
    // of AutotileEngine::windowOpened's — all three run
    // PhosphorEngine::pendingCrossScreenSnapRestore over the same record
    // fields, so a session window snapped on a snapping-mode monitor that
    // KWin drops on a scrolling screen at login is claimed by snap
    // cross-screen and NOT double-claimed into the strip here. Gated on
    // !oldState: a window this engine already tracks anywhere is scroll's
    // own (in-session migration), never a session restore.
    // Membership, not the raw reverse-map key (autotile's gate term for
    // term): a refused earlier open can leave a phantom key, and gating on
    // it would skip the defer while this engine manages nothing.
    const bool trackedHere = oldState && oldState->containsWindow(windowId);
    if (!trackedHere && m_snappingModeResolver && m_windowTracker) {
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        if (!appId.isEmpty() && appId != windowId) {
            const auto snapCrossRestorePending = [&](const PhosphorEngine::WindowPlacement& p) {
                return PhosphorEngine::pendingCrossScreenSnapRestore(
                    p, screenId, [this](const QString& rec, int desktop, const QString& activity) {
                        return m_snappingModeResolver(rec, desktop, activity);
                    });
            };
            if (m_windowTracker->placementStore().peek(windowId, appId, snapCrossRestorePending).has_value()) {
                qCInfo(lcScrollEngine) << "windowOpened:" << windowId << "on scrolling screen" << screenId
                                       << "defers to snap — carries a cross-screen snap restore";
                // A deferred arrival is still an arrival: without the
                // consume, this id never reaches insertOpenedWindow and its
                // seed entry lingers on the screen forever.
                consumePendingInitialOrder(screenId, windowId);
                return;
            }
        }
    }
    if (oldState) {
        // The window moved context (screen or desktop) — migrate. The old
        // context's per-window bookkeeping goes with it: a stale
        // FloatRestore could re-slot an unfloat on the NEW screen against
        // the OLD strip's geometry, and lastAppliedRect would keep
        // answering for a context that no longer holds the window.
        const ScrollLayoutParams oldParams = layoutParamsForScreen(oldKey.screenId);
        const bool wasFloating = oldState->isFloating(windowId);
        oldState->strip().takeWindow(windowId, oldParams);
        oldState->removeFloating(windowId);
        m_lastAppliedRect.remove(windowId);
        m_floatRestore.remove(windowId);
        // The mode-float marker goes with the old context too: the window
        // re-enters (usually tiled) on the new screen, and a stale marker
        // would re-float it at the next mode transition.
        m_scrollFloatedWindows.remove(windowId);
        if (wasFloating) {
            // Announce the dropped float bit: signal-driven subscribers
            // (the effect's FloatingCache) would otherwise keep believing
            // the window floats while insertOpenedWindow tiles it below,
            // and resolve the divergence as a float-back. A float RECORD
            // re-float re-announces true immediately afterwards.
            Q_EMIT windowFloatingChanged(windowId, false, oldKey.screenId);
        }
        scheduleRetileForScreen(oldKey.screenId);
        Q_EMIT placementChanged(oldKey.screenId);
    }

    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    // Track BEFORE inserting: insertOpenedWindow's oversized/rule-float
    // paths emit windowFloatingChanged, and a synchronous query-back from a
    // subscriber must already see the window as this engine's.
    m_states.setKeyForWindow(windowId, key);
    // Capture the pre-insert focus: with focus-new-windows OFF the
    // compositor keeps focus on the previous window, so the strip must not
    // adopt the arrival as its active column either — a diverged strip
    // makes every later focus-direction verb navigate from the wrong
    // origin, and a consume-open into a tabbed column would park the
    // window the user is actually looking at.
    const QString priorActive = state->strip().activeWindowId();
    insertOpenedWindow(state, windowId, screenId, minWidth, minHeight);
    m_activeScreen = screenId;

    bool focusNew = true;
    if (auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        focusNew = settings->scrollingFocusNewWindows();
    }
    if (!focusNew && !priorActive.isEmpty() && state->strip().activeWindowId() == windowId
        && state->strip().containsWindow(priorActive)) {
        const ScrollLayoutParams params = layoutParamsForScreen(screenId);
        state->strip().focusWindow(priorActive, params);
    }
    applyLayout(screenId, focusNew && state->strip().activeWindowId() == windowId);
    Q_EMIT placementChanged(screenId);
}

void ScrollEngine::windowClosed(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    const bool wasActive = state->strip().activeWindowId() == windowId;
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const bool inStrip = state->strip().removeWindow(windowId, params);
    if (!inStrip) {
        state->removeFloating(windowId);
    }
    m_states.removeWindow(windowId);
    // m_lastAppliedRect is deliberately RETAINED through the close: the
    // daemon's close capture consults lastManagedRect DURING windowClosed
    // (the engines hear the close before WindowTracking does), and the
    // live frame is still the strip rect at that moment — without the
    // memory, the column rect becomes the reopen float-back geometry (the
    // float-back tile-rect poison, autotile's twin retains for the same
    // reason). pruneStaleWindows reclaims the entry independently.
    m_floatRestore.remove(windowId);
    m_scrollFloatedWindows.remove(windowId);

    if (inStrip && key == currentKeyForScreen(key.screenId)) {
        // Background-context guard: applyLayout resolves the screen's
        // CURRENT context, so a close on another desktop's state would
        // relayout the wrong strip (the mutated one must stay silent
        // until its desktop returns).
        applyLayout(key.screenId, wasActive && !state->strip().activeWindowId().isEmpty());
    }
    Q_EMIT placementChanged(key.screenId);
}

void ScrollEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    if (state->strip().focusWindow(windowId, params)) {
        // The focus change may scroll the viewport; never re-activate here
        // (the compositor initiated this focus). Background-context guard:
        // see windowClosed.
        if (key == currentKeyForScreen(key.screenId)) {
            applyLayout(key.screenId, false);
        }
    }
}

QSize ScrollEngine::windowMinimumSize(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    const ScrollState* state = stateForWindow(windowId);
    if (!state) {
        return {};
    }
    return state->strip().windowMinimumSize(windowId);
}

void ScrollEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    if (state->strip().setWindowMinimumSize(windowId, minWidth, minHeight)) {
        scheduleRetileForScreen(key.screenId);
    }
}

void ScrollEngine::onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                                   const QString& screenId)
{
    Q_UNUSED(oldFrame)
    // key.screenId is authoritative below; a mismatched caller value would
    // retile the wrong strip.
    Q_UNUSED(screenId)
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    // Reconcile the column to the size the client/user actually settled on;
    // only the owning column relayouts (a resize never reflows neighbours'
    // widths — they just shift). Width intent is only rewritten when the
    // WIDTH moved relative to the last applied rect — a vertical-only
    // resize must not pin a Proportion/Preset column to pixels.
    const QRect lastApplied = m_lastAppliedRect.value(windowId);
    const bool widthChanged = !lastApplied.isValid() || lastApplied.width() != newFrame.width();
    const bool heightChanged = !lastApplied.isValid() || lastApplied.height() != newFrame.height();
    if (state->strip().reconcileWindowSize(windowId, newFrame.size(), widthChanged, heightChanged)) {
        scheduleRetileForScreen(key.screenId);
        return;
    }
    // The strip REFUSED the size (no-op ack): the window
    // is now displaced from the engine's rect, but m_lastAppliedRect still
    // holds it, so the emit-on-change gate would treat the corrective
    // relayout as "nothing moved" and never re-issue the rect. Drop the
    // memory and retile so the authoritative geometry is re-applied.
    if (lastApplied.isValid() && lastApplied != newFrame) {
        m_lastAppliedRect.remove(windowId);
        scheduleRetileForScreen(key.screenId);
    }
}

// ── Float management ────────────────────────────────────────────────────────

bool ScrollEngine::floatWindowInternal(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                                       const QString& windowId, const QString& screenId)
{
    if (state->isFloating(windowId)) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const int columnIdx = state->strip().columnOfWindow(windowId);
    if (columnIdx < 0) {
        // Tracked in the reverse map yet in neither the strip nor the
        // floating set — a genuine bookkeeping inconsistency, not a
        // documented no-op like the other bails in this file.
        qCWarning(lcScrollEngine) << "floatWindowInternal:" << windowId
                                  << "tracked but absent from strip and floating set on" << key.screenId;
        return false;
    }
    FloatRestore restore;
    restore.column = columnIdx;
    const Column& sourceColumn = state->strip().columns().at(columnIdx);
    restore.width = sourceColumn.width;
    restore.display = sourceColumn.display;
    const QSize minSize = state->strip().windowMinimumSize(windowId);
    restore.minWidth = minSize.width();
    restore.minHeight = minSize.height();
    if (sourceColumn.tiles.size() > 1) {
        restore.tileIndex = sourceColumn.indexOfWindow(windowId);
        // Anchor on a surviving sibling so the stack can be re-located even
        // after column indices shift (prefer the neighbour above, else below).
        for (int i = restore.tileIndex - 1; i >= 0 && restore.stackAnchor.isEmpty(); --i) {
            restore.stackAnchor = sourceColumn.tiles.at(i).windowId;
        }
        for (int i = restore.tileIndex + 1; i < sourceColumn.tiles.size() && restore.stackAnchor.isEmpty(); ++i) {
            restore.stackAnchor = sourceColumn.tiles.at(i).windowId;
        }
    }
    state->strip().takeWindow(windowId, params);
    state->addFloating(windowId);
    m_floatRestore.insert(windowId, restore);
    m_scrollFloatedWindows.insert(windowId);
    m_lastAppliedRect.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, true, screenId.isEmpty() ? key.screenId : screenId);
    // Background-context guard: see windowClosed.
    if (key == currentKeyForScreen(key.screenId)) {
        applyLayout(key.screenId, false);
    }
    Q_EMIT placementChanged(key.screenId);
    return true;
}

bool ScrollEngine::unfloatWindowInternal(ScrollState* state, const QString& windowId, const QString& screenId,
                                         bool applyAfter)
{
    if (!state->removeFloating(windowId)) {
        return false;
    }
    // Captured before the re-insert so the guard at the tail reads the
    // context the window actually belongs to.
    const PhosphorEngine::PlacementStateKey key = m_states.keyForWindow(windowId);
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    // Restore the remembered column slot (minimize/unminimize and float
    // round-trips keep their place); fall back to next-to-focus.
    const bool hadSlot = m_floatRestore.contains(windowId);
    const FloatRestore restore = m_floatRestore.take(windowId);
    bool inserted = false;
    if (hadSlot && restore.tileIndex >= 0) {
        // The window left a SHARED column: return to the surviving stack.
        // The stack is re-located through the surviving-sibling anchor —
        // the remembered index goes stale when columns close during the
        // float and would splice into a stranger's stack. Anchor gone →
        // fall through to a fresh column.
        const int anchoredColumn =
            restore.stackAnchor.isEmpty() ? -1 : state->strip().columnOfWindow(restore.stackAnchor);
        if (anchoredColumn >= 0) {
            inserted = state->strip().insertWindowIntoColumnAt(anchoredColumn, restore.tileIndex, windowId, params,
                                                               restore.minWidth, restore.minHeight);
        }
    }
    if (!inserted && hadSlot) {
        inserted = state->strip().insertWindowAt(restore.column, windowId, restore.width, restore.display, params);
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, effectiveDefaultColumnWidth(screenId),
                                               effectiveDefaultColumnDisplay(screenId), params);
    }
    if (inserted) {
        // Re-apply the min size the floated tile carried (the fresh-column
        // branches insert without it).
        if (restore.minWidth > 0 || restore.minHeight > 0) {
            state->strip().setWindowMinimumSize(windowId, restore.minWidth, restore.minHeight);
        }
        state->strip().focusWindow(windowId, params);
    }
    m_scrollFloatedWindows.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, false, screenId);
    // Batch callers (snapAllWindows) relayout once for the whole batch.
    if (applyAfter) {
        // Background-context guard, same terms as floatWindowInternal:
        // applyLayout resolves the screen's CURRENT context, so an unfloat on
        // another desktop's state would relayout the wrong strip. The
        // placement announcement still fires — the managed set did change.
        if (key == currentKeyForScreen(screenId)) {
            applyLayout(screenId, false);
        }
        Q_EMIT placementChanged(screenId);
    }
    return inserted;
}

void ScrollEngine::setWindowFloat(const QString& rawWindowId, bool shouldFloat, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);

    if (!state) {
        if (shouldFloat) {
            return; // never tracked — nothing to pull out of a strip
        }
        // Unfloat a window the engine has not adopted yet (e.g. floated
        // before the screen switched to scrolling): adopt into the
        // authoritative screen's current strip.
        const QString targetScreen = resolveOperationScreen(screenId);
        if (targetScreen.isEmpty()) {
            return;
        }
        ScrollState* target = stateForKey(currentKeyForScreen(targetScreen), true);
        if (!target || target->containsWindow(windowId)) {
            return;
        }
        const ScrollLayoutParams params = layoutParamsForScreen(targetScreen);
        // Same as handoffReceive: the retained close/release rect only has to
        // outlive the capture window, and carrying it into a re-adoption would
        // gate away the first windowsTiled batch for this window.
        m_lastAppliedRect.remove(windowId);
        if (target->strip().insertWindow(windowId, effectiveDefaultColumnWidth(targetScreen),
                                         effectiveDefaultColumnDisplay(targetScreen), params)) {
            // Third unfloat route: clear the mode-transition float marker
            // like unfloatWindowInternal/handoffRelease do.
            m_scrollFloatedWindows.remove(windowId);
            m_states.setKeyForWindow(windowId, currentKeyForScreen(targetScreen));
            Q_EMIT windowFloatingChanged(windowId, false, targetScreen);
            applyLayout(targetScreen, false);
            Q_EMIT placementChanged(targetScreen);
        }
        return;
    }

    if (shouldFloat) {
        floatWindowInternal(state, key, windowId, screenId);
    } else {
        unfloatWindowInternal(state, windowId, key.screenId);
    }
}

void ScrollEngine::toggleWindowFloat(const QString& rawWindowId, const QString& screenId)
{
    // setWindowFloat canonicalizes its own input; canonicalizeForLookup is
    // idempotent, so passing the resolved id through is a single-pass
    // pipeline, not a second translation.
    const QString windowId = canonicalizeForLookup(rawWindowId);
    ScrollState* state = stateForWindow(windowId);
    const bool floating = state && state->isFloating(windowId);
    setWindowFloat(windowId, !floating, screenId);
}

// ── Cross-engine handoff ────────────────────────────────────────────────────

void ScrollEngine::handoffRelease(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
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
    // m_lastAppliedRect deliberately retained (same rationale as
    // windowClosed: a close/capture racing the handoff still needs the
    // poison-guard memory; pruneStaleWindows reclaims it).
    m_floatRestore.remove(windowId);
    // The mode-transition float marker must not outlive this engine's
    // tracking: the receiving engine owns the float bit from here, and a
    // stale entry would keep isModeSpecificFloated answering true.
    m_scrollFloatedWindows.remove(windowId);
    scheduleRetileForScreen(key.screenId);
}

void ScrollEngine::handoffReceive(const HandoffContext& ctx)
{
    const QString windowId = canonicalizeForLookup(ctx.windowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(ctx.toScreenId)) {
        return;
    }
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
    if (ScrollState* staleState = stateForWindow(windowId, &staleKey); staleState && staleKey != key) {
        const ScrollLayoutParams staleParams = layoutParamsForScreen(staleKey.screenId);
        const bool staleWasFloating = staleState->isFloating(windowId);
        staleState->strip().takeWindow(windowId, staleParams);
        staleState->removeFloating(windowId);
        m_lastAppliedRect.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        if (staleWasFloating) {
            // Same announcement as windowOpened's migration: a silently
            // dropped float bit leaves signal-driven subscribers believing
            // the window floats while the receive tiles it (the
            // wasFloating branch below re-announces true when it applies).
            Q_EMIT windowFloatingChanged(windowId, false, staleKey.screenId);
        }
        scheduleRetileForScreen(staleKey.screenId);
        Q_EMIT placementChanged(staleKey.screenId);
    }
    ScrollState* state = stateForKey(key, true);
    if (!state || state->containsWindow(windowId)) {
        return;
    }
    // Re-adoption starts from a blank rect memory: handoffRelease/windowClosed
    // only retain m_lastAppliedRect long enough to survive the close/capture
    // window, and a leftover entry would defeat applyLayout's emit-on-change
    // gate so no windowsTiled batch ever fires for the re-adopted window.
    m_lastAppliedRect.remove(windowId);
    if (ctx.wasFloating) {
        state->addFloating(windowId);
        // The float is scroll-managed from here (autotile's receive marks the
        // same way, through the daemon's passive float sync): without the
        // marker a later mode transition treats it as a snap float and
        // poisons the snap slot with the arrival frame.
        m_scrollFloatedWindows.insert(windowId);
        m_states.setKeyForWindow(windowId, key);
        Q_EMIT windowFloatingChanged(windowId, true, ctx.toScreenId);
        // The screen's placement changed too (managed set grew), even
        // though no strip geometry moved.
        Q_EMIT placementChanged(ctx.toScreenId);
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(ctx.toScreenId);
    ColumnWidth width = effectiveDefaultColumnWidth(ctx.toScreenId);
    if (ctx.sourceGeometry.isValid()) {
        width = ColumnWidth::makeFixed(ctx.sourceGeometry.width());
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
        if (ctx.minSize.width() > 0 || ctx.minSize.height() > 0) {
            state->strip().setWindowMinimumSize(windowId, ctx.minSize.width(), ctx.minSize.height());
        }
        m_states.setKeyForWindow(windowId, key);
        const bool isCurrentContext = key == currentKeyForScreen(ctx.toScreenId);
        if (isCurrentContext) {
            state->strip().focusWindow(windowId, params);
            applyLayout(ctx.toScreenId, false);
        }
        Q_EMIT placementChanged(ctx.toScreenId);
    }
}

// ── Unified placement capture ───────────────────────────────────────────────

std::optional<PhosphorEngine::WindowPlacement> ScrollEngine::capturePlacement(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    const ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return std::nullopt;
    }
    PhosphorEngine::WindowPlacement placement;
    placement.windowId = windowId;
    placement.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
    placement.screenId = key.screenId;
    placement.virtualDesktop = key.desktop;
    placement.activity = key.activity;

    PhosphorEngine::EngineSlot slot;
    if (state->isFloating(windowId)) {
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
    } else {
        slot.state = PhosphorEngine::WindowPlacement::stateTiled();
        // COLUMN index, not window index: the restore path feeds slot.order
        // to insertWindowAt(), which takes a column position. The two only
        // coincide while every column is single-tile — a stacked column
        // would shift every later window's restore slot.
        slot.order = state->strip().columnOfWindow(windowId);
    }
    placement.engines.insert(engineId(), slot);
    return placement;
}

} // namespace PhosphorScrollEngine
