// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Scroll drag drop-indicator — the per-screen outline of the slot a dragged
// window would land in while a scrolling drag re-insert is armed.
//
// Why this exists at all, when autotile shows drop feedback without any
// overlay: autotile's feedback IS its live restructure — it moves the window
// in the tiling state on every tick and retiles, so neighbours visibly open a
// gap. The scroll engine deliberately cannot do that. It detaches the window
// once at drag start and applies structure at drop (DETACH-ONCE), because
// restructuring live slid the strip out from under a stationary cursor. That
// leaves the drop target invisible, so it has to be painted.
//
// Display-only: unlike the tab strips next door this installs NO input region.
// It is drawn underneath a cursor that is mid-drag, and taking input there
// would break the very drag it describes.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include "phosphor_roles.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorScreens/Manager.h>

#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

namespace PlasmaZones {

void OverlayService::updateScrollDropIndicator(const QString& screenId, const QRect& rect)
{
    if (screenId.isEmpty()) {
        return;
    }

    // The enable toggle folds into the same "no indicator" answer as an empty
    // rect, so one branch below serves both. No cached-rect replay like the
    // tab strips: this rect only exists for the duration of a drag, and
    // toggling the setting mid-drag to see it appear is not a real workflow.
    const bool indicatorEnabled = !m_settings || m_settings->scrollingDropIndicatorEnabled();
    const bool wantsIndicator = indicatorEnabled && rect.isValid() && !rect.isEmpty();

    // Change-gate on the ABSOLUTE rect, before any of the work below. A drag
    // pushes on every pointer tick and the target only changes when the cursor
    // crosses into a different column or stack slot, so the overwhelmingly
    // common call is a repeat. Without this gate every tick would re-write the
    // QML properties and re-run a surface-state sync for no visual change.
    //
    // Gated on the caller's coordinate space rather than the shell-local one
    // so the comparison stays valid across a screen-geometry change: if the
    // screen moves under a held rect, the shifted value changes while the
    // absolute one does not, and the sync below is what has to re-run.
    const auto cachedIt = m_lastScrollDropIndicatorRect.constFind(screenId);
    const bool hadIndicator = cachedIt != m_lastScrollDropIndicatorRect.constEnd();
    if (wantsIndicator && hadIndicator && cachedIt.value() == rect) {
        return;
    }
    if (!wantsIndicator && !hadIndicator) {
        return; // already clear — do not manufacture a shell to hide nothing
    }

    if (wantsIndicator) {
        m_lastScrollDropIndicatorRect.insert(screenId, rect);
    } else {
        m_lastScrollDropIndicatorRect.remove(screenId);
    }

    if (!wantsIndicator) {
        // Hide-and-unload without creating a shell for a screen that never
        // showed an indicator.
        auto it = m_screenStates.find(screenId);
        if (it == m_screenStates.end()) {
            return;
        }
        QQuickItem* existingSlot = it->scrollDropIndicatorSlot();
        if (!existingSlot || !existingSlot->isVisible()) {
            return;
        }
        // A hide is already animating and its completion owns the teardown.
        // Re-entering hideSlot would bump the guard, making the in-flight
        // completion stale-return, and leave the teardown resting on the
        // animator emitting a fresh completion for the re-entered hide.
        if (m_scrollDropIndicatorHidePending.contains(screenId)) {
            return;
        }
        // Generation-guard the animated hide, same contract as the tab strips:
        // with animations DISABLED, SurfaceAnimator salvages a superseded
        // onComplete and fires it synchronously from inside a later beginShow,
        // i.e. after that path has already written loaded=true and
        // setVisible(true), which this callback would otherwise clobber.
        const quint64 hideGeneration = ++m_scrollDropIndicatorHideGuard[screenId];
        m_scrollDropIndicatorHidePending.insert(screenId);
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ScrollDropIndicator(), [this, screenId, hideGeneration]() {
            if (m_scrollDropIndicatorHideGuard.value(screenId) != hideGeneration) {
                return; // superseded by a newer rect
            }
            m_scrollDropIndicatorHidePending.remove(screenId);
            auto stateIt = m_screenStates.find(screenId);
            if (stateIt == m_screenStates.end()) {
                return;
            }
            QQuickItem* slot = stateIt->scrollDropIndicatorSlot();
            if (!slot) {
                return;
            }
            slot->setVisible(false);
            writeQmlProperty(slot, QStringLiteral("loaded"), false);
            syncPassiveShellSurfaceState(screenId);
        });
        return;
    }

    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        return;
    }
    QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    auto* state = ensurePassiveShellFor(screenId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->scrollDropIndicatorSlot()) {
        qCWarning(lcOverlay) << "updateScrollDropIndicator: no passive shell for screen=" << screenId;
        return;
    }

    // Invalidate a pending hide only once this update is committed to running
    // the show choreography below. Bumping before the bails above would leave
    // the in-flight hide's completion stale-returning while nothing re-showed
    // the slot, stranding it visible+loaded at opacity 0 forever.
    ++m_scrollDropIndicatorHideGuard[screenId];
    const bool hideWasInFlight = m_scrollDropIndicatorHidePending.remove(screenId);

    // The rect arrives in absolute compositor coordinates; the shell window
    // sits at the screen origin, so shift into window space here (single
    // conversion point, matching the tab strips).
    const QRect local = rect.translated(-screenGeom.x(), -screenGeom.y());

    auto* slot = state->scrollDropIndicatorSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("indicatorRect"), local);

    // Paint settings, pushed on every update rather than only on the show
    // path. A drag is short enough that a settings change almost never lands
    // mid-drag, but writing an unchanged QML property emits no change
    // notification, so the cost of being correct here is five compares. EMPTY
    // colours mean "follow the theme" and the content item resolves that.
    if (m_settings) {
        writeQmlProperty(slot, QStringLiteral("indicatorColor"), m_settings->scrollingDropIndicatorColor());
        writeQmlProperty(slot, QStringLiteral("indicatorBorderColor"), m_settings->scrollingDropIndicatorBorderColor());
        writeQmlProperty(slot, QStringLiteral("indicatorOpacity"), m_settings->scrollingDropIndicatorOpacity());
        writeQmlProperty(slot, QStringLiteral("indicatorBorderWidth"), m_settings->scrollingDropIndicatorBorderWidth());
        writeQmlProperty(slot, QStringLiteral("indicatorBorderRadius"),
                         m_settings->scrollingDropIndicatorBorderRadius());
    }

    if (slot->isVisible() && !hideWasInFlight) {
        return; // live rect update — no show choreography, and no input to sync
    }
    // Either genuinely hidden, or mid-hide (still visible, opacity animating
    // toward 0). In BOTH cases the show choreography must run: early-returning
    // on a fading slot would leave it visible+loaded at opacity 0 forever once
    // the superseded hide completion no-ops.

    if (shellWindow) {
        assertWindowOnScreen(shellWindow, screen, screenGeom);
        shellWindow->setWidth(screenGeom.width());
        shellWindow->setHeight(screenGeom.height());
    }
    writeQmlProperty(slot, QStringLiteral("loaded"), true);

    cancelSurfacePrime(shellSurface);
    if (!shellSurface->isLogicallyShown()) {
        shellSurface->show();
    }
    slot->setVisible(true);
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::ScrollDropIndicator, []() { });
    // Re-derive the surface's input state after Surface::show() cleared the
    // transparent-input flag. This slot contributes no region of its own, so
    // the sync is what keeps the shell click-through for the rest of the drag.
    syncPassiveShellSurfaceStateForSurface(shellSurface);
}

} // namespace PlasmaZones
