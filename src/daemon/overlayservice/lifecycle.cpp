// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file lifecycle.cpp
 * @brief Overlay show/hide/toggle + idle-state lifecycle.
 *
 * Owns:
 *   - show / showAtPosition - entry points to make the zone overlay visible,
 *     resolving the cursor's screen and consulting per-VS disabled state
 *   - hide / toggle - symmetric counterparts; hide() destroys overlay
 *     windows rather than hiding them (Vulkan swapchain re-init issue)
 *   - setIdleForDragPause / refreshFromIdle / applyIdleStateForCursor -
 *     drag-mode pause that blanks shader output without tearing down the
 *     QQuickWindows (avoids per-screen Vulkan teardown stalls during
 *     modifier-key thrashing in a drag).
 */

#include "internal.h"
#include "daemon/overlayservice.h"
#include "qml_property_names.h"

#include <QCursor>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/Layout.h>

namespace PlasmaZones {

void OverlayService::show()
{
    if (m_visible) {
        return;
    }

    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor
        cursorScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!cursorScreen) {
            // Fallback to primary screen if cursor position detection fails
            cursorScreen = Utils::primaryScreen();
        }
        // If the cursor's screen has no active snapping overlay — disabled, or its
        // default layout is suppressed, or it's in autotile mode — don't show the
        // overlay at all. Mirrors the per-target gate in initializeOverlay.
        // Check both physical and effective (virtual) screen IDs.
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QCursor::pos(), cursorScreen);
            if (isSnappingContextInactive(effectiveId)) {
                return;
            }
        }
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::showAtPosition(int cursorX, int cursorY)
{
    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor using provided coordinates
        // This works on Wayland where QCursor::pos() doesn't work
        cursorScreen = Utils::findScreenAtPosition(cursorX, cursorY);
        if (!cursorScreen) {
            // Fallback to primary screen if no screen contains the cursor position
            cursorScreen = Utils::primaryScreen();
        }

        // If the cursor's screen has no active snapping overlay — disabled, or its
        // default layout is suppressed, or it's in autotile mode — don't show the
        // overlay at all. Mirrors the per-target gate in initializeOverlay.
        // Check both physical and effective (virtual) screen IDs.
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
            if (isSnappingContextInactive(effectiveId)) {
                return;
            }
        }
    }

    const QPoint cursorPos(cursorX, cursorY);

    if (m_visible) {
        // One-overlay-per-VS architecture: every VS already has a live
        // overlay window from initializeOverlay. Cross-VS switching is
        // just a matter of flipping per-window _idled state - no
        // re-init, no rekey, no layer-shell re-anchor. This sidesteps
        // the earlier "wrong spot" bug where rekey moved the map entry
        // but left the layer surface anchored to the previous VS's
        // bounds, and the full NVIDIA vkDestroyDevice deadlock on any
        // destroy path.
        if (!cursorScreen) {
            cursorScreen = Utils::findScreenAtPosition(cursorPos);
        }
        if (!cursorScreen) {
            return;
        }
        const QString cursorEffectiveId =
            Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
        if (cursorEffectiveId.isEmpty()) {
            return;
        }
        // Per-VS toggle (autotile→snap mid-session) leaves us with a VS that
        // is no longer excluded but never had its overlay window created at
        // first-show time. Detect that here - the cursor is now on a non-
        // excluded VS with no live window - and fall through to full init
        // so Phase 3 creates the window. Without this, applyIdleStateForCursor
        // finds nothing to flip and the overlay never becomes visible until
        // the next hide()/show() cycle.
        auto cursorIt = m_screenStates.find(cursorEffectiveId);
        const bool cursorVsHasWindow = cursorIt != m_screenStates.end() && cursorIt->overlayPhysScreen != nullptr;
        // Fast path is only correct when applyIdleStateForCursor can
        // salvage the slot. applyIdleStateForCursor only flips the
        // `_idled` QML property; it doesn't run beginShow / reset
        // opacity. If the slot is mid-hide-animation (opacity~0 from
        // dismissOverlayWindow's animator) or fully dismissed,
        // applyIdleStateForCursor would leave it invisible. Fall
        // through to initializeOverlay in that case so beginShow
        // animates the slot back up.
        //
        // Note: this is a DIFFERENT question from
        // syncPassiveShellSurfaceState's anyVisible predicate (which
        // asks "should the shell wl_surface be mapped at all" using
        // raw slot Item visibility). Both checks are intentional; do
        // not merge them.
        QQuickItem* cursorMainOverlay = cursorVsHasWindow ? cursorIt->mainOverlaySlot() : nullptr;
        const bool cursorSlotVisible =
            cursorMainOverlay != nullptr && !qFuzzyCompare(cursorMainOverlay->opacity(), 0.0);
        if ((cursorVsHasWindow && cursorSlotVisible) || m_excludedScreens.contains(cursorEffectiveId)) {
            m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;
            if (m_overlayIdled) {
                // A trigger-show arriving while warm-idled (a drag-end kept the
                // windows alive but blanked: zones=[], shader + CAVA quiesced).
                // applyIdleStateForCursor() alone only flips _idled, so it would
                // un-blank the slot but leave it showing the empty zone data with
                // a frozen shader. refreshFromIdle() clears m_overlayIdled,
                // restarts the render loop + CAVA, re-pushes zone data, and then
                // applies the cursor idle state (via m_currentOverlayScreenId).
                refreshFromIdle();
            } else {
                applyIdleStateForCursor(cursorEffectiveId, showOnAllMonitors);
            }
            return;
        }
        // Fall through - initializeOverlay will (re)build the per-VS window
        // set against the current excluded set and resume normal operation.
    }

    initializeOverlay(cursorScreen, cursorPos);
}

void OverlayService::hide()
{
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_overlayIdled = false; // not "warm-idled" — fully hidden
    m_currentOverlayScreenId.clear();

    // Stop shader animation
    stopShaderAnimation();
    // Wind down the audio-visualizer capture (deferred by a grace period in
    // syncCavaState so a quick re-show keeps it warm), so an idle daemon does
    // no continuous audio capture or repaint.
    syncCavaState();

    // Do NOT invalidate m_shaderTimer - keeping iTime continuous across
    // show/hide cycles prevents the shader phase from restarting at 0
    // on every re-show, which would make each show visually identical
    // (cycle-restart) rather than continuing the animation in place.

    // Post-shell-migration: the shell wl_surface stays mapped across
    // hide/show cycles (its lifecycle is managed by destroyPassiveShell,
    // not per-overlay-toggle). Drive the main-overlay slot's
    // configured hide leg via the SurfaceAnimator so the slot fades
    // out cleanly and the next show() can fade-in. dismissOverlayWindow
    // also clears the per-screen "main overlay active" sentinel
    // (overlayPhysScreen) via setVisible(false) + syncPassiveShellSurfaceState.
    // Gate on overlayPhysScreen - the shell may be alive for OSD-only
    // screens that never had main overlay attached, and dismissing
    // those would queue 0→0 opacity noise on idle slots.
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& screenId : screenIds) {
        if (!m_screenStates.value(screenId).overlayPhysScreen) {
            continue;
        }
        dismissOverlayWindow(screenId);
    }

    m_pendingShaderError.clear();

    Q_EMIT visibilityChanged(false);
}

void OverlayService::toggle()
{
    if (m_visible) {
        hide();
    } else {
        show();
    }
}

void OverlayService::setIdleForDragPause()
{
    // Blank the overlay's shader output without destroying QQuickWindows.
    // The heavy hide() path pays a ~QQuickWindow Vulkan teardown per screen
    // which blocks the main thread on the scene graph render thread - and
    // with modifier-key thrashing during a drag we ended up paying that cost
    // many times per second, stalling D-Bus dispatch long enough for
    // kwin-effect's endDrag to time out and the user to see multi-second lag.
    //
    // Here we only clear the per-window QML properties that drive the shader
    // (zones, zoneCount, highlights). Windows, Vulkan swap chains, and layer
    // surfaces stay alive. On the next activation tick, refreshFromIdle()
    // re-pushes the current zone data - cheap because the labels-texture
    // build is hash-cached on unchanged inputs.
    if (!m_visible) {
        return;
    }
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (!it.value().overlayPhysScreen) {
            continue;
        }
        // _idled and the zone-data properties live on mainOverlaySlot()
        // (declared on the slot in PassiveOverlayShell.qml), not on the shell
        // window root. Writing on the window root creates dynamic properties that
        // QML never observes - the slot's content keeps rendering live zones
        // while the user expects an idle blank.
        QQuickItem* slot = it.value().mainOverlaySlot();
        if (!slot) {
            continue;
        }
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::Idled), true);
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::Zones), QVariantList());
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::ZoneCount), 0);
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::HighlightedCount), 0);
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::HighlightedZoneId), QString());
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::HighlightedZoneIds), QVariantList());
        // Defensive resync of the shell's input region + mapped state
        // after the _idled flip. The slot Item stays setVisible(true)
        // across drag-pause idles (warm RHI pipeline), so anyVisible
        // does not change; the call is a no-op in the steady state
        // but guards against any path that left input-region state out
        // of sync with what anyInputGrabbing reports.
        syncPassiveShellSurfaceState(it.key());
        // NOTE: labelsTextureHash is intentionally NOT cleared here. The QML
        // side's labelsTexture property still holds the previously-built payload
        // (setProperty was never called with a new one); it just isn't sampled
        // while zoneCount is 0. Keeping the hash means refreshFromIdle() with
        // unchanged zones hits the cache and costs one hash compute instead
        // of rebuilding the sparse glyph-tile payload.
    }
    // CRITICAL: mark zone data CLEAN, not dirty. updateShaderUniforms
    // re-runs updateZonesForAllWindows() whenever m_zoneDataDirty is
    // set, which would rebuild the real zones and undo the blank. The
    // idle state is "what we just wrote, do not re-derive from layout
    // data until refreshFromIdle() is called."
    m_zoneDataDirty = false;

    // Real drag-end routes here too (the QQuickWindows are kept alive to dodge
    // an NVIDIA teardown deadlock — see WindowDragAdaptor), so without this the
    // 60 Hz shader render loop + CAVA would run for the daemon's whole lifetime
    // after the first drag. Mark idle and schedule a grace-period quiesce: a
    // quick re-trigger (modifier thrash) cancels it via refreshFromIdle() and
    // keeps everything warm; a genuine rest stops the render loop + CAVA after
    // the grace window. The windows stay alive, so no teardown cost on resume.
    m_overlayIdled = true;
    scheduleIdleQuiesce();
}

void OverlayService::refreshFromIdle()
{
    // Restore zone data after a setIdleForDragPause() blank and flip
    // the active VS's overlay back to visible.
    //
    // setIdleForDragPause() unconditionally idles every overlay (zones
    // blanked + _idled=true), so refreshFromIdle() re-pushes zone data
    // to all of them and then applies the cursor-based idle state to
    // un-idle the one the cursor is currently on. The L2 labels-texture
    // hash cache keeps the shader-path re-push cheap on unchanged inputs.
    if (!m_visible) {
        return;
    }

    // Coming back from the warm-idled state: cancel any pending quiesce and
    // resume the render loop + CAVA that scheduleIdleQuiesce() may have stopped.
    m_overlayIdled = false;
    if (m_idleQuiesceTimer) {
        m_idleQuiesceTimer->stop();
    }
    if (anyScreenUsesShader() && (!m_shaderUpdateTimer || !m_shaderUpdateTimer->isActive())) {
        ensureShaderTimerStarted(m_shaderTimer, m_shaderTimerMutex, m_lastFrameTime, m_frameCount);
        startShaderAnimation();
    }
    syncCavaState();

    updateZonesForAllWindows();
    // Resolve the cursor's current VS - the drag adaptor keeps
    // m_currentOverlayScreenId updated via showAtPosition, so this
    // reflects the last VS the cursor was observed on.
    const bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();
    applyIdleStateForCursor(m_currentOverlayScreenId, showOnAllMonitors);
}

void OverlayService::applyIdleStateForCursor(const QString& activeEffectiveId, bool showOnAllMonitors)
{
    // One-overlay-per-VS idle state: iterate every live overlay window
    // and flip its _idled QML property based on whether its VS should
    // currently be accepting input / rendering content.
    //
    // - showOnAllMonitors=true  → all overlays un-idled (all VSes active)
    // - showOnAllMonitors=false → only activeEffectiveId un-idled
    // - activeEffectiveId empty → all overlays idled (no active VS -
    //   used by setIdleForDragPause when drag-end hasn't chosen a next
    //   cursor position yet, or when the cursor sits on a disabled VS)
    //
    // The write is idempotent: QML property binding only re-evaluates
    // when the value actually changes, so flipping _idled on a window
    // that's already in the target state is free.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (!it.value().overlayPhysScreen) {
            continue;
        }
        QQuickItem* slot = it.value().mainOverlaySlot();
        if (!slot) {
            continue;
        }
        const bool shouldBeActive =
            showOnAllMonitors || (it.key() == activeEffectiveId && !activeEffectiveId.isEmpty());
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::Idled), !shouldBeActive);
        // Defensive resync of every per-VS shell after the _idled
        // flip. Slot Items stay setVisible(true) across _idled toggles
        // so anyVisible does not change; the call guards against any
        // path that left input-region state out of sync with what
        // anyInputGrabbing reports for cross-VS modal slots during a
        // drag.
        syncPassiveShellSurfaceState(it.key());
    }
}

} // namespace PlasmaZones
