// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file lifecycle.cpp
 * @brief Overlay show/hide/toggle + idle-state lifecycle.
 *
 * Split from overlayservice.cpp to keep each translation unit under the
 * project's <800-line guideline. Owns:
 *   - show / showAtPosition — entry points to make the zone overlay visible,
 *     resolving the cursor's screen and consulting per-VS disabled state
 *   - hide / toggle — symmetric counterparts; hide() destroys overlay
 *     windows rather than hiding them (Vulkan swapchain re-init issue)
 *   - setIdleForDragPause / refreshFromIdle / applyIdleStateForCursor —
 *     drag-mode pause that blanks shader output without tearing down the
 *     QQuickWindows (avoids per-screen Vulkan teardown stalls during
 *     modifier-key thrashing in a drag).
 */

#include "internal.h"
#include "../overlayservice.h"
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
        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        // Check both physical and effective (virtual) screen IDs
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QCursor::pos(), cursorScreen);
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, effectiveId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
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

        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        // Check both physical and effective (virtual) screen IDs
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, effectiveId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                return;
            }
        }
    }

    const QPoint cursorPos(cursorX, cursorY);

    if (m_visible) {
        // One-overlay-per-VS architecture: every VS already has a live
        // overlay window from initializeOverlay. Cross-VS switching is
        // just a matter of flipping per-window _idled state — no
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
        // first-show time. Detect that here — the cursor is now on a non-
        // excluded VS with no live window — and fall through to full init
        // so Phase 3 creates the window. Without this, applyIdleStateForCursor
        // finds nothing to flip and the overlay never becomes visible until
        // the next hide()/show() cycle.
        const bool cursorVsHasWindow = m_screenStates.contains(cursorEffectiveId)
            && m_screenStates.value(cursorEffectiveId).overlayPhysScreen != nullptr;
        if (cursorVsHasWindow || m_excludedScreens.contains(cursorEffectiveId)) {
            m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;
            applyIdleStateForCursor(cursorEffectiveId, showOnAllMonitors);
            return;
        }
        // Fall through — initializeOverlay will (re)build the per-VS window
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
    m_currentOverlayScreenId.clear();

    // Stop shader animation
    stopShaderAnimation();

    // Do NOT invalidate m_shaderTimer - keeps iTime continuous across show/hide
    // so animations feel less predictable and don't restart

    // Post-shell-migration: the shell wl_surface stays mapped across
    // hide/show cycles (its lifecycle is managed by destroyPassiveShell,
    // not per-overlay-toggle). Drive the main-overlay slot's
    // configured hide leg via the SurfaceAnimator so the slot fades
    // out cleanly and the next show() can fade-in. dismissOverlayWindow
    // also clears the per-screen "main overlay active" sentinel
    // (overlayPhysScreen) via setVisible(false) + syncPassiveShellSurfaceState.
    // Gate on overlayPhysScreen — the shell may be alive for OSD-only
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
    // which blocks the main thread on the scene graph render thread — and
    // with modifier-key thrashing during a drag we ended up paying that cost
    // many times per second, stalling D-Bus dispatch long enough for
    // kwin-effect's endDrag to time out and the user to see multi-second lag.
    //
    // Here we only clear the per-window QML properties that drive the shader
    // (zones, zoneCount, highlights). Windows, Vulkan swap chains, and layer
    // surfaces stay alive. On the next activation tick, refreshFromIdle()
    // re-pushes the current zone data — cheap because the labels-texture
    // build is hash-cached on unchanged inputs.
    if (!m_visible) {
        return;
    }
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (!it.value().overlayPhysScreen) {
            continue;
        }
        // _idled and the zone-data properties live on
        // mainOverlaySlot() (PassiveOverlayShell.qml lines
        // 633, 652, 661-662, etc.), not on the shell window root.
        // Writing on the window root creates dynamic properties that
        // QML never observes — the slot's content keeps rendering live
        // zones while the user expects an idle blank.
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
        // Re-evaluate the shell surface's input region now that this
        // screen's main overlay slot is idled. The slot Item stays
        // setVisible(true) (warm RHI pipeline) but
        // `syncPassiveShellSurfaceState`'s isMainOverlayLive predicate
        // treats `_idled` slots as not-blocking-input, so the shell
        // releases its input region for click-through if no other slot
        // (OSD, snap-assist, picker, zone-selector) is up. Without this
        // call the previous show()'s input grab persists for the
        // surface's lifetime even though the user has released the
        // activation trigger.
        syncPassiveShellSurfaceState(it.key());
        // NOTE: labelsTextureHash is intentionally NOT cleared here. The QML
        // side's labelsTexture property still holds the previously-built image
        // (setProperty was never called with a new one); it just isn't sampled
        // while zoneCount is 0. Keeping the hash means refreshFromIdle() with
        // unchanged zones hits the cache and costs one hash compute instead
        // of rebuilding 23 MB of pixels.
    }
    // CRITICAL: mark zone data CLEAN, not dirty. updateShaderUniforms
    // re-runs updateZonesForAllWindows() whenever m_zoneDataDirty is
    // set, which would rebuild the real zones and undo the blank. The
    // idle state is "what we just wrote, do not re-derive from layout
    // data until refreshFromIdle() is called."
    m_zoneDataDirty = false;
    // NOTE: we deliberately do NOT call stopShaderAnimation() here. The
    // shader timer keeps ticking at ~60 Hz while idled, but with zoneCount
    // set to 0 the per-frame work collapses to a handful of uniform uploads
    // to a surface that's rendering no visible geometry — bounded cost, O(1)
    // per screen. Pausing and restarting the timer across the idle cycle
    // would require additional state tracking in refreshFromIdle() and add
    // a startup transient on every modifier re-press. The bounded per-frame
    // cost is the cheaper trade.
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
    updateZonesForAllWindows();
    // Resolve the cursor's current VS — the drag adaptor keeps
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
    // - activeEffectiveId empty → all overlays idled (no active VS —
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
        // Re-evaluate the shell surface's input region after every
        // _idled flip. In single-monitor mode, the cursor's VS gets
        // _idled=false (live, grabs input) and every other VS gets
        // _idled=true (idled, releases input via the
        // isMainOverlayLive predicate in syncPassiveShellSurfaceState).
        // Cross-VS cursor moves during a drag flow through this path,
        // so without the per-flip resync the inactive VS's shell would
        // keep grabbing clicks even though it should be transparent.
        syncPassiveShellSurfaceState(it.key());
    }
}

} // namespace PlasmaZones
