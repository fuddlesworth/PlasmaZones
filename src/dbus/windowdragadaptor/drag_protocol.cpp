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

#include "../windowdragadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "../../core/settings_interfaces.h"
#include "../../core/logging.h"
#include <PhosphorEngineApi/IPlacementEngine.h>
#include <QGuiApplication>
#include <QTimer>

namespace PlasmaZones {

DragPolicy WindowDragAdaptor::computeDragPolicy(const ISettings* settings,
                                                const PhosphorEngineApi::IPlacementEngine* autotileEngine,
                                                const QString& windowId, const QString& screenId, int curDesktop,
                                                const QString& curActivity)
{
    DragPolicy policy;
    policy.screenId = screenId;

    // Order matters — strongest disables checked first so the reason string
    // is stable regardless of which conditions coincide.

    // 1) Disabled context (activity / desktop / monitor excluded in settings).
    //    Dead drag: no overlay, no cursor stream, no float transition.
    if (settings && !screenId.isEmpty() && isContextDisabled(settings, screenId, curDesktop, curActivity)) {
        policy.bypassReason = DragBypassReason::ContextDisabled;
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
        policy.bypassReason = DragBypassReason::AutotileScreen;
        policy.captureGeometry = true; // preserve pre-autotile size for unfloat restore
        const bool reorderMode = settings && settings->autotileDragBehavior() == AutotileDragBehavior::Reorder;
        if (!windowId.isEmpty() && !reorderMode) {
            policy.immediateFloatOnStart = autotileEngine->isWindowTracked(windowId);
        }
        return policy;
    }

    // 3) Top-level snapping disabled. Dead drag on any non-autotile screen —
    //    the user configured the tool with snap mode off. beginDrag still
    //    returns a policy so endDrag can clean up consistently.
    if (settings && !settings->snappingEnabled()) {
        policy.bypassReason = DragBypassReason::SnappingDisabled;
        return policy;
    }

    // 4) Snap path — canonical case. Plugin streams cursor updates, daemon
    //    runs overlay / zone detection / snap assist. Activation-trigger
    //    gating still happens locally in the plugin against the current
    //    modifier state (input-event optimization, not policy).
    policy.streamDragMoved = true;
    policy.showOverlay = true;
    policy.grabKeyboard = true;
    policy.captureGeometry = true;
    policy.bypassReason = DragBypassReason::None;
    return policy;
}

DragPolicy WindowDragAdaptor::beginDrag(const QString& windowId, int frameX, int frameY, int frameWidth,
                                        int frameHeight, const QString& startScreenId, int mouseButtons)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "beginDrag: empty windowId";
        return DragPolicy{};
    }

    // Any stale pending state from a previous drag that didn't complete
    // cleanly must not bleed into this one. clearPendingSnapDragState also
    // cancels any leftover drag-insert preview so a new drag always starts
    // from a clean slate.
    clearPendingSnapDragState();

    // Reset autotile drag-insert toggle state on every drag start. This runs
    // before branching so it covers both the bypass path (drag starts on an
    // autotile screen, dragStarted() is never called) and the snap path.
    // Prev is seeded to true so the first dragMoved tick can't register a
    // spurious rising edge when the user initiated the drag with the autotile
    // trigger already held (e.g. Alt+Click, where Alt is KWin's move-window
    // modifier and also the default drag-insert trigger). The user must
    // release and re-press to toggle.
    m_autotileDragInsertToggled = false;
    m_prevAutotileDragInsertHeld = true;

    // Cache autotile drag-insert triggers unconditionally. The snap path may
    // defer dragStarted (where the cache was historically populated) until
    // the user first holds a snap activation trigger. If drag-insert fires
    // before that (e.g. user holds the drag-insert trigger directly, or the
    // cursor crosses to an autotile screen and the policy flips to bypass),
    // dragMoved would otherwise read a stale cache from the previous drag.
    if (m_settings) {
        m_cachedAutotileDragInsertTriggers = parseTriggers(m_settings->autotileDragInsertTriggers());
    }

    const int curDesktop = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    const QString curActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
    const DragPolicy policy =
        computeDragPolicy(m_settings, m_autotileEngine, windowId, startScreenId, curDesktop, curActivity);

    // Reusable mutable copy — the reorder fallback path below may need to
    // restore immediateFloatOnStart that computeDragPolicy proactively cleared.
    DragPolicy effectivePolicy = policy;

    qCInfo(lcDbusWindow) << "beginDrag:" << windowId << "screen=" << startScreenId
                         << "bypass=" << effectivePolicy.bypassReason << "stream=" << effectivePolicy.streamDragMoved
                         << "immediateFloat=" << effectivePolicy.immediateFloatOnStart;

    if (effectivePolicy.bypassReason != DragBypassReason::None) {
        // Bypass path — record the id so the matching endDrag can find us,
        // but skip the full snap-path setup (overlay, zone state, etc.).
        // Trigger cache and stale-preview cleanup both happen above so bypass
        // and snap paths behave identically.
        m_draggedWindowId = windowId;
        m_originalGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
        m_snapCancelled = false;
        m_wasSnapped = false;
        m_dragReorderActive = false;

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
        if (effectivePolicy.bypassReason == DragBypassReason::AutotileScreen && m_autotileEngine && m_settings
            && m_settings->autotileDragBehavior() == AutotileDragBehavior::Reorder
            && m_autotileEngine->isWindowTiled(windowId)) {
            if (m_autotileEngine->beginDragInsertPreview(windowId, startScreenId)) {
                m_dragReorderActive = true;
            } else {
                qCWarning(lcDbusWindow) << "beginDrag: reorder mode requested but beginDragInsertPreview failed for"
                                        << windowId << "screen=" << startScreenId
                                        << "— restoring float-on-start fallback";
                effectivePolicy.immediateFloatOnStart = m_autotileEngine->isWindowTracked(windowId);
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
    // against them without a chicken-and-egg dependency.
    if (m_settings) {
        m_cachedActivationTriggers = parseTriggers(m_settings->dragActivationTriggers());
        m_cachedDeactivationTriggers = parseTriggers(m_settings->dragDeactivationTriggers());
        m_cachedZoneSpanTriggers = parseTriggers(m_settings->zoneSpanTriggers());
    }

    m_pendingSnapDragWindowId = windowId;
    m_pendingSnapDragGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
    m_pendingSnapDragMouseButtons = mouseButtons;
    m_pendingSnapDragWasSnapped = m_windowTracking && !m_windowTracking->getZoneForWindow(windowId).isEmpty();

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
        if (resolved.qscreen
            && !isContextDisabled(m_settings, resolved.screenId, m_layoutManager->currentVirtualDesktop(),
                                  m_layoutManager->currentActivity())) {
            edgeActivation = isNearTriggerEdge(resolved.qscreen, cursorX, cursorY, resolved.screenId);
        }
    }

    if (!triggerHeld && !toggleMode && !edgeActivation) {
        return false;
    }

    const QString pendingId = m_pendingSnapDragWindowId;
    const QRect pendingGeo = m_pendingSnapDragGeometry;
    const int pendingButtons = m_pendingSnapDragMouseButtons;
    m_pendingSnapDragWindowId.clear();
    m_pendingSnapDragGeometry = QRect();
    m_pendingSnapDragMouseButtons = 0;
    // Keep m_pendingSnapDragWasSnapped — dragStarted re-derives m_wasSnapped
    // from live tracking state.

    qCInfo(lcDbusWindow) << "activateSnapDragIfNeeded: promoting" << pendingId << "to active drag"
                         << "via=" << (edgeActivation ? "edge-hover" : "modifier");
    dragStarted(pendingId, static_cast<double>(pendingGeo.x()), static_cast<double>(pendingGeo.y()),
                static_cast<double>(pendingGeo.width()), static_cast<double>(pendingGeo.height()), pendingButtons);
    return !m_draggedWindowId.isEmpty();
}

void WindowDragAdaptor::clearPendingSnapDragState()
{
    m_pendingSnapDragWindowId.clear();
    m_pendingSnapDragGeometry = QRect();
    m_pendingSnapDragMouseButtons = 0;
    m_pendingSnapDragWasSnapped = false;
    m_dragReorderActive = false;
    // Any drag-insert preview left over from an incomplete previous drag
    // (daemon lost track, client disconnect, snapping-disabled flip, etc.)
    // must be cleared too — its referenced window may no longer exist.
    cancelDragInsertIfActive();
}

DragOutcome WindowDragAdaptor::endDrag(const QString& windowId, int cursorX, int cursorY, int modifiers,
                                       int mouseButtons, bool cancelled)
{
    DragOutcome outcome;
    outcome.windowId = windowId;

    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "endDrag: empty windowId";
        return outcome;
    }

    // Pending snap-path drag that never activated. Mirrors the main-branch
    // behavior where sendDeferredDragStarted() never latched and
    // DragTracker::dragStopped short-circuited at `if (!m_dragStartedSent)`.
    // If the window was snapped at drag start and the user dragged it
    // without holding the activation trigger, drive notifyDragOutUnsnap
    // on the window-tracking side so the window unsnaps and floats at
    // the drop location — otherwise the stale zone assignment persists.
    if (m_draggedWindowId.isEmpty() && m_pendingSnapDragWindowId == windowId) {
        const bool wasSnapped = m_pendingSnapDragWasSnapped;
        qCInfo(lcDbusWindow) << "endDrag: pending snap drag never activated" << windowId << "wasSnapped=" << wasSnapped
                             << "cancelled=" << cancelled;
        clearPendingSnapDragState();
        m_currentDragPolicy = {};
        if (!cancelled && wasSnapped && m_windowTracking) {
            m_windowTracking->notifyDragOutUnsnap(windowId);
        }
        outcome.action = DragOutcome::NoOp;
        return outcome;
    }

    if (m_draggedWindowId != windowId) {
        qCWarning(lcDbusWindow) << "endDrag: windowId mismatch — stashed=" << m_draggedWindowId
                                << "received=" << windowId;
        return outcome;
    }

    const DragBypassReason bypassReason = m_currentDragPolicy.bypassReason;
    m_currentDragPolicy = {}; // next drag starts fresh
    // m_dragReorderActive lives for one drag only. Reset here (before the
    // various early-return branches below) so no branch has to remember.
    m_dragReorderActive = false;

    qCInfo(lcDbusWindow) << "endDrag:" << windowId << "cursor=" << cursorX << cursorY << "cancelled=" << cancelled
                         << "bypass=" << bypassReason;

    // Cancelled drags on any path: plugin should simply clean up. For
    // autotile bypass, there's no zone/snap state to unwind. For snap path,
    // we still delegate to legacy dragStopped so it can reset its own
    // internal state machine, then emit CancelSnap back.
    if (cancelled) {
        if (bypassReason == DragBypassReason::None) {
            // Snap path cancelled — run dragStopped through the legacy
            // adaptor so overlay/zone-state cleanup happens, but discard
            // the geometry outputs.
            int sx = 0, sy = 0, sw = 0, sh = 0;
            bool shouldApply = false;
            bool restoreSizeOnly = false;
            bool snapAssistRequested = false;
            QString releaseScreen;
            EmptyZoneList emptyZones;
            QString resolvedZoneId;
            dragStopped(windowId, cursorX, cursorY, modifiers, mouseButtons, sx, sy, sw, sh, shouldApply, releaseScreen,
                        restoreSizeOnly, snapAssistRequested, emptyZones, resolvedZoneId);
        } else {
            // Bypass paths — no overlay state to clean up; just drop the id.
            // An active autotile drag-insert preview must be cancelled so
            // neighbours snap back to their original order.
            cancelDragInsertIfActive();
            m_draggedWindowId.clear();
        }
        outcome.action = DragOutcome::CancelSnap;
        return outcome;
    }

    // Autotile bypass — plugin will float the window at the drop location.
    // Daemon has no placement decision to make; the outcome just carries
    // the release screen so the plugin can pass it to
    // setWindowFloatingForScreen.
    if (bypassReason == DragBypassReason::AutotileScreen) {
        // Autotile drag-insert: if a preview is live, commit it so the
        // window takes its picked slot in the stack on the next retile.
        // The autotile engine owns final geometry; no float outcome needed.
        if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
            m_autotileEngine->commitDragInsertPreview();
            outcome.action = DragOutcome::NoOp;
            m_draggedWindowId.clear();
            return outcome;
        }
        // Release screen is resolved plugin-side from the cursor position,
        // but we pass the cursor through so the daemon can log it. The
        // plugin passes its own view of "current screen" to the float call.
        outcome.action = DragOutcome::ApplyFloat;
        outcome.targetScreenId.clear(); // plugin resolves from cursor
        outcome.x = cursorX;
        outcome.y = cursorY;
        m_draggedWindowId.clear();
        return outcome;
    }

    // Snapping disabled / context disabled — dead drag. No action.
    if (bypassReason == DragBypassReason::SnappingDisabled || bypassReason == DragBypassReason::ContextDisabled) {
        outcome.action = DragOutcome::NoOp;
        m_draggedWindowId.clear();
        return outcome;
    }

    // Snap path — delegate to legacy dragStopped and translate its
    // out-params into a DragOutcome. dragStopped handles the full
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
    // DragOutcome::validationError).

    int sx = 0, sy = 0, sw = 0, sh = 0;
    bool shouldApply = false;
    bool restoreSizeOnly = false;
    bool snapAssistRequested = false;
    QString releaseScreen;
    EmptyZoneList emptyZones;
    QString resolvedZoneId;
    dragStopped(windowId, cursorX, cursorY, modifiers, mouseButtons, sx, sy, sw, sh, shouldApply, releaseScreen,
                restoreSizeOnly, snapAssistRequested, emptyZones, resolvedZoneId);

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
        outcome.action = restoreSizeOnly ? DragOutcome::RestoreSize : DragOutcome::ApplySnap;
        // Only ApplySnap carries a target zone. RestoreSize is drag-out
        // unsnap (no target zone by definition).
        if (!restoreSizeOnly) {
            outcome.zoneId = resolvedZoneId;
        }
    } else {
        outcome.action = DragOutcome::NoOp;
    }

    m_draggedWindowId.clear();

    // Schedule the snap-assist compute to run after this function returns and
    // the D-Bus reply has been dispatched. QTimer::singleShot(0) queues the
    // call for the next event loop iteration, which happens after Qt finishes
    // handing the DragOutcome off to the D-Bus marshaller.
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
    // stale m_autotileScreens cache to decide when to flip.
    //
    // Full-struct comparison via DragPolicy::operator== catches every
    // policy-relevant transition: bypass-reason flips, autotile→autotile
    // cross-VS (distinct screenIds), and any future per-screen field added
    // to the struct (snap behavior, zone-selector corner, etc.) without
    // touching this site.
    auto resolved = resolveScreenAt(QPointF(cursorX, cursorY));
    const QString cursorScreenId = resolved.screenId;
    if (!cursorScreenId.isEmpty()) {
        const int curDesktop = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
        const QString curActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
        const DragPolicy candidate =
            computeDragPolicy(m_settings, m_autotileEngine, windowId, cursorScreenId, curDesktop, curActivity);
        if (candidate != m_currentDragPolicy) {
            // Log both bypass reason and screenId on each side so same-reason
            // flips (snap→snap or autotile→autotile cross-VS) aren't opaque in
            // the logs — "None @ DP-1 -> None @ DP-2" makes the trigger obvious.
            qCInfo(lcDbusWindow) << "updateDragCursor: policy flip" << m_currentDragPolicy.bypassReason << "@"
                                 << m_currentDragPolicy.screenId << "->" << candidate.bypassReason << "@"
                                 << cursorScreenId;
            m_currentDragPolicy = candidate;
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
    // state stays current. Bypass paths: legacy dragMoved is a harmless
    // no-op because the snap-path state machine isn't initialized (no
    // m_snapCancelled, no m_draggedWindowId processing for bypass drags
    // that never called dragStarted internally).
    dragMoved(windowId, cursorX, cursorY, modifiers, mouseButtons);
}

} // namespace PlasmaZones
