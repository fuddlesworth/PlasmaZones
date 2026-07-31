// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Scroll tab-strip indicators — per-screen, display-only pills marking the
// tabs of tabbed scrolling columns. Unlike the cheatsheet/picker this slot
// is NOT a daemon-level singleton (every scrolling screen can carry strips
// concurrently), grabs no input, and updates by plain property writes
// because the model refreshes on every strip relayout.

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

void OverlayService::updateScrollTabStrips(const QString& screenId, const QVariantList& strips)
{
    if (screenId.isEmpty()) {
        return;
    }

    // Cache the live model regardless of the enable toggle, so flipping the
    // indicator back on can replay the current strips immediately — the
    // engine's tabStripsChanged is change-latched and would otherwise stay
    // silent until the next structural strip change. The settings hook
    // (overlayservice/settings.cpp) replays this cache through THIS function
    // on every toggle, and the disabled branch below turns a replay into a
    // hide, so one entry point serves both directions.
    if (strips.isEmpty()) {
        m_lastScrollTabStrips.remove(screenId);
    } else {
        m_lastScrollTabStrips.insert(screenId, strips);
    }
    const bool stripEnabled = !m_settings || m_settings->scrollingTabStripEnabled();

    if (strips.isEmpty() || !stripEnabled) {
        // Hide-and-unload without creating a shell for a screen that never
        // showed strips.
        auto it = m_screenStates.find(screenId);
        if (it == m_screenStates.end()) {
            return;
        }
        QQuickItem* existingSlot = it->scrollTabsSlot();
        if (!existingSlot || !existingSlot->isVisible()) {
            return;
        }
        // A hide is already animating for this screen and its completion owns
        // the teardown. Re-entering hideSlot would bump the guard, making the
        // in-flight completion stale-return, and leave the teardown resting on
        // the animator emitting a fresh completion for the re-entered hide.
        if (m_scrollTabsHidePending.contains(screenId)) {
            return;
        }
        // Generation-guard the animated hide. With animations ENABLED a
        // superseding beginShow pre-cancels this track, and a cancel is not a
        // completion, so the callback below never runs at all. The path the
        // guard actually protects is SurfaceAnimator's animations-DISABLED
        // branch, which salvages a superseded onComplete and fires it
        // SYNCHRONOUSLY from inside the beginShow call on the show path — i.e.
        // after that path has already written loaded=true and setVisible(true),
        // which this callback would otherwise clobber. The show path pairs with
        // the guard by treating hide-in-flight as "not visible" so it re-runs
        // beginShow instead of early-returning on a slot that is fading out.
        const quint64 hideGeneration = ++m_scrollTabsHideGuard[screenId];
        m_scrollTabsHidePending.insert(screenId);
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ScrollTabs(), [this, screenId, hideGeneration]() {
            if (m_scrollTabsHideGuard.value(screenId) != hideGeneration) {
                return; // superseded by a newer non-empty update
            }
            m_scrollTabsHidePending.remove(screenId);
            auto stateIt = m_screenStates.find(screenId);
            if (stateIt == m_screenStates.end()) {
                return;
            }
            QQuickItem* slot = stateIt->scrollTabsSlot();
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
    if (!state || !state->shell || !state->shell->shellSurface() || !state->scrollTabsSlot()) {
        qCWarning(lcOverlay) << "updateScrollTabStrips: no passive shell for screen=" << screenId;
        return;
    }

    // Invalidate a pending hide only once this update is committed to running
    // the show choreography below. Bumping the guard BEFORE the two bails above
    // was unrecoverable: the in-flight hide's completion would stale-return
    // while nothing re-showed the slot, so the animator drove opacity to 0 and
    // left the slot visible+loaded at opacity 0 permanently — and because
    // anyVisible stayed true the wl_surface never unmapped, while every later
    // non-empty update hit the still-visible early return below. Nothing
    // between the bump's old position and here mutates slot state, so deferring
    // it is safe.
    ++m_scrollTabsHideGuard[screenId];
    const bool hideWasInFlight = m_scrollTabsHidePending.remove(screenId);

    // The strip rects arrive in absolute compositor coordinates; the shell
    // window is positioned at the screen origin, so shift into window
    // space here (single conversion point).
    QVariantList shifted;
    shifted.reserve(strips.size());
    for (const QVariant& entry : strips) {
        QVariantMap strip = entry.toMap();
        strip.insert(QStringLiteral("x"), strip.value(QStringLiteral("x")).toInt() - screenGeom.x());
        strip.insert(QStringLiteral("y"), strip.value(QStringLiteral("y")).toInt() - screenGeom.y());
        shifted.append(strip);
    }

    auto* slot = state->scrollTabsSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("strips"), shifted);

    if (slot->isVisible() && !hideWasInFlight) {
        return; // live model update — no show choreography needed
    }
    // Either genuinely hidden, or mid-hide (still visible, opacity
    // animating toward 0). In BOTH cases the show choreography must run:
    // early-returning on a fading slot would leave it visible+loaded at
    // opacity 0 forever once the superseded hide completion no-ops.

    // Same user-font pipeline as every other overlay slot (OSD, cheatsheet,
    // picker): the pills must honour the overlay font family and size scale.
    // Pushed on the show path only, like all five siblings — a strip relayout
    // changes no font, so re-pushing six settings-derived properties on every
    // relayout was pure churn.
    writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/false);

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
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::ScrollTabs, []() { });
    // Display-only: syncPassiveShellSurfaceState counts only modal slots
    // toward the input region, so this restores click-through after
    // Surface::show() cleared the transparent-input flag.
    syncPassiveShellSurfaceStateForSurface(shellSurface);
}

} // namespace PlasmaZones
