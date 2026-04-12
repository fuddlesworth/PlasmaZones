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
#include "../../core/layoutmanager.h"
#include "../../core/settings_interfaces.h"
#include "../../core/logging.h"
#include "../../autotile/AutotileEngine.h"
#include <QGuiApplication>

namespace PlasmaZones {

DragPolicy WindowDragAdaptor::computeDragPolicy(const ISettings* settings, const AutotileEngine* autotileEngine,
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
        policy.bypassReason = QStringLiteral("context_disabled");
        return policy;
    }

    // 2) Autotile screen — the autotile engine owns window placement. The
    //    plugin applies handleDragToFloat immediately if the window is
    //    currently tiled, so the user sees the free-floating size restored
    //    during the interactive move (not deferred to drop).
    if (autotileEngine && !screenId.isEmpty() && autotileEngine->isAutotileScreen(screenId)) {
        policy.bypassReason = QStringLiteral("autotile_screen");
        policy.captureGeometry = true; // preserve pre-autotile size for unfloat restore
        if (!windowId.isEmpty()) {
            policy.immediateFloatOnStart = autotileEngine->isWindowTracked(windowId);
        }
        return policy;
    }

    // 3) Top-level snapping disabled. Dead drag on any non-autotile screen —
    //    the user configured the tool with snap mode off. beginDrag still
    //    returns a policy so endDrag can clean up consistently.
    if (settings && !settings->snappingEnabled()) {
        policy.bypassReason = QStringLiteral("snapping_disabled");
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
    policy.bypassReason.clear();
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
    // cleanly must not bleed into this one.
    clearPendingSnapDragState();

    const int curDesktop = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    const QString curActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
    const DragPolicy policy =
        computeDragPolicy(m_settings, m_autotileEngine, windowId, startScreenId, curDesktop, curActivity);

    qCInfo(lcDbusWindow) << "beginDrag:" << windowId << "screen=" << startScreenId << "bypass=" << policy.bypassReason
                         << "stream=" << policy.streamDragMoved << "immediateFloat=" << policy.immediateFloatOnStart;

    // Stash bypass reason so the matching endDrag can dispatch to the
    // right branch without re-computing policy.
    m_currentDragBypassReason = policy.bypassReason;

    if (!policy.bypassReason.isEmpty()) {
        // Bypass path — record the id so the matching endDrag can find us,
        // but skip the full snap-path setup (overlay, zone state, etc.).
        m_draggedWindowId = windowId;
        m_originalGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
        m_snapCancelled = false;
        m_wasSnapped = false;
        // Autotile drag-insert lives on the bypass path (cursor on an
        // autotile screen). dragMoved's drag-insert preview block reads
        // m_cachedAutotileDragInsertTriggers, so cache them here — the
        // legacy dragStarted setup that normally does this never runs for
        // bypass drags. Any stale preview from a prior drag is cleared too.
        if (m_settings) {
            m_cachedAutotileDragInsertTriggers = parseTriggers(m_settings->autotileDragInsertTriggers());
        }
        if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
            m_autotileEngine->cancelDragInsertPreview();
        }
        return policy;
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
        m_cachedZoneSpanTriggers = parseTriggers(m_settings->zoneSpanTriggers());
    }

    m_pendingSnapDragWindowId = windowId;
    m_pendingSnapDragGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
    m_pendingSnapDragMouseButtons = mouseButtons;
    m_pendingSnapDragWasSnapped = m_windowTracking && !m_windowTracking->getZoneForWindow(windowId).isEmpty();

    return policy;
}

bool WindowDragAdaptor::activateSnapDragIfNeeded(int modifiers, int mouseButtons)
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
    if (!triggerHeld && !toggleMode) {
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

    qCInfo(lcDbusWindow) << "activateSnapDragIfNeeded: promoting" << pendingId << "to active drag";
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
        m_currentDragBypassReason.clear();
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

    const QString bypassReason = m_currentDragBypassReason;
    m_currentDragBypassReason.clear(); // next drag starts fresh

    qCInfo(lcDbusWindow) << "endDrag:" << windowId << "cursor=" << cursorX << cursorY << "cancelled=" << cancelled
                         << "bypass=" << bypassReason;

    // Cancelled drags on any path: plugin should simply clean up. For
    // autotile bypass, there's no zone/snap state to unwind. For snap path,
    // we still delegate to legacy dragStopped so it can reset its own
    // internal state machine, then emit CancelSnap back.
    if (cancelled) {
        if (bypassReason.isEmpty()) {
            // Snap path cancelled — run dragStopped through the legacy
            // adaptor so overlay/zone-state cleanup happens, but discard
            // the geometry outputs.
            int sx = 0, sy = 0, sw = 0, sh = 0;
            bool shouldApply = false;
            bool restoreSizeOnly = false;
            bool snapAssistRequested = false;
            QString releaseScreen;
            EmptyZoneList emptyZones;
            dragStopped(windowId, cursorX, cursorY, modifiers, mouseButtons, sx, sy, sw, sh, shouldApply, releaseScreen,
                        restoreSizeOnly, snapAssistRequested, emptyZones);
        } else {
            // Bypass paths — no overlay state to clean up; just drop the id.
            // An active autotile drag-insert preview must be cancelled so
            // neighbours snap back to their original order.
            if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
                m_autotileEngine->cancelDragInsertPreview();
            }
            m_draggedWindowId.clear();
        }
        outcome.action = DragOutcome::CancelSnap;
        return outcome;
    }

    // Autotile bypass — plugin will float the window at the drop location.
    // Daemon has no placement decision to make; the outcome just carries
    // the release screen so the plugin can pass it to
    // setWindowFloatingForScreen.
    if (bypassReason == QLatin1String("autotile_screen")) {
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
    if (bypassReason == QLatin1String("snapping_disabled") || bypassReason == QLatin1String("context_disabled")) {
        outcome.action = DragOutcome::NoOp;
        m_draggedWindowId.clear();
        return outcome;
    }

    // Snap path — delegate to legacy dragStopped and translate its
    // out-params into a DragOutcome. dragStopped handles the full
    // dispatch matrix (snap-to-zone, drag-out unsnap, cross-screen
    // cleanup, zone selector, snap assist).
    //
    // Snapshot the active zone id BEFORE dragStopped runs — the cleanup
    // path inside dragStopped clears m_currentZoneId via
    // hideOverlayAndClearZoneState, and we want that id exposed on the
    // outcome so the daemon is authoritative for every DragOutcome field.
    const QString capturedZoneId = m_currentZoneId;

    int sx = 0, sy = 0, sw = 0, sh = 0;
    bool shouldApply = false;
    bool restoreSizeOnly = false;
    bool snapAssistRequested = false;
    QString releaseScreen;
    EmptyZoneList emptyZones;
    dragStopped(windowId, cursorX, cursorY, modifiers, mouseButtons, sx, sy, sw, sh, shouldApply, releaseScreen,
                restoreSizeOnly, snapAssistRequested, emptyZones);

    outcome.targetScreenId = releaseScreen;
    outcome.x = sx;
    outcome.y = sy;
    outcome.width = sw;
    outcome.height = sh;
    outcome.requestSnapAssist = snapAssistRequested;
    outcome.emptyZones = emptyZones;

    if (shouldApply) {
        outcome.action = restoreSizeOnly ? DragOutcome::RestoreSize : DragOutcome::ApplySnap;
        // Primary zone captured pre-cleanup. Empty on RestoreSize (drag-out
        // unsnap has no target zone), populated on ApplySnap.
        if (!restoreSizeOnly) {
            outcome.zoneId = capturedZoneId;
        }
    } else {
        outcome.action = DragOutcome::NoOp;
    }

    m_draggedWindowId.clear();
    return outcome;
}

void WindowDragAdaptor::updateDragCursor(const QString& windowId, int cursorX, int cursorY, int modifiers,
                                         int mouseButtons)
{
    if (windowId.isEmpty()) {
        return;
    }

    // Pending snap-path drag: try to activate if the trigger is now held.
    // If it's still pending after the check, return — the daemon does no
    // overlay work until the user commits to zone selection.
    if (m_draggedWindowId.isEmpty() && m_pendingSnapDragWindowId == windowId) {
        if (!activateSnapDragIfNeeded(modifiers, mouseButtons)) {
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
    auto resolved = resolveScreenAt(QPointF(cursorX, cursorY));
    const QString cursorScreenId = resolved.screenId;
    if (!cursorScreenId.isEmpty()) {
        const int curDesktop = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
        const QString curActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
        const DragPolicy candidate =
            computeDragPolicy(m_settings, m_autotileEngine, windowId, cursorScreenId, curDesktop, curActivity);
        if (candidate.bypassReason != m_currentDragBypassReason
            || (candidate.bypassReason == QLatin1String("autotile_screen") && candidate.screenId != cursorScreenId)) {
            qCInfo(lcDbusWindow) << "updateDragCursor: policy flip" << m_currentDragBypassReason << "->"
                                 << candidate.bypassReason << "on" << cursorScreenId;
            m_currentDragBypassReason = candidate.bypassReason;
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
