// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Phase 3 of the v3 drag protocol refactor. Replaces the effect-side
// m_dragBypassedForAutotile / m_cachedZoneSelectorEnabled distributed state
// with a daemon-authoritative beginDrag / endDrag protocol.
//
// The existing dragStarted / dragMoved / dragStopped methods stay alive in
// parallel while the compositor plugin is being ported over. Once nothing
// calls them they'll be deleted (sub-commit 3E).

#include "../windowdragadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/settings_interfaces.h"
#include "../../core/logging.h"
#include "../../autotile/AutotileEngine.h"

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

    const int curDesktop = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    const QString curActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
    const DragPolicy policy =
        computeDragPolicy(m_settings, m_autotileEngine, windowId, startScreenId, curDesktop, curActivity);

    qCInfo(lcDbusWindow) << "beginDrag:" << windowId << "screen=" << startScreenId << "bypass=" << policy.bypassReason
                         << "stream=" << policy.streamDragMoved << "immediateFloat=" << policy.immediateFloatOnStart;

    // Stash bypass reason so the matching endDrag can dispatch to the
    // right branch without re-computing policy (daemon state may have
    // changed mid-drag, but endDrag should act on the policy that was
    // in force when the drag started, unless cross-VS flip replaced it
    // via dragPolicyChanged — sub-commit 3d).
    m_currentDragBypassReason = policy.bypassReason;

    if (!policy.bypassReason.isEmpty()) {
        // Bypass path — record the id so the matching endDrag can find us,
        // but skip the full snap-path setup (overlay, zone state, etc.).
        m_draggedWindowId = windowId;
        m_originalGeometry = QRect(frameX, frameY, frameWidth, frameHeight);
        m_snapCancelled = false;
        m_wasSnapped = false;
        return policy;
    }

    // Snap path — delegate to the legacy dragStarted to set up the rest of
    // the state machine (pre-parsed triggers, wasSnapped check, zone state
    // reset). This wraps the existing logic rather than duplicating it so
    // the snap path stays behavior-identical during the migration.
    dragStarted(windowId, static_cast<double>(frameX), static_cast<double>(frameY), static_cast<double>(frameWidth),
                static_cast<double>(frameHeight), mouseButtons);

    // dragStarted may have cleared m_draggedWindowId if snapping is actually
    // disabled (guard is inside dragStarted for the legacy path); reflect
    // that back in the policy so the plugin gets the correct answer.
    if (m_draggedWindowId != windowId) {
        DragPolicy fallback;
        fallback.screenId = startScreenId;
        fallback.bypassReason = QStringLiteral("snapping_disabled");
        m_currentDragBypassReason = fallback.bypassReason;
        return fallback;
    }

    return policy;
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
        // Captured zone id is the primary zone from multi-zone snap or
        // the single zone for a regular snap. dragStopped cleared its
        // m_currentZoneId, so we can't read it here — but the plugin
        // already knows the zone from its own view of daemon-emitted
        // zoneGeometryDuringDragChanged signals during the drag, and
        // DragOutcome's zoneId is informational only for painting.
    } else {
        outcome.action = DragOutcome::NoOp;
    }

    m_draggedWindowId.clear();
    return outcome;
}

void WindowDragAdaptor::updateDragCursor(const QString& windowId, int cursorX, int cursorY, int modifiers,
                                         int mouseButtons)
{
    if (windowId.isEmpty() || windowId != m_draggedWindowId) {
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
