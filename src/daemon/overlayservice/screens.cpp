// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Screen-management methods on OverlayService: setup / remove /
// hot-plug add / remove / physical-screen teardown. Extracted from
// overlayservice.cpp to keep that TU under the project's <800-line
// cap and to keep the screen-lifecycle code grouped with itself.

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/utils.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorLayer/Surface.h>

#include <QQuickWindow>
#include <QScreen>
#include <QStringList>

namespace PlasmaZones {

void OverlayService::setupForScreen(QScreen* screen)
{
    // Set up overlay windows for all effective screens on this physical screen
    auto* mgr = m_screenManager;
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (mgr && mgr->hasVirtualScreens(physId)) {
        for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
            if (!m_screenStates.contains(vsId) || !m_screenStates[vsId].overlayPhysScreen) {
                QRect vsGeom = mgr->screenGeometry(vsId);
                if (!vsGeom.isValid()) {
                    qCWarning(lcOverlay) << "setupForScreen: invalid geometry for virtual screen" << vsId
                                         << ", skipping overlay creation";
                    continue;
                }
                createOverlayWindow(vsId, screen, vsGeom);
            }
        }
    } else {
        if (!m_screenStates.contains(physId) || !m_screenStates[physId].overlayPhysScreen) {
            createOverlayWindow(screen);
        }
    }
}

void OverlayService::removeScreen(QScreen* screen)
{
    destroyOverlayWindow(screen);
}

void OverlayService::assertWindowOnScreen(QWindow* window, QScreen* screen, const QRect& geometry)
{
    if (!window || !screen) {
        return;
    }
    if (window->screen() != screen) {
        window->setScreen(screen);
    }
    // For virtual screens (geometry differs from physical), positioning is handled by
    // LayerShellQt margins. Calling setGeometry with absolute coordinates would override
    // those margins, causing double-positioning. Only set geometry for physical screens.
    const QRect targetGeom = geometry.isValid() ? geometry : screen->geometry();
    if (targetGeom == screen->geometry()) {
        window->setGeometry(targetGeom);
    }
    // Virtual screens: size is set by the caller; position is set by LayerShellQt margins.
}

void OverlayService::handleScreenAdded(QScreen* screen)
{
    if (!screen) {
        return;
    }

    // Always attempt to recreate overlay windows for reconnected screens,
    // even when m_visible is false. This handles monitor power-cycle recovery:
    // screenRemoved clears the ShellHost creation-failure sentinels
    // (m_shellHost->clearFailure() per prefix-matched id), so when the
    // screen comes back we must retry shell creation. If m_visible is true the
    // existing path also shows the overlay; if false we just ensure the
    // windows exist so the next showAtPosition call finds them ready.
    const bool wasVisible = m_visible;
    if (!wasVisible) {
        m_visible = true; // transient — initializeOverlay may set it false again
    }

    const QString physScreenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);

    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physScreenId)) {
        // Create overlays for each virtual screen on this physical screen
        for (const QString& vsId : mgr->virtualScreenIdsFor(physScreenId)) {
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, vsId, m_currentVirtualDesktop,
                                  m_currentActivity)) {
                continue;
            }
            QRect vsGeom = mgr->screenGeometry(vsId);
            if (vsGeom.isValid()) {
                createOverlayWindow(vsId, screen, vsGeom);
                updateOverlayWindow(vsId, screen);
                const auto& vsState = m_screenStates.value(vsId);
                if (vsState.overlayPhysScreen && vsState.shell) {
                    if (auto* window = vsState.shell->shellWindow) {
                        assertWindowOnScreen(window, screen, vsGeom);
                        if (vsState.shell->shellSurface && !vsState.shell->shellSurface->isLogicallyShown()) {
                            vsState.shell->shellSurface->show();
                        }
                    }
                }
            }
        }
    } else {
        createOverlayWindow(screen);
        updateOverlayWindow(screen);
        const auto& pState = m_screenStates.value(physScreenId);
        if (pState.overlayPhysScreen && pState.shell) {
            if (auto* window = pState.shell->shellWindow) {
                assertWindowOnScreen(window, screen);
                if (pState.shell->shellSurface && !pState.shell->shellSurface->isLogicallyShown()) {
                    pState.shell->shellSurface->show();
                }
            }
        }
    }

    // Restore m_visible if the caller wasn't already in a visible state and
    // no overlay windows were actually created (transport unavailable, etc.).
    if (!wasVisible && !m_visible) {
        qCDebug(lcOverlay) << "handleScreenAdded: no overlay windows created for" << physScreenId
                           << "— transport may be unavailable";
    }
}

void OverlayService::destroyAllWindowsForPhysicalScreen(QScreen* screen)
{
    // Remove all windows associated with this physical screen
    // (includes any virtual screens on this physical screen)
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& id : screenIds) {
        auto it = m_screenStates.constFind(id);
        if (it == m_screenStates.constEnd()) {
            continue;
        }
        const auto& state = it.value();
        if (state.overlayPhysScreen == screen || state.zoneSelectorPhysScreen == screen
            || (state.shell && state.shell->physScreen == screen)) {
            destroyOverlayWindow(id);
            destroyZoneSelectorWindow(id);
            destroyPassiveShell(id);
            // Drop the empty state entry once the shell surface is gone —
            // screen hot-plug cycles don't slowly accumulate dead keys.
            // Re-find because the destroy calls above may have invalidated
            // our iterator through the PreDestroyCallback's m_screenStates
            // re-entry.
            auto postIt = m_screenStates.constFind(id);
            if (postIt == m_screenStates.constEnd() || !postIt->shell || !postIt->shell->shellSurface) {
                m_screenStates.remove(id);
            }
        }
    }

    // Snap-assist + layout picker post-shell-migration are Item slots
    // inside the per-screen passive shell — destroying the shell
    // (above, via destroyPassiveShell) tears the slots down with
    // it. No separate cleanup needed.

    // Drop notification-window "creation failed" sentinels for screen ids
    // rooted on this physical screen. Without this, if the same physical
    // monitor is reconnected (hot-plug cycle) it inherits the stale flag
    // and we silently refuse to recreate the OSD. Matching is prefix-based
    // because virtual-screen ids embed the physical id as the prefix.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (!physId.isEmpty()) {
        const QString vsPrefix = physId + PhosphorIdentity::VirtualScreenId::Separator;
        for (const QString& flagged : m_shellHost->failureScreenIds()) {
            if (flagged == physId || flagged.startsWith(vsPrefix)) {
                m_shellHost->clearFailure(flagged);
            }
        }
    }

    // Drop the dedup sentinel for this physical screen so a hot-plug cycle
    // doesn't suppress the first navigation OSD on the reconnected monitor
    // when it lands inside the implicit 200 ms timeout window. The dedup is
    // keyed on the screenId at time-of-fire, and a removed-then-readded
    // monitor reuses the same id — without this clear, a navigation action
    // on the readded screen within 200 ms of the last action on the
    // pre-removal incarnation gets silently swallowed.
    if (!physId.isEmpty()
        && (m_lastNavigationScreenId == physId
            || m_lastNavigationScreenId.startsWith(physId + PhosphorIdentity::VirtualScreenId::Separator))) {
        m_lastNavigationActionKey.clear();
        m_lastNavigationScreenId.clear();
        m_lastNavigationTime.invalidate();
    }
}

void OverlayService::handleScreenRemoved(QScreen* screen)
{
    destroyAllWindowsForPhysicalScreen(screen);
}

} // namespace PlasmaZones
