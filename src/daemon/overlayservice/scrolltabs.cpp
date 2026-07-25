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
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ScrollTabs(), [this, screenId]() {
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

    if (slot->isVisible()) {
        return; // live model update — no show choreography needed
    }

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
