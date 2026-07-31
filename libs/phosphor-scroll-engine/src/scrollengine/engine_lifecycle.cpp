// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

void ScrollEngine::seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight)
{
    // windowMinimumSize reads the clamp out of this entry for every window
    // that is floated rather than tiled, and the cross-engine handoff queries
    // it whatever state the window is in: with no entry the answer is the
    // "unknown" one, and the receiving engine gets an unclamped window. That
    // bites hardest on exactly the windows these paths float — the oversized
    // ones, whose clamp is why they could not take a column in the first
    // place.
    const auto existing = m_floatRestore.find(windowId);
    if (existing != m_floatRestore.end()) {
        // A real remembered slot is worth more than a slotless seed; only
        // the clamp is refreshed.
        existing->minWidth = qMax(0, minWidth);
        existing->minHeight = qMax(0, minHeight);
        return;
    }
    FloatRestore restore;
    restore.column = -1; // no slot to go back to; unfloat opens a fresh column
    restore.minWidth = qMax(0, minWidth);
    restore.minHeight = qMax(0, minHeight);
    m_floatRestore.insert(windowId, restore);
}

bool ScrollEngine::insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId,
                                      int minWidth, int minHeight)
{
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);

    // Fixed-size / oversized windows cannot honour a column slot: float them
    // at their native size instead of forcing a tile (the min size is the
    // only constraint the compositor reports; a rule `float` action covers
    // the rest).
    const bool oversized =
        params.workArea.isValid() && (minWidth > params.workArea.width() || minHeight > params.workArea.height());
    const bool ruleFloated = m_floatPredicate && m_floatPredicate(windowId, screenId);
    // Sticky handling gates insertion only: RestoreOnly and IgnoreAll both
    // keep sticky windows out of the strip, because insertion is active
    // management (autotile's shouldTileWindow makes the same collapse). The
    // desktop-pin logic in updateStickyScreenPins stays unconditional — with
    // sticky windows floated, the all-sticky managed set never forms and the
    // pin degrades correctly on its own.
    const bool stickyExcluded = m_stickyWindowHandling != PhosphorEngine::StickyWindowHandling::TreatAsNormal
        && m_windowTracker && m_windowTracker->isWindowSticky(windowId);
    if (oversized || ruleFloated || stickyExcluded) {
        state->addFloating(windowId);
        seedFloatRestoreForOpen(windowId, minWidth, minHeight);
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
        Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
        return true;
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
                seedFloatRestoreForOpen(windowId, minWidth, minHeight);
                // The record's SCROLL slot says floating, so this float is
                // this engine's own — marked like the rule-float exit above.
                m_scrollFloatedWindows.insert(windowId);
                consumePendingInitialOrder(screenId, windowId); // same rationale as the rule-float exit
                // The window is marked floating unconditionally above; only
                // the geometry MOVE onto the recorded free spot is gated
                // (daemon-wired scrollingRestoreFloatedWindowsOnLogin
                // setting + per-window RestorePosition rule) — the autotile
                // shape, insert.cpp. SCREEN-LOCAL recorded position only,
                // for autotile's documented reason: a rect captured on a
                // different screen would teleport the window while the
                // float tracking points elsewhere.
                const QString restoreScreen = record->screenId.isEmpty() ? screenId : record->screenId;
                const QRect freeGeo = record->freeGeometryFor(restoreScreen);
                const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
                if (freeGeo.isValid() && restorePosition) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                }
                Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
                return true;
            }
            if (slot.state == PhosphorEngine::WindowPlacement::stateTiled() && slot.order >= 0) {
                restoreColumn = slot.order;
            }
        }
    }

    ColumnWidth width = effectiveDefaultColumnWidth(screenId);
    ColumnDisplay display = effectiveDefaultColumnDisplay(screenId);
    // "Client decides" is the CONFIG default, so a per-screen rule override
    // outranks it — the header documents these overrides as layering over the
    // config defaults. Overwriting unconditionally meant a
    // SetScrollDefaultColumnWidth rule pinned to a screen never took effect
    // while the global kind was ClientDecides, with no diagnostic. (The
    // per-WINDOW open rule below is applied after this block and wins over
    // both, which is the intended precedence.)
    // Any width key counts — the rule channel's bare fraction OR the
    // settings channel's kind trio; a per-screen kind=Preset would otherwise
    // be silently overridden by the global ClientDecides.
    const QVariantMap screenOverrides = m_perScreenOverrides.value(screenId);
    const bool screenPinsWidth = screenOverrides.contains(ScrollPerScreenKeys::defaultColumnWidth())
        || screenOverrides.contains(ScrollPerScreenKeys::defaultColumnWidthKind());
    if (m_defaultWidthClientDecides && m_windowTracker && !screenPinsWidth) {
        // Open at the client's own size when one is on record; the first
        // client resize reconciles it afterwards.
        if (const auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId)) {
            width = ColumnWidth::makeFixed(geo->width());
        }
    }

    // Per-window open rules layer over the context/config defaults.
    ScrollOpenParams openParams;
    if (m_openParamsResolver) {
        openParams = m_openParamsResolver(windowId, screenId);
    }
    if (openParams.widthFraction) {
        width = ColumnWidth::makeProportion(qBound<qreal>(MinColumnWidthFraction, *openParams.widthFraction, 1.0));
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
            return true;
        }
    }

    // Mode-round-trip structure restore FIRST: a strip stashed at the last
    // reassignment away from Scrolling rebuilds exactly (stacks, widths,
    // display, heights), which is strictly stronger than the order seed's
    // position-only verdict. The seed entry is still consumed so it cannot
    // linger past the adoption.
    bool inserted = false;
    if (restoreFromStripStash(state, currentKeyForScreen(screenId), windowId, params, minWidth, minHeight)) {
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
        // Fresh open with no remembered position: the ONLY site the
        // insert-position setting governs. Restore/seed/unfloat paths above
        // and the re-homing call sites elsewhere keep right-of-active — a
        // "first/last" default teleporting a restored window would read as
        // a lost slot. IntoActiveColumn routes through the consume verb
        // (same shape as the openColumnPlacement rule) and falls through to
        // a positional insert on an empty strip.
        const ScrollInsertPosition insertPos = effectiveInsertPosition(screenId);
        if (insertPos == ScrollInsertPosition::IntoActiveColumn && !state->strip().isEmpty()) {
            inserted =
                state->strip().insertWindowIntoActiveColumn(windowId, width, std::nullopt, params, minWidth, minHeight);
        }
        if (!inserted) {
            inserted = state->strip().insertWindow(
                windowId, width, display, params, minWidth, minHeight,
                insertPos == ScrollInsertPosition::IntoActiveColumn ? ScrollInsertPosition::RightOfActive : insertPos);
        }
    }
    if (inserted && openParams.heightFraction && params.workArea.height() > 0) {
        // Per-window open rule wins over every default and remembered
        // height, matching the width/tabbed precedence above. Committed as
        // Fixed pixels against the live work area, the same resolution the
        // adjust verbs use.
        const qreal fraction = qBound<qreal>(0.05, *openParams.heightFraction, 1.0);
        state->strip().setWindowHeightIntent(
            windowId, WindowHeight::makeFixed(qMax(1, qRound(fraction * params.workArea.height()))));
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
    return inserted;
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
        // Re-announce of a window we already track here. Still an arrival as
        // far as the mode-transition seed is concerned: the header's
        // "consumed on EVERY outcome" invariant has no exception for this
        // one, and skipping it leaves the screen's seed list unable to empty,
        // so it survives to re-position an unrelated later open. No-op when
        // the screen carries no seed, which is the usual case here.
        consumePendingInitialOrder(screenId, windowId);
        return;
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
            Q_EMIT windowFloatingStateSynced(windowId, false, oldKey.screenId);
        }
        scheduleRetileForScreen(oldKey.screenId);
        Q_EMIT placementChanged(oldKey.screenId);
    }

    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    // Track BEFORE inserting: insertOpenedWindow's oversized/rule-float
    // paths emit windowFloatingStateSynced, and a synchronous query-back from
    // a subscriber must already see the window as this engine's.
    m_states.setKeyForWindow(windowId, key);
    // Capture the pre-insert focus: with focus-new-windows OFF the
    // compositor keeps focus on the previous window, so the strip must not
    // adopt the arrival as its active column either — a diverged strip
    // makes every later focus-direction verb navigate from the wrong
    // origin, and a consume-open into a tabbed column would park the
    // window the user is actually looking at.
    const QString priorActive = state->strip().activeWindowId();
    // Re-adoption starts from a blank rect memory, unconditionally. The close
    // and release paths deliberately RETAIN m_lastAppliedRect (the daemon's
    // close capture reads it as the float-back poison guard), and this is the
    // second of its two reclaimers — pruneStaleWindows is the other, and it
    // only fires on aliveness. Left standing, a retained rect that happens to
    // equal the one the strip resolves defeats applyLayout's emit-on-change
    // gate, so no windowsTiled batch ever fires for the re-adopted window and
    // a single-window screen sits at the geometry the OTHER mode left it in.
    m_lastAppliedRect.remove(windowId);
    if (!insertOpenedWindow(state, windowId, screenId, minWidth, minHeight)) {
        // Every insert refused (the strip already holds the window). Nothing
        // moved and nothing was adopted, so neither the geometry batch nor
        // the dirty mark may fire.
        return;
    }

    bool focusNew = true;
    if (auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        focusNew = settings->scrollingFocusNewWindows();
    }
    const bool arrivalTookFocus = focusNew && state->strip().activeWindowId() == windowId;
    if (!focusNew && !priorActive.isEmpty() && state->strip().activeWindowId() == windowId
        && state->strip().containsWindow(priorActive)) {
        const ScrollLayoutParams params = layoutParamsForScreen(screenId);
        state->strip().focusWindow(priorActive, params);
    }
    // Only an arrival that actually TAKES focus re-targets the screen-hintless
    // shortcut paths. Writing this on ANY arrival pointed them at whatever
    // monitor a background app last opened a window on — including floated
    // opens and opens under focus-new-windows OFF, neither of which the user
    // is looking at. Focus events own the value the rest of the time.
    if (arrivalTookFocus) {
        m_activeScreen = screenId;
    }
    applyLayout(screenId, arrivalTookFocus);
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
    // Unconditional, not gated on the strip removal failing: the two sets are
    // meant to be disjoint, but a window that somehow sits in BOTH would keep
    // its floating entry forever under the gated form — nothing else ever
    // revisits a closed window's floating membership. A no-op for the normal
    // case, which is the whole point.
    state->removeFloating(windowId);
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
        // Focus and view anchor are persisted (serializeStripState), and
        // placementChanged is the only thing that marks DirtyScrollStrips.
        // Emitted for a background context too: the strip that changed is
        // serialized whether or not it is the one on screen right now.
        Q_EMIT placementChanged(key.screenId);
    }
}

QSize ScrollEngine::windowMinimumSize(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (const ScrollState* state = stateForWindow(windowId)) {
        if (state->strip().containsWindow(windowId)) {
            // Verbatim, including one-axis clamps like 900x0: the strip
            // answers (0, 0) only for a window it does not hold.
            return state->strip().windowMinimumSize(windowId);
        }
    }
    // A floated (or, via the effect's minimize-as-float model, minimized)
    // window is not a strip tile, but its clamp is not unknown — the
    // FloatRestore entry carries it. The cross-engine handoff queries this
    // whatever state the window is in, and answering 0x0 hands the receiving
    // engine an unclamped window.
    const auto it = m_floatRestore.constFind(windowId);
    // Unknown window: an INVALID QSize, deliberately, and a divergence from
    // the sibling engines — AutotileEngine answers 0x0 for an unknown window,
    // which is also what an unconstrained known window answers. The two cases
    // are not the same thing here: the handoff asks this whatever state the
    // window is in, and "I have never heard of it" has to be distinguishable
    // from "it reported no minimum", or a receiving engine cannot tell a real
    // 0x0 clamp from a missing answer. Callers that just want a clamp can
    // treat both alike, since an invalid QSize's width/height are -1 and every
    // clamp site takes a qMax against 0.
    return it != m_floatRestore.constEnd() ? QSize(it->minWidth, it->minHeight) : QSize();
}

void ScrollEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // While the window floats there is no tile to write to, and unfloat
    // re-applies the captured clamp — so without this write-through the
    // restore puts back whatever the client reported at float time.
    if (const auto it = m_floatRestore.find(windowId); it != m_floatRestore.end()) {
        it->minWidth = minWidth;
        it->minHeight = minHeight;
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    // Background-context guard, the same one windowClosed and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // min-size report for a window on another desktop would relayout a strip
    // this change did not touch. The model write still lands; the switch back
    // retiles the mutated strip.
    if (state->strip().setWindowMinimumSize(windowId, minWidth, minHeight)
        && key == currentKeyForScreen(key.screenId)) {
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
    // Background-context guard, as windowMinSizeUpdated and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // resize of a window on another desktop must not drive one. The model
    // reconcile still happens — it is the persisted intent — and the switch
    // back retiles.
    const bool currentContext = key == currentKeyForScreen(key.screenId);
    // Reconcile the column to the size the client/user actually settled on;
    // only the owning column relayouts (a resize never reflows neighbours'
    // widths — they just shift). Width intent is only rewritten when the
    // WIDTH moved relative to the last applied rect — a vertical-only
    // resize must not pin a Proportion/Preset column to pixels.
    //
    // With NO last-applied rect there is no baseline to compare against, and
    // treating that as "both changed" pinned BOTH intents to pixels — so a
    // purely vertical resize arriving in the window between an adoption
    // (handoffReceive, the setWindowFloat adoption branch, floatWindowInternal)
    // and its scheduled applyLayout converted a Proportion column to Fixed,
    // which is exactly what the widthChanged gate exists to prevent. Reconcile
    // nothing in that case and let the pending relayout establish the baseline.
    const QRect lastApplied = m_lastAppliedRect.value(windowId);
    if (!lastApplied.isValid()) {
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    const bool widthChanged = lastApplied.width() != newFrame.width();
    const bool heightChanged = lastApplied.height() != newFrame.height();
    if (state->strip().reconcileWindowSize(windowId, newFrame.size(), widthChanged, heightChanged)) {
        // The reconcile WROTE persisted intent (the column's Fixed width, the
        // tile's Fixed height — both serialized by serializeStripState), and
        // placementChanged is the sole producer of DirtyScrollStrips. Without
        // this emit a resize that is the session's last strip interaction is
        // never saved and the column comes back at its old width.
        // reconcileWindowSize returns true only on a genuine change, so
        // emit-on-change holds.
        Q_EMIT placementChanged(key.screenId);
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    // The strip REFUSED the size (no-op ack): the window
    // is now displaced from the engine's rect, but m_lastAppliedRect still
    // holds it, so the emit-on-change gate would treat the corrective
    // relayout as "nothing moved" and never re-issue the rect. Drop the
    // memory and retile so the authoritative geometry is re-applied.
    if (lastApplied.isValid() && lastApplied != newFrame) {
        m_lastAppliedRect.remove(windowId);
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
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
    // The height intent dies with the tile too, and the minimize path rides
    // this same round trip — without it a minimize/restore silently reset a
    // user-set window height to Auto.
    const int tileIdx = sourceColumn.indexOfWindow(windowId);
    if (tileIdx >= 0) {
        restore.height = sourceColumn.tiles.at(tileIdx).height;
    }
    if (sourceColumn.tiles.size() > 1) {
        restore.tileIndex = tileIdx;
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
    if (!inserted && hadSlot && restore.column >= 0) {
        // column >= 0 keeps a SEEDED slot (seedFloatRestoreForOpen writes
        // column = -1 for a window floated at open, which never held a strip
        // position) out of this arm — insertWindowAt would qBound -1 to the
        // leftmost column and stamp the record's default width/display over
        // the configured defaults. A slotless seed falls through to the plain
        // next-to-focus insert below.
        inserted = state->strip().insertWindowAt(restore.column, windowId, restore.width, restore.display, params);
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, effectiveDefaultColumnWidth(screenId),
                                               effectiveDefaultColumnDisplay(screenId), params);
    }
    if (!inserted) {
        // Every insert refused (an empty id is the only way today). The float
        // set was already given up above, so returning now would leave the
        // window tracked but in NEITHER the strip nor the floating set — the
        // inconsistency floatWindowInternal warns about. Put it back.
        qCWarning(lcScrollEngine) << "unfloatWindowInternal: every insert refused for" << windowId
                                  << "— restoring floating state";
        state->addFloating(windowId);
        if (hadSlot) {
            m_floatRestore.insert(windowId, restore);
        }
        return false;
    }
    {
        // `inserted` is necessarily true here — the !inserted arm above
        // returns — so no second guard.
        //
        // Re-apply the min size the floated tile carried (the fresh-column
        // branches insert without it).
        if (restore.minWidth > 0 || restore.minHeight > 0) {
            state->strip().setWindowMinimumSize(windowId, restore.minWidth, restore.minHeight);
        }
        // Same for the height intent: every insert path builds a default
        // (Auto) tile, so the user's height only survives if it is re-applied.
        state->strip().setWindowHeightIntent(windowId, restore.height);
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
        // Whatever clamp is on record for the window while it floats — the
        // seed a float-at-open left, or a live windowMinSizeUpdated
        // write-through. This route inserts without min sizes, so without the
        // re-apply the adopted tile relayouts unclamped until the client
        // happens to re-report, which for a fixed-size window is never.
        const FloatRestore adopted = m_floatRestore.value(windowId);
        if (target->strip().insertWindow(windowId, effectiveDefaultColumnWidth(targetScreen),
                                         effectiveDefaultColumnDisplay(targetScreen), params, adopted.minWidth,
                                         adopted.minHeight)) {
            // The tile owns the clamp from here; a refused insert keeps the
            // entry so a later attempt still has it.
            m_floatRestore.remove(windowId);
            // Third unfloat route: clear the mode-transition float marker
            // like unfloatWindowInternal/handoffRelease do.
            m_scrollFloatedWindows.remove(windowId);
            m_states.setKeyForWindow(windowId, currentKeyForScreen(targetScreen));
            Q_EMIT windowFloatingStateSynced(windowId, false, targetScreen);
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
            Q_EMIT windowFloatingStateSynced(windowId, false, staleKey.screenId);
        }
        scheduleRetileForScreen(staleKey.screenId);
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
        return;
    }
    // Re-adoption starts from a blank rect memory: handoffRelease/windowClosed
    // only retain m_lastAppliedRect long enough to survive the close/capture
    // window, and a leftover entry would defeat applyLayout's emit-on-change
    // gate so no windowsTiled batch ever fires for the re-adopted window.
    m_lastAppliedRect.remove(windowId);
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
        Q_EMIT windowFloatingStateSynced(windowId, true, ctx.toScreenId);
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
        return;
    }
    // The insert refused (an empty id, or a window this strip already holds —
    // both ruled out above, so this is a real inconsistency). Every sibling
    // insert site logs its refusal; a silent one here leaves the window
    // released by the source engine and adopted by nobody, with no trace.
    qCWarning(lcScrollEngine) << "handoffReceive: insert refused for" << windowId << "on" << ctx.toScreenId
                              << "— the window is released by its source engine and unadopted here";
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
