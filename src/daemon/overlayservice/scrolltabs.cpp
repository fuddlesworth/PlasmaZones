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

    if (strips.isEmpty()) {
        // Hide-and-unload without creating a shell for a screen that never
        // showed strips.
        auto it = m_screenStates.find(screenId);
        if (it == m_screenStates.end() || !it->scrollTabsSlot() || !it->scrollTabsSlot()->isVisible()) {
            return;
        }
        // Generation-guard the animated hide: a non-empty update landing
        // while this hide is in flight bumps the guard, and the completion
        // below must then NOT tear down the slot it just repopulated —
        // otherwise the indicator stays hidden until the next strip change
        // (which a stable layout never produces). The show path below pairs
        // with this by treating hide-in-flight as "not visible" so it
        // re-runs beginShow (which animates the opacity back up and
        // supersedes the hide track) instead of early-returning on a slot
        // that is still visible but fading out.
        const quint64 hideGeneration = ++m_scrollTabsHideGuard[screenId];
        m_scrollTabsHidePending.insert(screenId);
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ScrollTabs(), [this, screenId, hideGeneration]() {
            if (m_scrollTabsHideGuard.value(screenId) != hideGeneration) {
                return; // superseded by a newer non-empty update
            }
            m_scrollTabsHidePending.remove(screenId);
            auto stateIt = m_screenStates.find(screenId);
            if (stateIt == m_screenStates.end() || !stateIt->scrollTabsSlot()) {
                return;
            }
            stateIt->scrollTabsSlot()->setVisible(false);
            writeQmlProperty(stateIt->scrollTabsSlot(), QStringLiteral("loaded"), false);
            syncPassiveShellSurfaceState(screenId);
        });
        return;
    }
    // Any non-empty update invalidates a pending hide (see the guard above).
    ++m_scrollTabsHideGuard[screenId];
    const bool hideWasInFlight = m_scrollTabsHidePending.remove(screenId);

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
    // Same user-font pipeline as every other overlay slot (OSD, cheatsheet,
    // picker): the pills must honour the overlay font family and size scale.
    writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/false);

    if (slot->isVisible() && !hideWasInFlight) {
        return; // live model update — no show choreography needed
    }
    // Either genuinely hidden, or mid-hide (still visible, opacity
    // animating toward 0). In BOTH cases the show choreography must run:
    // early-returning on a fading slot would leave it visible+loaded at
    // opacity 0 forever once the superseded hide completion no-ops.

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
