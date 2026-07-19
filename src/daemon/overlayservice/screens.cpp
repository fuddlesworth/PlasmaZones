// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Screen-management methods on OverlayService: setup / remove / hot-plug add /
// remove / physical-screen teardown. Extracted from overlayservice.cpp to keep
// the screen-lifecycle code grouped with itself.

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
#include <QTimer>

namespace PlasmaZones {

void OverlayService::setupForScreen(QScreen* screen)
{
    // Set up overlay windows for all effective screens on this physical screen
    auto* mgr = m_screenManager;
    const QString physId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
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
        m_visible = true; // transient - initializeOverlay may set it false again
    }

    const QString physScreenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);

    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physScreenId)) {
        // Create overlays for each virtual screen on this physical screen
        for (const QString& vsId : mgr->virtualScreenIdsFor(physScreenId)) {
            // Recreate the snap overlay only on virtual screens that have one —
            // skip disabled, suppressed-default, and autotile-mode contexts,
            // matching the overlay activation gate in overlay.cpp.
            if (isSnappingContextInactive(vsId)) {
                continue;
            }
            QRect vsGeom = mgr->screenGeometry(vsId);
            if (vsGeom.isValid()) {
                createOverlayWindow(vsId, screen, vsGeom);
                updateOverlayWindow(vsId, screen);
                const auto& vsState = m_screenStates.value(vsId);
                if (vsState.overlayPhysScreen && vsState.shell) {
                    if (auto* window = vsState.shell->shellWindow()) {
                        assertWindowOnScreen(window, screen, vsGeom);
                        if (vsState.shell->shellSurface() && !vsState.shell->shellSurface()->isLogicallyShown()) {
                            vsState.shell->shellSurface()->show();
                            // Surface::show() clears Qt::WindowTransparentForInput.
                            // Re-assert the input region based on current modal-slot
                            // state so a hot-plug add on a screen with no modal
                            // slot up doesn't leave the shell click-eating.
                            syncPassiveShellSurfaceState(vsId);
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
            if (auto* window = pState.shell->shellWindow()) {
                assertWindowOnScreen(window, screen);
                if (pState.shell->shellSurface() && !pState.shell->shellSurface()->isLogicallyShown()) {
                    pState.shell->shellSurface()->show();
                    syncPassiveShellSurfaceState(physScreenId);
                }
            }
        }
    }

    // Restore m_visible if the caller wasn't already in a visible state and
    // no overlay windows were actually created (transport unavailable, etc.).
    if (!wasVisible && !m_visible) {
        qCDebug(lcOverlay) << "handleScreenAdded: no overlay windows created for" << physScreenId
                           << "- transport may be unavailable";
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
            || (state.shell && state.shell->physScreen() == screen)) {
            destroyOverlayWindow(id);
            destroyZoneSelectorWindow(id);
            destroyPassiveShell(id);
            // Drop the empty state entry once the shell surface is gone -
            // screen hot-plug cycles don't slowly accumulate dead keys.
            // Re-find because the destroy calls above may have invalidated
            // our iterator through the PreDestroyCallback's m_screenStates
            // re-entry.
            auto postIt = m_screenStates.constFind(id);
            if (postIt == m_screenStates.constEnd() || !postIt->shell || !postIt->shell->shellSurface()) {
                m_screenStates.remove(id);
                // Symmetric drop on the lib side - destroyPassiveShell
                // only zeroes the ShellState fields, the entry itself
                // survives in ShellHost's m_states. Without this drop,
                // many hot-plug cycles slowly grow the lib's map with
                // dead keys.
                m_shellHost->removeState(id);
            }

            // The modal singletons (snap assist, layout picker, cheatsheet)
            // track which screen's slot shows them. Destroying that
            // screen's shell just destroyed the slot, so the visible flag
            // and screen id must reset AND the dismissed signals must fire:
            // the daemon's Escape ad-hoc grabs release off those signals,
            // and a stale visible=true would swallow the next toggle press
            // showing nothing. Signals fire even though the slot never
            // animated out — dismissal-on-teardown is part of each
            // signal's documented contract.
            if (id == m_snapAssistScreenId) {
                m_snapAssistVisible = false;
                m_snapAssistScreenId.clear();
                Q_EMIT snapAssistDismissed();
            }
            if (id == m_layoutPickerScreenId) {
                m_layoutPickerVisible = false;
                m_layoutPickerScreenId.clear();
                Q_EMIT layoutPickerDismissed();
            }
            if (id == m_cheatsheetScreenId) {
                m_cheatsheetVisible = false;
                m_cheatsheetScreenId.clear();
                Q_EMIT cheatsheetDismissed();
            }
        }
    }

    // Snap-assist + layout picker post-shell-migration are Item slots
    // inside the per-screen passive shell - destroying the shell
    // (above, via destroyPassiveShell) tears the slots down with
    // it. No separate cleanup needed.

    // Drop notification-window "creation failed" sentinels for screen ids
    // rooted on this physical screen. Without this, if the same physical
    // monitor is reconnected (hot-plug cycle) it inherits the stale flag
    // and we silently refuse to recreate the OSD. Matching is prefix-based
    // because virtual-screen ids embed the physical id as the prefix.
    const QString physId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
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
    // monitor reuses the same id - without this clear, a navigation action
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

void OverlayService::onVirtualScreensChanged(const QString& physicalScreenId)
{
    // Destroy old overlays for this physical screen, recreate with new config.
    QScreen* physScreen = PhosphorScreens::ScreenIdentity::findByIdOrName(physicalScreenId);
    if (!physScreen) {
        // Physical screen removed. Tear down every shell whose key
        // matches the virtual-screen prefix (`physId/vs:N`): this
        // catches all sub-region overlays rooted on the just-removed
        // monitor. The bare physId entry is handled separately below;
        // it has no `/vs:N` suffix so it doesn't match the startsWith
        // filter.
        //
        // Two-pass (collect keys, then destroy + erase) because
        // ShellHost::destroyShell's pre-destroy callback
        // (unwirePassiveShellSlots) re-enters m_screenStates by key.
        // A single-pass loop using `it = erase(it)` would invalidate
        // the iterator while the callback is still reading via that
        // same map.
        const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
        QStringList virtualKeysToDestroy;
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.key().startsWith(prefix)) {
                virtualKeysToDestroy.append(it.key());
            }
        }
        for (const QString& key : virtualKeysToDestroy) {
            m_shellHost->destroyShell(key);
            m_shellHost->removeState(key);
            m_screenStates.remove(key);
        }
        destroyOverlayWindow(physicalScreenId);
        destroyZoneSelectorWindow(physicalScreenId);
        destroyPassiveShell(physicalScreenId);
        m_shellHost->removeState(physicalScreenId);
        m_screenStates.remove(physicalScreenId);
        // Drop sticky creation-failure flags rooted on the now-removed
        // physical monitor. Without this, a same-name replug would
        // inherit the stale flag and silently refuse to recreate.
        // Mirrors the symmetric clear in destroyAllWindowsForPhysicalScreen.
        for (const QString& flagged : m_shellHost->failureScreenIds()) {
            if (flagged == physicalScreenId || flagged.startsWith(prefix)) {
                m_shellHost->clearFailure(flagged);
            }
        }
        return;
    }

    // If the new config HAS virtual screens for this physical ID,
    // destroy any overlay window keyed by the bare physical screen
    // ID itself. Virtual screens use prefixed keys; the bare key
    // would be a leftover from the previous (non-virtual)
    // configuration.
    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physicalScreenId)) {
        destroyOverlayWindow(physicalScreenId);
        destroyZoneSelectorWindow(physicalScreenId);
        destroyPassiveShell(physicalScreenId);
    }

    // Clear selected zone before destroying windows. The selection
    // references zone geometry from the old virtual screen config and
    // would be stale.
    clearSelectedZone();

    // Track whether zone selectors were visible before destruction so
    // we can recreate them for the new virtual screen configuration.
    const bool hadZoneSelector = m_zoneSelectorVisible;

    // Destroy all window types (overlays, selectors, OSDs, snap
    // assist, layout picker).
    destroyAllWindowsForPhysicalScreen(physScreen);

    // Reset zone selector flag. The windows were destroyed, so the
    // flag must be cleared to allow re-showing. Without this, the
    // guard at the top of showZoneSelector() prevents recreation.
    if (hadZoneSelector) {
        m_zoneSelectorVisible = false;
    }

    // Recreate with new virtual screen config if visible.
    if (isVisible()) {
        if (mgr && mgr->hasVirtualScreens(physicalScreenId)) {
            for (const QString& vsId : mgr->virtualScreenIdsFor(physicalScreenId)) {
                QRect vsGeom = mgr->screenGeometry(vsId);
                if (vsGeom.isValid()) {
                    createOverlayWindow(vsId, physScreen, vsGeom);
                }
            }
        } else {
            createOverlayWindow(physScreen);
        }
    }

    // Recreate zone selectors for the new virtual screen
    // configuration. Defer to the next event loop pass to allow
    // PhosphorZones::LayoutRegistry to process assignment migrations
    // for the new virtual screen IDs first, ensuring the zone selector
    // shows the correct layout list.
    if (hadZoneSelector) {
        m_zoneSelectorRecreationPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_zoneSelectorRecreationPending = false;
            // m_zoneSelectorVisible was set to false above (to allow
            // recreation). If an external showZoneSelector() ran
            // during the event loop pass between posting this timer
            // and its execution, it will have set m_zoneSelectorVisible
            // back to true. In that case we must NOT call
            // showZoneSelector() again (double-show). The
            // !m_zoneSelectorVisible guard handles exactly this:
            // false means "no interim show happened, we still need
            // to recreate"; true means "already re-shown, skip".
            if (!m_zoneSelectorVisible) {
                showZoneSelector();
            }
        });
    }
}

} // namespace PlasmaZones
