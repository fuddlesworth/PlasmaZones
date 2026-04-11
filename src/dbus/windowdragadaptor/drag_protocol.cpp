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
        return fallback;
    }

    return policy;
}

} // namespace PlasmaZones
