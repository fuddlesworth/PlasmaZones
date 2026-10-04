// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WindowTrackingAdaptor: focus and activation reports from the effect. The
// cursor and active-window screen, a screen's current desktop, and the
// window activation that feeds focus memory and last-used-zone tracking.

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

namespace PlasmaZones {

void WindowTrackingAdaptor::cursorScreenChanged(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }

    // The KWin effect may send a physical screen ID when virtual screen configs
    // haven't loaded yet.  Resolve to the correct virtual screen using the
    // focused window's daemon-tracked screen assignment as the best hint.
    QString resolvedId = screenId;
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        auto* mgr = m_service->screenManager();
        if (mgr && mgr->hasVirtualScreens(screenId)) {
            // Use focused window's tracked screen as hint. No m_service
            // guard: the deref above already relies on it (ctor-owned,
            // never null).
            if (!m_lastActiveWindowId.isEmpty()) {
                const QString trackedScreen = m_service->screenForWindow(m_lastActiveWindowId);
                if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
                    && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen) == screenId) {
                    resolvedId = trackedScreen;
                }
            }
            // If no window hint, fall back to first virtual screen
            if (!PhosphorIdentity::VirtualScreenId::isVirtual(resolvedId)) {
                QStringList vsIds = mgr->virtualScreenIdsFor(screenId);
                if (!vsIds.isEmpty()) {
                    resolvedId = vsIds.first();
                }
            }
        }
    }

    m_lastCursorScreenId = resolvedId;
    qCDebug(lcDbusWindow) << "Cursor screen changed to" << resolvedId;
}

QString WindowTrackingAdaptor::resolveFocusedWindowScreen(const QString& windowId, const QString& screenId) const
{
    if (screenId.isEmpty() || PhosphorIdentity::VirtualScreenId::isVirtual(screenId) || !m_service) {
        return screenId;
    }
    const QString trackedScreen = m_service->screenForWindow(windowId);
    if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
        && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen)
            == PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)) {
        return trackedScreen;
    }
    return screenId;
}

void WindowTrackingAdaptor::activeWindowScreenChanged(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // Only the focused window's record: a report for any other window (the
    // focus moved on before this one arrived) must not repoint the shortcuts.
    if (shadowWindowId(windowId) != m_lastActiveWindowId) {
        return;
    }
    const QString resolvedScreen = resolveFocusedWindowScreen(windowId, screenId);
    if (resolvedScreen == m_lastActiveScreenId) {
        return;
    }
    qCDebug(lcDbusWindow) << "activeWindowScreenChanged:" << windowId << "from" << m_lastActiveScreenId << "to"
                          << resolvedScreen;
    m_lastActiveScreenId = resolvedScreen;
}

void WindowTrackingAdaptor::screenDesktopChanged(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1 || !m_virtualDesktopManager) {
        return;
    }
    // The effect reports the PHYSICAL screen id, and VirtualDesktopManager keys its
    // per-screen map on that physical id. The daemon asks with EFFECTIVE ids, which
    // on a subdivided output are the vs:N children and never the physical parent, so
    // currentDesktopForScreen resolves an effective id up to its parent output on a
    // key miss before falling back to the global desktop. updateScreenDesktop emits
    // screenDesktopChanged only on a real change (emit-on-change).
    m_virtualDesktopManager->updateScreenDesktop(screenId, desktop);
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowActivated"))) {
        return;
    }

    // Track the active window for daemon-driven navigation (move/focus/swap/etc.)
    m_lastActiveWindowId = shadowWindowId(windowId);

    // Track the active window's screen as fallback for shortcut screen detection.
    // The primary source is now cursorScreenChanged (from KWin effect's mouseChanged).
    // Prefer the daemon-tracked screen assignment (set at snap time) over what the
    // effect reports, since the effect may send a physical ID before VS configs load.
    const QString resolvedScreen = resolveFocusedWindowScreen(windowId, screenId);
    if (!resolvedScreen.isEmpty()) {
        m_lastActiveScreenId = resolvedScreen;
    }

    // Cross-monitor re-home backstop (the analogue of autotile's windowFocused
    // migration): if the snap engine tracks this window but its owning per-key
    // store names a DIFFERENT monitor than the one it activated on, migrate its
    // snap state onto the activation screen so screenForTrackedWindow and the
    // unfloat fallback-screen resolution report the real monitor (#724). Guarded
    // by screensMatch so a mere virtual/physical id-form difference on the same
    // monitor never churns the stores. windowScreenChanged handles the primary
    // drift path; this catches activations that arrive without a screen-change report.
    // A SNAPPED window is left alone: a migrate leaves the zone behind, and an
    // activation report racing a snap commit would silently unsnap it.
    if (PhosphorSnapEngine::SnapEngine* snap = snapEngine(); snap && !resolvedScreen.isEmpty()) {
        const QString owning = snap->screenForTrackedWindow(windowId);
        if (!owning.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(owning, resolvedScreen)
            && snap->zoneForWindow(windowId).isEmpty()) {
            snap->migrateWindowToScreen(windowId, resolvedScreen);
        }
        // Snap's layer-focus memories (the switch verb's "return to the
        // window I was on" candidates) are armed here, on the adaptor that
        // already holds the snap engine — snap is deliberately NOT in the
        // TilingAdaptor lifecycle vector, whose focus relay serves the
        // tiling family. Must run AFTER the migrate above: noteFocused
        // writes through the window's owning store, and the migrate is what
        // re-keys that store onto the activation screen. Accepted side
        // effect: this also populates SnapEngine::m_lastActiveScreenId on
        // every activation (previously never written in production), which
        // activates tier 3 of setWindowFloat's screen-resolution chain —
        // the tier that member was documented to feed. resolvedScreen (not
        // the raw screenId) keeps virtual-screen ids intact for it.
        snap->windowFocused(windowId, resolvedScreen);
    }

    qCDebug(lcDbusWindow) << "Window activated:" << windowId << "on screen" << screenId;

    // Update last-used zone when focusing a snapped window
    // Skip auto-snapped windows - only user-focused windows should update the tracking
    QString zoneId = m_service->zoneForWindow(windowId);
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone()
        && !m_service->isAutoSnapped(windowId)) {
        QString windowClass = m_service->currentAppIdFor(windowId);
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktopForScreen(resolvedScreen));
    }
}

void WindowTrackingAdaptor::forgetClosedFullscreenHold(const QString& windowId)
{
    if (m_cachedScrollEngine) {
        m_cachedScrollEngine->forgetClosedFullscreenHold(windowId);
    }
}

} // namespace PlasmaZones
