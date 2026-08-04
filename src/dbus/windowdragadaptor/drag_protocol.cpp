// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Daemon-authoritative beginDrag / endDrag protocol. Replaces the
// effect-side m_dragBypassedForAutotile / m_cachedZoneSelectorEnabled
// distributed state with a single source of truth on the daemon side.
//
// The legacy dragStarted / dragMoved / dragStopped methods are kept as
// internal C++ helpers (called from this file) so the intricate snap-path
// state machine doesn't need to be rewritten. They're no longer exposed
// on the D-Bus surface.

#include "windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "core/interfaces/settings_interfaces.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <QGuiApplication>
#include <QTimer>

namespace PlasmaZones {

PhosphorProtocol::DragPolicy
WindowDragAdaptor::computeDragPolicy(const ISettings* settings, const PhosphorEngine::IPlacementEngine* autotileEngine,
                                     const PhosphorEngine::IPlacementEngine* scrollEngine, const QString& windowId,
                                     const QString& screenId, const PhosphorContext::IContextResolver* resolver,
                                     bool reorderMode, bool activeLayoutSuppressed)
{
    PhosphorProtocol::DragPolicy policy;
    policy.screenId = screenId;

    // Order matters — strongest disables checked first so the reason string
    // is stable regardless of which conditions coincide.

    // 1) Disabled context (activity / desktop / monitor excluded in settings).
    //    Dead drag: no overlay, no cursor stream, no float transition.
    //
    //    Use `handleFor(screenId)` (live mode) — NOT `handleForMode(..., Snapping)`.
    //    The disable lists are per-mode, so the right question is "is THIS
    //    screen's current mode disabled?", not "is Snapping disabled here?".
    //    Hard-coding Snapping would have routed an autotile-mode screen with
    //    Snapping-disabled-but-Autotile-enabled into the dead-drag branch
    //    instead of the EngineOwnedScreen bypass below, hiding the autotile
    //    placement behaviour the user actually configured.
    if (resolver && !screenId.isEmpty() && resolver->isDisabled(resolver->handleFor(screenId))) {
        policy.bypassReason = PhosphorProtocol::DragBypassReason::ContextDisabled;
        return policy;
    }

    // 2) Autotile screen — the autotile engine owns window placement. The
    //    plugin applies handleDragToFloat immediately if the window is
    //    currently tiled, so the user sees the free-floating size restored
    //    during the interactive move (not deferred to drop).
    //
    //    In Reorder mode (Krohnkite-style drag-to-swap), the plugin must
    //    NOT float the window on drag-start — the daemon tracks the cursor
    //    across calculated zones via drag-insert preview and reorders the
    //    window within its stack on drop instead. Clear immediateFloatOnStart
    //    so both the effect fast path and its async reply handler skip the
    //    handleDragToFloat call.
    if (autotileEngine && !screenId.isEmpty() && autotileEngine->isActiveOnScreen(screenId)) {
        policy.bypassReason = PhosphorProtocol::DragBypassReason::EngineOwnedScreen;
        policy.captureGeometry = true; // preserve pre-autotile size for unfloat restore
        // reorderMode is resolved by the caller (effectiveReorderMode): a matched
        // context SetDragBehavior rule wins over the global setting for this screen.
        if (!windowId.isEmpty() && !reorderMode) {
            policy.immediateFloatOnStart = autotileEngine->isWindowTracked(windowId);
        }
        return policy;
    }

    // 2b) Scrolling screen — same engine-owns-placement bypass as autotile
    //     (the wire reason is shared; the effect treats it as "engine-managed
    //     screen"). Without this branch a drag on a scrolling screen fell
    //     through to the SNAP pipeline — cursor streaming, keyboard grab,
    //     zone detection, and an ApplySnap drop against a snap layout the
    //     screen does not run.
    //
    //     reorderMode here is scrolling's "always re-insert" (the
    //     AlwaysActive sentinel in its drag-insert trigger list, resolved by
    //     the caller via effectiveDragReorderModeFor): the strip runs a
    //     drag-insert preview, so the effect must not float the tile being
    //     reordered — same contract as the autotile branch above. Otherwise
    //     a tracked window floats immediately: drag-to-float, reposition,
    //     and a held trigger re-inserts (or unfloat restores the column
    //     slot).
    if (scrollEngine && !screenId.isEmpty() && scrollEngine->isActiveOnScreen(screenId)) {
        policy.bypassReason = PhosphorProtocol::DragBypassReason::EngineOwnedScreen;
        policy.captureGeometry = true;
        if (!windowId.isEmpty() && !reorderMode) {
            policy.immediateFloatOnStart = scrollEngine->isWindowTracked(windowId);
        }
        return policy;
    }

    // 3) Top-level snapping disabled. Dead drag on any non-autotile screen —
    //    the user configured the tool with snap mode off. beginDrag still
    //    returns a policy so endDrag can clean up consistently.
    if (settings && !settings->snappingEnabled()) {
        policy.bypassReason = PhosphorProtocol::DragBypassReason::SnappingDisabled;
        return policy;
    }

    // 4) No active zone layout on this screen: the default-layout assignment
    //    is suppressed for this context (SuppressDefaultLayoutAssignment, or a
    //    per-context override). Dead drag — the drag path's per-tick layout
    //    resolution (resolveLayoutForScreen) falls back to the GLOBAL default
    //    layout for an unassigned screen, so without this gate a drag on such
    //    a screen hit-tests and snaps into zones the screen was never
    //    assigned, while the overlay layer (which consults the suppress-aware
    //    predicate) correctly shows nothing there (#724).
    if (activeLayoutSuppressed) {
        policy.bypassReason = PhosphorProtocol::DragBypassReason::LayoutSuppressed;
        return policy;
    }

    // 5) Snap path — canonical case. Plugin streams cursor updates, daemon
    //    runs overlay / zone detection / snap assist. Activation-trigger
    //    gating still happens locally in the plugin against the current
    //    modifier state (input-event optimization, not policy).
    //    An EMPTY screenId lands here BY INTENT: every gate above requires a
    //    resolved screen, so the least-information start defaults to the
    //    full pipeline rather than a dead drag, and updateDragCursor's
    //    per-tick recompute corrects the policy the moment a real cursor
    //    screen resolves.
    policy.streamDragMoved = true;
    policy.showOverlay = true;
    policy.grabKeyboard = true;
    policy.captureGeometry = true;
    policy.bypassReason = PhosphorProtocol::DragBypassReason::None;
    return policy;
}

bool WindowDragAdaptor::resolveReorderMode(const PhosphorZones::LayoutRegistry* layoutManager,
                                           const ISettings* settings, const QString& screenId)
{
    // A matched context SetDragBehavior rule wins over the global setting for this
    // screen; resolved through the layout registry (which the static
    // computeDragPolicy can't reach, so it takes the result as a param).
    //
    // The desktop and activity are two reads of one moment — a mid-call
    // virtual-desktop switch decoupling them is the split-snapshot shape
    // the sibling gates take a single resolver snapshot to avoid. Both
    // reads sit adjacent here on purpose; if this ever grows a third read,
    // route it through a ContextResolver snapshot like the siblings.
    if (layoutManager && !screenId.isEmpty()) {
        const int vd = layoutManager->currentVirtualDesktopForScreen(screenId);
        const QString activity = layoutManager->currentActivity();
        const PhosphorZones::ContextTilingParams params =
            layoutManager->resolveContextTilingParams(screenId, vd, activity);
        if (params.dragBehavior) {
            return static_cast<AutotileDragBehavior>(*params.dragBehavior) == AutotileDragBehavior::Reorder;
        }
    }
    return settings && settings->autotileDragBehavior() == AutotileDragBehavior::Reorder;
}

bool WindowDragAdaptor::effectiveReorderMode(const QString& screenId) const
{
    return resolveReorderMode(m_layoutManager, m_settings, screenId);
}

bool WindowDragAdaptor::isActiveLayoutSuppressedForScreen(const QString& screenId) const
{
    // Per-drag memo: this sits on the ~30 Hz dragMoved path (twice per tick —
    // the policy recompute and prepareHandlerContext), and the underlying
    // isContextActiveLayoutSuppressed runs up to six rule-evaluator resolves
    // per call. Keyed on (screen, desktop) because BOTH axes can change
    // mid-drag: the cursor crosses monitors, and a virtual-desktop switch
    // during a drag is a supported flow (the snap-assist path snapshots the
    // drop-time desktop for exactly that reason). The desktop read is cheap;
    // only the rule resolves are elided. beginDrag clears the memo, and
    // onLayoutChanged clears it when an assignment write lands mid-drag.
    if (!m_layoutManager || screenId.isEmpty()) {
        return false;
    }
    // Key on all three context axes the predicate reads. The two cheap reads
    // are paid per call; only the rule resolves are elided.
    const int vd = m_layoutManager->currentVirtualDesktopForScreen(screenId);
    const QString activity = m_layoutManager->currentActivity();
    if (screenId == m_suppressMemoScreenId && vd == m_suppressMemoDesktop && activity == m_suppressMemoActivity) {
        return m_suppressMemoValue;
    }
    const bool suppressed = isActiveLayoutSuppressedForScreen(screenId, vd);
    m_suppressMemoScreenId = screenId;
    m_suppressMemoDesktop = vd;
    m_suppressMemoActivity = activity;
    m_suppressMemoValue = suppressed;
    return suppressed;
}

bool WindowDragAdaptor::isActiveLayoutSuppressedForScreen(const QString& screenId, int virtualDesktop) const
{
    // Uncached desktop-explicit form, for callers that must evaluate against
    // a SNAPSHOT desktop rather than a live read (the deferred snap-assist
    // build). Mirrors OverlayService::isSnappingContextInactive's suppress
    // leg — same predicate, same activity axis — so the drag pipeline and the
    // overlay layer agree about whether a screen has zones (#724). Residual
    // divergence window: this reads the registry's live currentActivity()
    // while the overlay service reads its own mirrored member, so the two can
    // transiently disagree during an activity switch; both converge on the
    // same resolve within the switch cascade.
    if (!m_layoutManager || screenId.isEmpty()) {
        return false;
    }
    return m_layoutManager->isContextActiveLayoutSuppressed(screenId, virtualDesktop,
                                                            m_layoutManager->currentActivity());
}

PhosphorProtocol::DragPolicy WindowDragAdaptor::beginDrag(const QString& windowId, int frameX, int frameY,
                                                          int frameWidth, int frameHeight, const QString& startScreenId,
                                                          int mouseButtons)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "beginDrag: empty windowId";
        return PhosphorProtocol::DragPolicy{};
    }

    // Any stale pending state from a previous drag that didn't complete
    // cleanly must not bleed into this one. clearPendingSnapDragState also
    // cancels any leftover drag-insert preview so a new drag always starts
    // from a clean slate.
    clearPendingSnapDragState();

    // Mark the window as under a compositor interactive move for the WHOLE
    // drag. The scroll engine keeps a dragged tile in its strip model (the
    // effect's immediate float is visual only), so without this mark any
    // mid-drag retile re-emits the slot rect against KWin's move and the
    // per-ack reconcile pins size intents to transient drag frames. Cleared
    // on every drag exit (endDrag before the drop settles, window close,
    // and the clean-slate reset above for crash recovery).
    if (m_scrollEngine) {
        m_scrollEngine->setInteractiveDragWindow(windowId);
    }

    // Reset the drag-insert toggle latch on every drag start. This runs
    // before branching so it covers both the bypass path (drag starts on an
    // engine-owned screen, dragStarted() is never called) and the snap path.
    // Prev is seeded to true so the first dragMoved tick can't register a
    // spurious rising edge when the user initiated the drag with the
    // drag-insert trigger already held (e.g. Alt+Click, where Alt is KWin's
    // move-window modifier and also the default trigger). The user must
    // release and re-press to toggle.
    m_dragInsertToggled = false;
    m_prevDragInsertHeld = true;
    m_dragInsertTickLogged = false;
    m_lastLoggedRawInsertHeld = false;
    m_dragReorderAbandoned = false;
    // Zone span toggle latch (#563), same rationale and seed contract as the
    // autotile drag-insert latch above: reset on every beginDrag so it covers
    // both the bypass path (dragStarted never runs) and the snap path. Prev is
    // seeded true so a span trigger already held at drag start is not read as a
    // rising edge on the first dragMoved tick.
    m_zoneSpanToggled = false;
    m_prevZoneSpanTriggerHeld = true;
    // Per-drag suppression memo — assignments may have changed between drags.
    m_suppressMemoScreenId.clear();
    m_suppressMemoDesktop = 0;
    m_suppressMemoActivity.clear();
    m_suppressMemoValue = false;
    // Reset the modifier-conflict warning latch on every beginDrag,
    // not just the snap-path dragStarted further down. Bypass-path drags
    // never call dragStarted, so without this reset a previously latched
    // value would persist across bypass drags and suppress the warning
    // for a later drag that does reach the conflict check.
    m_modifierConflictWarned = false;
    // Same per-drag scope for the activation-transition log dedup: only
    // dragStarted resets it otherwise, which bypass drags never run, so a
    // carried value suppressed the first transition line of the next drag.
    m_lastLoggedActivationActive = false;

    // Cache ALL per-drag trigger sets unconditionally, ahead of the bypass
    // branch. The snap path may defer dragStarted (where the caches were
    // historically populated) until the user first holds a snap activation
    // trigger, and a drag STARTING bypassed (autotile screen, or the newly
    // common layout-suppressed screen) never runs dragStarted at all — its
    // dragMoved ticks would otherwise evaluate activation and zone-span
    // state against the PREVIOUS drag's trigger cache.
    if (m_settings) {
        m_cachedAutotileDragInsertTriggers = parseTriggers(m_settings->autotileDragInsertTriggers());
        m_cachedScrollingDragInsertTriggers = parseTriggers(m_settings->scrollingDragInsertTriggers());
        m_cachedActivationTriggers = parseTriggers(m_settings->dragActivationTriggers());
        m_cachedZoneSpanTriggers = parseTriggers(m_settings->zoneSpanTriggers());
    }

    // Resolve once and reuse: the autotile arm runs a full context rule
    // resolve, and the reorder-fallback branch below needs the same answer.
    // Must come AFTER the trigger-cache parse — the scrolling arm reads the
    // parsed scrolling cache for its AlwaysActive sentinel.
    const bool startReorderMode = effectiveDragReorderModeFor(startScreenId);
    const PhosphorProtocol::DragPolicy policy =
        computeDragPolicy(m_settings, m_autotileEngine, m_scrollEngine, windowId, startScreenId, m_contextResolver,
                          startReorderMode, isActiveLayoutSuppressedForScreen(startScreenId));

    // Reusable mutable copy — the reorder fallback path below may need to
    // restore immediateFloatOnStart that computeDragPolicy proactively cleared.
    PhosphorProtocol::DragPolicy effectivePolicy = policy;

    qCInfo(lcDbusWindow) << "beginDrag:" << windowId << "screen=" << startScreenId
                         << "bypass=" << effectivePolicy.bypassReason << "stream=" << effectivePolicy.streamDragMoved
                         << "immediateFloat=" << effectivePolicy.immediateFloatOnStart;

    if (effectivePolicy.bypassReason != PhosphorProtocol::DragBypassReason::None) {
        // Bypass path — record the id so the matching endDrag can find us,
        // but skip the full snap-path setup (overlay, zone state, etc.).
        // Trigger cache and stale-preview cleanup both happen above so bypass
        // and snap paths behave identically.
        //
        // Full state reset FIRST: a previous drag that crossed onto an active
        // snap screen mid-drag and dropped back on a bypass screen can leave
        // the zone block (m_currentZoneId/geometry, multi-zone state,
        // m_lastEmittedZoneGeometry) populated — dragStarted clears it for the
        // snap path, and this is the bypass twin. Without it, this drag
        // inherits a committable stale zone and a preview-suppressing
        // last-emitted rect from the previous drag.
        resetDragState();
        m_draggedWindowId = windowId;
        m_originalGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
        m_snapCancelled = false;
        m_wasSnapped = false;
        m_dragReorderActive = false;
        // resetDragState just set m_prevTriggerHeld=false; re-seed it true
        // HERE (after the reset, or it is clobbered) for the same reason the
        // two sibling latches are seeded above: dragStarted never runs on
        // this path, and prev=false with the activation trigger already held
        // at drag start reads as a rising edge on tick 1, spuriously
        // flipping toggle-activation for the rest of the drag.
        m_prevTriggerHeld = true;

        // Reorder mode: eagerly begin a drag-insert preview so even
        // zero-movement drops have something to commit on endDrag (otherwise
        // the autotile-bypass endDrag path falls through to ApplyFloat and
        // we'd float the very tile the user was trying to reorder). Gated on
        // isWindowTiled so floating/untracked windows still drag free and
        // take the normal ApplyFloat path — matches Krohnkite's "floating
        // windows are first-class" semantics.
        //
        // If beginDragInsertPreview returns false (target state missing,
        // window lost between tiled-check and the call, etc.) the daemon
        // can't run the swap pipeline. computeDragPolicy already cleared
        // immediateFloatOnStart on the assumption that reorder would take
        // over; without that override the user would see no float-restore
        // during the drag and a sudden float on drop. Restore the
        // legacy float-on-start behavior here so the UX matches the Float
        // mode default for the rest of this drag.
        if (effectivePolicy.bypassReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen && startReorderMode) {
            PhosphorEngine::IPlacementEngine* startEngine = dragInsertEngineFor(startScreenId);
            if (startEngine && startEngine->isWindowTiled(windowId)) {
                if (startEngine->beginDragInsertPreview(windowId, startScreenId)) {
                    m_dragReorderActive = true;
                } else {
                    qCWarning(lcDbusWindow)
                        << "beginDrag: reorder mode requested but beginDragInsertPreview failed for" << windowId
                        << "screen=" << startScreenId << "— restoring float-on-start fallback";
                    effectivePolicy.immediateFloatOnStart = startEngine->isWindowTracked(windowId);
                    // The fallback is a PER-DRAG decision. Without this
                    // latch, updateDragCursor's first same-screen tick
                    // recomputed the policy under reorderMode=true, saw the
                    // restored immediateFloatOnStart as a mismatch, re-
                    // emitted a contradicting policy, re-armed the reorder
                    // latch, and dragMoved retried the failed preview every
                    // tick — the deliberate fallback survived exactly one
                    // tick.
                    m_dragReorderAbandoned = true;
                }
            }
        }
        // Stash the full (post-fallback) policy so updateDragCursor's
        // comparator sees the same struct we returned to the client, and
        // endDrag can dispatch on bypassReason without recomputing.
        m_currentDragPolicy = effectivePolicy;
        return effectivePolicy;
    }

    // Snap path — do NOT run the legacy dragStarted setup yet. We defer
    // m_draggedWindowId population until the user first holds an
    // activation trigger (checked in updateDragCursor via
    // activateSnapDragIfNeeded). This restores the lazy drag-state
    // semantics that used to live in the kwin-effect's
    // sendDeferredDragStarted() latch: if a user drags a window without
    // ever holding the activation trigger, the daemon never runs the
    // overlay show/hide cycle and never pays the Vulkan shader-overlay
    // destroy/create cost. Pre-parsing triggers here (instead of inside
    // dragStarted) so activateSnapDragIfNeeded can compare modifier state
    // against them without a chicken-and-egg dependency. (The caches are
    // populated in the unconditional block above, so bypass-path drags get
    // them too.)

    // Same clean-slate reset the bypass branch runs: a prior drag that never
    // got an endDrag (effect crash, dropped reply) leaves m_draggedWindowId
    // set, and with it stale, this pending drag is permanently dead —
    // updateDragCursor's pending-activate branch requires the id empty, and
    // its mismatch return then swallows every tick.
    resetDragState();
    m_prevTriggerHeld = true;
    m_pendingSnapDragWindowId = windowId;
    m_pendingSnapDragGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
    m_pendingSnapDragWasSnapped = m_windowTracking && !m_windowTracking->getZoneForWindow(windowId).isEmpty();
    Q_UNUSED(
        mouseButtons); // pre-2026-05 cached the mouseButtons here; dragStarted Q_UNUSEDd them so the cache was dead

    // Stash the full policy so updateDragCursor's comparator has a
    // previous-policy reference to compare cross-VS candidates against.
    m_currentDragPolicy = effectivePolicy;
    return effectivePolicy;
}

bool WindowDragAdaptor::activateSnapDragIfNeeded(int modifiers, int mouseButtons, int cursorX, int cursorY)
{
    // Already active? nothing to do.
    if (!m_draggedWindowId.isEmpty()) {
        return true;
    }
    // No pending drag to activate.
    if (m_pendingSnapDragWindowId.isEmpty()) {
        return false;
    }

    const Qt::KeyboardModifiers mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    const bool triggerHeld = anyTriggerHeld(m_cachedActivationTriggers, mods, mouseButtons);
    const bool toggleMode = m_settings && m_settings->toggleActivation();

    // Edge-hover activation: if the zone selector feature is enabled and the
    // cursor is near an edge that would trigger the popup, treat that as a
    // commitment to snap — the same intent the activation modifier signals.
    // Without this path, non-modifier drags stayed pending forever, the
    // zone-selector trigger inside dragMoved never ran, and the popup never
    // appeared. Repro: drag a window, don't hold the modifier, hover an
    // edge — nothing happens.
    bool edgeActivation = false;
    if (!triggerHeld && !toggleMode && m_settings && m_settings->zoneSelectorEnabled()) {
        auto resolved = resolveScreenAt(QPointF(cursorX, cursorY));
        // Live-mode disable check — `handleFor` not `handleForMode(Snapping)`.
        // The user may have flipped the screen's mode between beginDrag
        // and this cursor tick; consult the destination's actual mode
        // so a snap-mode screen that just turned autotile doesn't
        // continue firing edge-activation against the stale snap-disable
        // list. Mirrors the matching fix in computeDragPolicy above.
        // Layout-suppressed screens are excluded too: promoting a pending
        // drag there would activate the snap machinery (keyboard grab, zone
        // state) on a screen the policy tier declares a dead drag. The
        // mid-drag selector check (checkZoneSelectorTrigger) refuses the same
        // screens, so a suppressed monitor offers no selector at either tier.
        // A missing resolver means "gate off", matching computeDragPolicy and
        // prepareHandlerContext — requiring it non-null made this the one
        // sibling that failed CLOSED pre-wiring.
        if (resolved.qscreen
            && !(m_contextResolver && m_contextResolver->isDisabled(m_contextResolver->handleFor(resolved.screenId)))
            && !isActiveLayoutSuppressedForScreen(resolved.screenId)) {
            edgeActivation = isNearTriggerEdge(resolved.qscreen, cursorX, cursorY, resolved.screenId);
        }
    }

    if (!triggerHeld && !toggleMode && !edgeActivation) {
        return false;
    }

    const QString pendingId = m_pendingSnapDragWindowId;
    const QRect pendingGeo = m_pendingSnapDragGeometry;
    m_pendingSnapDragWindowId.clear();
    m_pendingSnapDragGeometry = QRect();

    qCInfo(lcDbusWindow) << "activateSnapDragIfNeeded: promoting" << pendingId << "to active drag"
                         << "via=" << (edgeActivation ? "edge-hover" : "modifier");
    dragStarted(pendingId, static_cast<double>(pendingGeo.x()), static_cast<double>(pendingGeo.y()),
                static_cast<double>(pendingGeo.width()), static_cast<double>(pendingGeo.height()));
    // Drop the wasSnapped latch after dragStarted has re-derived live
    // m_wasSnapped from the tracking state — its single consumer (the
    // endDrag pending-never-activated branch) is now unreachable for
    // this drag. This is defensive: the next beginDrag re-clears the
    // field via clearPendingSnapDragState() regardless, so a stale-true
    // value can't actually bleed into a later drag; the reset just keeps
    // the field honest for the remainder of this now-activated drag.
    m_pendingSnapDragWasSnapped = false;
    return !m_draggedWindowId.isEmpty();
}

void WindowDragAdaptor::clearPendingSnapDragState()
{
    m_pendingSnapDragWindowId.clear();
    m_pendingSnapDragGeometry = QRect();
    m_pendingSnapDragWasSnapped = false;
    m_dragReorderActive = false;
    // Any drag-insert preview left over from an incomplete previous drag
    // (daemon lost track, client disconnect, snapping-disabled flip, etc.)
    // must be cleared too — its referenced window may no longer exist.
    cancelDragInsertIfActive();
    // Same staleness argument for the interactive-drag mark: a leftover
    // mark would silently exempt a window from layout forever.
    if (m_scrollEngine) {
        m_scrollEngine->setInteractiveDragWindow(QString());
    }
}

PhosphorProtocol::DragOutcome WindowDragAdaptor::endDrag(const QString& windowId, int cursorX, int cursorY,
                                                         int modifiers, int mouseButtons, bool cancelled)
{
    PhosphorProtocol::DragOutcome outcome;
    outcome.windowId = windowId;

    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "endDrag: empty windowId";
        return outcome;
    }

    // Pending snap-path drag that never activated. Mirrors the historical
    // effect-side behavior where the deferred drag-started notification
    // never latched, so the drag-stopped path short-circuited without a
    // daemon round-trip.
    // If the window was snapped at drag start and the user dragged it
    // without holding the activation trigger, drive notifyDragOutUnsnap
    // on the window-tracking side so the window unsnaps and floats at
    // the drop location — otherwise the stale zone assignment persists.
    if (m_draggedWindowId.isEmpty() && m_pendingSnapDragWindowId == windowId) {
        // A pending drag is always on the SNAP path: beginDrag's snap path sets
        // m_currentDragPolicy.bypassReason == None, and the only mutator of that field
        // afterwards (the policy-flip in updateDragCursor) runs solely after the early
        // `windowId != m_draggedWindowId` return there, i.e. only when m_draggedWindowId
        // is non-empty — never while the drag is still pending. So there is no bypass to
        // promote here; a pending never-activated drag can only unsnap-and-NoOp.
        const bool wasSnapped = m_pendingSnapDragWasSnapped;
        qCInfo(lcDbusWindow) << "endDrag: pending snap drag never activated" << windowId << "wasSnapped=" << wasSnapped
                             << "cancelled=" << cancelled;
        clearPendingSnapDragState();
        m_currentDragPolicy = {};
        // If the window was snapped at drag start and dragged without holding the
        // activation trigger, unsnap it so it floats at the drop location instead of
        // leaving a stale zone assignment.
        if (!cancelled && wasSnapped && m_windowTracking) {
            m_windowTracking->notifyDragOutUnsnap(windowId);
        }
        outcome.action = PhosphorProtocol::DragOutcome::NoOp;
        return outcome;
    }

    if (m_draggedWindowId != windowId) {
        qCWarning(lcDbusWindow) << "endDrag: windowId mismatch — stashed=" << m_draggedWindowId
                                << "received=" << windowId;
        // Two very different situations share this branch. With a LIVE drag
        // in flight (a newer beginDrag owns m_draggedWindowId), the state
        // belongs to THAT drag — wiping the policy here degraded its
        // bypassReason to None and routed an engine-owned drop into the
        // snap commit pipeline, and dropped its reorder latch. Leave it
        // alone. Only with NO live drag is the state stale residue worth
        // clearing, and then the sweep must be complete: the policy, the
        // reorder latch, any leftover preview, AND the scroll engine's
        // interactive-drag mark (a leftover mark silently exempts a window
        // from layout until the next beginDrag).
        if (m_draggedWindowId.isEmpty()) {
            m_currentDragPolicy = {};
            m_dragReorderActive = false;
            cancelDragInsertIfActive();
            if (m_scrollEngine) {
                m_scrollEngine->setInteractiveDragWindow(QString());
            }
        }
        return outcome;
    }

    const PhosphorProtocol::DragBypassReason bypassReason = m_currentDragPolicy.bypassReason;
    m_currentDragPolicy = {}; // next drag starts fresh
    // m_dragReorderActive lives for one drag only. Reset here (before the
    // various early-return branches below) so no branch has to remember.
    m_dragReorderActive = false;
    // The interactive move is over: release the window back to engine
    // authority BEFORE the drop settles, so a preview commit's finalizing
    // relayout emits the dragged window's rect unfiltered.
    if (m_scrollEngine) {
        m_scrollEngine->setInteractiveDragWindow(QString());
    }

    qCInfo(lcDbusWindow) << "endDrag:" << windowId << "cursor=" << cursorX << cursorY << "cancelled=" << cancelled
                         << "bypass=" << bypassReason;

    // Cancelled drags on any path: plugin should simply clean up. For
    // autotile bypass, there's no zone/snap state to unwind. For snap path,
    // we still delegate to legacy dragStopped so it can reset its own
    // internal state machine, then emit CancelSnap back.
    if (cancelled) {
        // BOTH arms cancel any live drag-insert preview FIRST. The snap arm
        // delegates to dragStopped, whose first action is
        // settleDragInsertPreviewAt — a COMMIT when the preview's screen
        // matches the cursor. A cancelled drag durably reordering the strip
        // is exactly what "cancelled" must not do; with the preview gone,
        // the settle finds nothing and dragStopped proceeds to its normal
        // overlay/zone teardown.
        cancelDragInsertIfActive();
        if (bypassReason == PhosphorProtocol::DragBypassReason::None) {
            // Snap path cancelled — run dragStopped through the legacy
            // adaptor so overlay/zone-state cleanup happens, but discard
            // the geometry outputs.
            int sx = 0, sy = 0, sw = 0, sh = 0;
            bool shouldApply = false;
            bool restoreSizeOnly = false;
            bool snapAssistRequested = false;
            QString releaseScreen;
            QString resolvedZoneId;
            dragStopped(windowId, cursorX, cursorY, modifiers, mouseButtons, sx, sy, sw, sh, shouldApply, releaseScreen,
                        restoreSizeOnly, snapAssistRequested, resolvedZoneId);
        } else {
            // Bypass paths. The preview cancel already ran above the split;
            // the overlay/selector can still be visible here (shown on a
            // snap screen before a mid-drag crossing — resetDragState never
            // tears visibility down), and zone state inherited from that
            // crossing dies with resetDragState instead of leaking into the
            // next drag.
            hideOverlayAndSelector();
            resetDragState();
        }
        outcome.action = PhosphorProtocol::DragOutcome::CancelSnap;
        return outcome;
    }

    // Autotile bypass — plugin will float the window at the drop location.
    // Daemon has no placement decision to make; the outcome just carries
    // the release screen so the plugin can pass it to
    // setWindowFloatingForScreen.
    // Dispatch on the bypass reason as a default-less switch so -Wswitch
    // flags the NEXT enumerator addition here (the build runs -Wall without
    // -Werror, so it is a warning) instead of letting it fall silently into
    // the snap path — which is exactly how LayoutSuppressed was missed on
    // first landing. Bypass exits route
    // through resetDragState (not a bare id clear) so zone state inherited
    // from a mid-drag screen crossing dies with the drag.
    switch (bypassReason) {
    case PhosphorProtocol::DragBypassReason::EngineOwnedScreen:
        // Drag-insert: if a preview is live ON THE RELEASE SCREEN (autotile
        // stack or scroll strip), commit it so the window takes its picked
        // slot on the next retile. The owning engine controls final
        // geometry; no float outcome needed (see the drop.cpp twin, which
        // shares the screen gate through this helper).
        if (settleDragInsertPreviewAt(cursorX, cursorY)) {
            outcome.action = PhosphorProtocol::DragOutcome::NoOp;
            hideOverlayAndSelector();
            resetDragState();
            return outcome;
        }
        // Release screen is resolved plugin-side from the cursor position,
        // but we pass the cursor through so the daemon can log it. The
        // plugin passes its own view of "current screen" to the float call.
        outcome.action = PhosphorProtocol::DragOutcome::ApplyFloat;
        outcome.targetScreenId.clear(); // plugin resolves from cursor
        outcome.x = cursorX;
        outcome.y = cursorY;
        hideOverlayAndSelector();
        resetDragState();
        return outcome;
    case PhosphorProtocol::DragBypassReason::SnappingDisabled:
    case PhosphorProtocol::DragBypassReason::ContextDisabled:
    case PhosphorProtocol::DragBypassReason::LayoutSuppressed:
        // Dead drag — no action. LayoutSuppressed belongs here: the policy
        // declared the drag dead (the screen has no zone layout), so the
        // release must not run the snap dispatch below.
        outcome.action = PhosphorProtocol::DragOutcome::NoOp;
        hideOverlayAndSelector();
        resetDragState();
        return outcome;
    case PhosphorProtocol::DragBypassReason::None:
        break; // canonical snap path below
    }

    // Snap path — delegate to legacy dragStopped and translate its
    // out-params into a PhosphorProtocol::DragOutcome. dragStopped handles the full
    // dispatch matrix (snap-to-zone, drag-out unsnap, cross-screen
    // cleanup, zone selector, snap assist).
    //
    // The primary zone id is returned via a dedicated out-parameter
    // (resolvedZoneId) populated by each snap-success branch inside
    // drop.cpp — hover detection, zone selector, and multi-zone modifier
    // all write it explicitly. This replaces the earlier pattern of
    // snapshotting m_currentZoneId before the call, which missed the
    // zone-selector path (that branch never wrote m_currentZoneId and
    // left an empty zoneId on the outcome — rejected post-Phase-1B by
    // PhosphorProtocol::DragOutcome::validationError).

    int sx = 0, sy = 0, sw = 0, sh = 0;
    bool shouldApply = false;
    bool restoreSizeOnly = false;
    bool snapAssistRequested = false;
    QString releaseScreen;
    QString resolvedZoneId;
    dragStopped(windowId, cursorX, cursorY, modifiers, mouseButtons, sx, sy, sw, sh, shouldApply, releaseScreen,
                restoreSizeOnly, snapAssistRequested, resolvedZoneId);

    outcome.targetScreenId = releaseScreen;
    outcome.x = sx;
    outcome.y = sy;
    outcome.width = sw;
    outcome.height = sh;
    outcome.requestSnapAssist = snapAssistRequested;
    // emptyZones intentionally left empty here — the expensive
    // buildEmptyZoneList walk is deferred to computeAndEmitSnapAssist() which
    // runs after this D-Bus reply has been sent, so the compositor is
    // unblocked before the list is built. The effect receives the list via
    // the snapAssistReady signal and shows the window picker from its slot.
    outcome.emptyZones.clear();

    if (shouldApply) {
        outcome.action =
            restoreSizeOnly ? PhosphorProtocol::DragOutcome::RestoreSize : PhosphorProtocol::DragOutcome::ApplySnap;
        // Only ApplySnap carries a target zone. RestoreSize is drag-out
        // unsnap (no target zone by definition).
        if (!restoreSizeOnly) {
            outcome.zoneId = resolvedZoneId;
        }
    } else {
        outcome.action = PhosphorProtocol::DragOutcome::NoOp;
    }

    // No m_draggedWindowId.clear() here: both dragStopped exits (the settle
    // early-return and the tail) run resetDragState, which already clears it.

    // Schedule the snap-assist compute to run after this function returns and
    // the D-Bus reply has been dispatched. QTimer::singleShot(0) queues the
    // call for the next event loop iteration, which happens after Qt finishes
    // handing the PhosphorProtocol::DragOutcome off to the D-Bus marshaller.
    if (snapAssistRequested) {
        QTimer::singleShot(0, this, &WindowDragAdaptor::computeAndEmitSnapAssist);
    }

    return outcome;
}

void WindowDragAdaptor::updateDragCursor(const QString& windowId, int cursorX, int cursorY, int modifiers,
                                         int mouseButtons)
{
    if (windowId.isEmpty()) {
        return;
    }

    // Pending snap-path drag: try to activate if the trigger is now held
    // or the cursor has reached an edge-trigger region. If it's still
    // pending after the check, return — the daemon does no overlay work
    // until the user commits to zone selection.
    if (m_draggedWindowId.isEmpty() && m_pendingSnapDragWindowId == windowId) {
        if (!activateSnapDragIfNeeded(modifiers, mouseButtons, cursorX, cursorY)) {
            return;
        }
    }

    if (windowId != m_draggedWindowId) {
        return;
    }

    // Cross-VS flip detection: if the cursor moved to a screen whose policy
    // differs from the one in force, compute the new policy and emit
    // dragPolicyChanged. The plugin reacts by calling handleDragToFloat or
    // re-initializing snap-drag state as needed. This replaces the
    // effect-side detection loop in the dragMoved lambda that used the
    // stale m_managedScreens cache to decide when to flip.
    //
    // Full-struct comparison via PhosphorProtocol::DragPolicy::operator== catches every
    // policy-relevant transition: bypass-reason flips, autotile→autotile
    // cross-VS (distinct screenIds), and any future per-screen field added
    // to the struct (snap behavior, zone-selector corner, etc.) without
    // touching this site.
    auto resolved = resolveScreenAt(QPointF(cursorX, cursorY));
    const QString cursorScreenId = resolved.screenId;
    if (!cursorScreenId.isEmpty()) {
        // effectiveDragReorderModeFor runs a full context rule resolve on the
        // autotile arm; on this ~30 Hz path its result only matters on
        // engine-owned screens (computeDragPolicy reads reorderMode solely in
        // those branches). Gate on the cheap dragInsertEngineFor set-lookups
        // so snap-only drags don't pay a per-motion-event rule evaluation for
        // a value that would be discarded.
        PhosphorEngine::IPlacementEngine* cursorEngine = dragInsertEngineFor(cursorScreenId);
        const bool reorderMode = cursorEngine && effectiveDragReorderModeFor(cursorScreenId);
        const PhosphorProtocol::DragPolicy candidate =
            computeDragPolicy(m_settings, m_autotileEngine, m_scrollEngine, windowId, cursorScreenId, m_contextResolver,
                              reorderMode, isActiveLayoutSuppressedForScreen(cursorScreenId));
        if (candidate != m_currentDragPolicy) {
            // Log both bypass reason and screenId on each side so same-reason
            // flips (snap→snap or autotile→autotile cross-VS) aren't opaque in
            // the logs — "None @ DP-1 -> None @ DP-2" makes the trigger obvious.
            qCInfo(lcDbusWindow) << "updateDragCursor: policy flip" << m_currentDragPolicy.bypassReason << "@"
                                 << m_currentDragPolicy.screenId
                                 << "immediateFloat=" << m_currentDragPolicy.immediateFloatOnStart << "->"
                                 << candidate.bypassReason << "@" << cursorScreenId
                                 << "immediateFloat=" << candidate.immediateFloatOnStart;
            m_currentDragPolicy = candidate;
            // Re-latch reorder mode to the CURRENT autotile screen. Per-context
            // SetDragBehavior means two autotile screens can differ (start Reorder,
            // destination Float, or vice versa); without this the beginDrag-time
            // start-screen snapshot would keep forcing the start screen's mode on the
            // destination (dragMoved reads m_dragReorderActive at drag.cpp). The
            // per-tick preview lifecycle in dragMoved begins/cancels the drag-insert
            // preview off this latch, so tracking the current screen restores correct
            // Reorder-vs-Float behavior across a mid-drag screen crossing.
            //
            // The GATE mirrors beginDrag's eager-preview seed (the reorder
            // block in its bypass branch): (1) the destination policy is the autotile-bypass path —
            // bypassReason == EngineOwnedScreen excludes a context-DISABLED autotile screen
            // (computeDragPolicy evaluates ContextDisabled first), so the latch never
            // forces a preview on a dead/excluded screen; and (2) the window is still
            // tiled — beginDragInsertPreview would otherwise ADOPT a floating/untracked
            // window into the stack (fresh-adoption branch), silently tiling a window
            // dragged onto a Reorder screen with no trigger held, defeating the "floating
            // windows drag free" invariant. A genuinely tiled window crossing Float→Reorder
            // is still tiled at the flip, so this stays true for the case that should reorder.
            //
            // Unlike the seed, this only sets the latch; it does NOT eagerly call
            // beginDragInsertPreview or restore a float-on-start fallback. The next
            // dragMoved tick begins the preview off the latch (drag.cpp), so on a mid-drag
            // cross to a Reorder screen where that begin fails the window float-drops on
            // release rather than during the drag — an acceptable degradation for a
            // transient begin failure (the common case begins successfully).
            // isWindowTiled OR a live preview: under detach-once semantics
            // the dragged window is NOT a tile while its preview is live, so
            // tiled-only would drop the reorder latch on every mid-drag
            // screen crossing. The preview probe is ENGINE-AGNOSTIC
            // (dragInsertPreviewEngine, not cursorEngine): on a cross-engine
            // crossing the live preview still belongs to the departing
            // engine, which is exactly the crossing this disjunct exists to
            // keep latched. And once beginDrag's fallback abandoned reorder
            // for this drag, the latch stays down — re-arming it here made
            // dragMoved retry the failed preview every tick and re-emitted a
            // policy contradicting the float fallback one tick after it.
            m_dragReorderActive = candidate.bypassReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen
                && reorderMode && !m_dragReorderAbandoned && cursorEngine
                && (cursorEngine->isWindowTiled(windowId) || dragInsertPreviewEngine() != nullptr);
            // A flip onto ANY bypass screen invalidates the live zone
            // selection: without this, the zone state populated on the last
            // active-screen tick (m_currentZoneId/geometry, multi-zone set,
            // painted spans) rides along — dragStopped can commit it on a
            // screen that cannot host it (the physical-only #511 guard does
            // not catch same-monitor virtual-screen splits), and the
            // still-valid m_currentZoneGeometry keeps the tail geometry emit
            // pinning the window at zone size while it is dragged over the
            // bypass screen, which also starves the restore-free-size leg.
            if (candidate.bypassReason != PhosphorProtocol::DragBypassReason::None) {
                if (m_zoneDetector) {
                    m_zoneDetector->clearHighlights();
                }
                if (m_overlayService) {
                    m_overlayService->clearHighlight();
                }
                m_currentZoneId.clear();
                m_currentZoneScreenId.clear();
                m_currentZoneGeometry = QRect();
                m_currentAdjacentZoneIds.clear();
                m_isMultiZoneMode = false;
                m_currentMultiZoneGeometry = QRect();
                m_paintedZoneIds.clear();
                // The selector popup too: once a drag-insert preview goes
                // live, dragMoved's preview block returns before the
                // selector-dismiss gates on every tick, so a popup shown on
                // the pre-crossing snap screen has no other surviving hide
                // path (endDrag's bypass exits tear visibility down only at
                // release; this keeps it from lingering the whole hold).
                if (m_zoneSelectorShown && m_overlayService) {
                    m_zoneSelectorShown = false;
                    m_zoneSelectorShownOn.clear();
                    m_overlayService->hideZoneSelector();
                }
            }
            Q_EMIT dragPolicyChanged(windowId, candidate);
            // After the flip, fall through: if we're now on the snap path
            // we still want to call legacy dragMoved below for overlay
            // updates. If we're now bypassed, legacy dragMoved is a
            // no-op because m_snapCancelled isn't set and dragMoved only
            // does real work when m_draggedWindowId matches — which it
            // does — but the overlay/zone state will be hidden by the
            // effect's reaction handler, so running dragMoved here does
            // no harm.
        }
    }

    // Snap path: forward to legacy dragMoved so overlay/zone-detection
    // state stays current. Bypass paths still run dragMoved because it
    // also drives the autotile drag-insert preview block in dragMoved.
    // beginDrag's bypass branch DOES set m_draggedWindowId, so dragMoved's
    // `windowId != m_draggedWindowId` guard would NOT early-return. The
    // snap-side overlay branches inside dragMoved instead become no-ops
    // because `prepareHandlerContext` suppresses the overlay path when the
    // cursor screen is engine-owned (autotile or scrolling) or
    // context-disabled — the same gate that decided the bypass branch at
    // beginDrag.
    dragMoved(windowId, cursorX, cursorY, modifiers, mouseButtons);
}

} // namespace PlasmaZones
