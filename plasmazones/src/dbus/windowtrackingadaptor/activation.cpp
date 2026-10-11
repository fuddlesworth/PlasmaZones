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

void WindowTrackingAdaptor::holdScreenReport(const QString& key, const QString& screenId, std::function<void()> replay)
{
    qCDebug(lcDbusWindow) << "Holding a report naming the unknown screen" << screenId << "under" << key;
    m_heldScreenReports.insert(key, HeldScreenReport{screenId, std::move(replay)});
}

void WindowTrackingAdaptor::replayHeldScreenReports()
{
    const QHash<QString, HeldScreenReport> held = m_heldScreenReports;
    for (auto it = held.constBegin(); it != held.constEnd(); ++it) {
        if (!resolveBusScreen(it->screenId).isEmpty() && m_heldScreenReports.contains(it.key())) {
            m_heldScreenReports.remove(it.key());
            it->replay();
        }
    }
}

void WindowTrackingAdaptor::cursorScreenChanged(const QString& screenId)
{
    m_heldScreenReports.remove(QStringLiteral("cursor")); // a later report supersedes a held one
    if (screenId.isEmpty()) {
        return;
    }
    // The effect can send a physical id before the virtual-screen definitions
    // load; resolveBusScreen maps it into the split monitor, and a screen the
    // daemon does not know yet is held rather than stored (F81).
    const QString resolvedId = resolveBusScreen(screenId, m_lastActiveWindowId);
    if (resolvedId.isEmpty()) {
        holdScreenReport(QStringLiteral("cursor"), screenId, [this, screenId] {
            cursorScreenChanged(screenId);
        });
        return;
    }
    m_lastCursorScreenId = resolvedId;
    qCDebug(lcDbusWindow) << "Cursor screen changed to" << resolvedId;
}

QString WindowTrackingAdaptor::resolveFocusedWindowScreen(const QString& windowId, const QString& screenId) const
{
    if (screenId.isEmpty() || !m_service) {
        return screenId;
    }
    const QString trackedScreen = m_service->screenForWindow(windowId);
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        && PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
        && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen)
            == PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)) {
        return trackedScreen;
    }
    return resolveBusScreen(screenId, windowId);
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
    m_heldScreenReports.remove(QStringLiteral("active"));
    const QString resolvedScreen = resolveFocusedWindowScreen(windowId, screenId);
    if (resolvedScreen.isEmpty()) {
        holdScreenReport(QStringLiteral("active"), screenId, [this, windowId, screenId] {
            activeWindowScreenChanged(windowId, screenId);
        });
        return;
    }
    if (resolvedScreen == m_lastActiveScreenId) {
        return;
    }
    qCDebug(lcDbusWindow) << "activeWindowScreenChanged:" << windowId << "from" << m_lastActiveScreenId << "to"
                          << resolvedScreen;
    m_lastActiveScreenId = resolvedScreen;
    // Snap's own record of the focused screen follows, the one its float
    // verb resolves a screen from (F28, F143). No migrate here: this report
    // fires at every crossing of a drag, and the move itself is settled by
    // the drag's end or the effect's crossing notice (F732).
    if (PhosphorSnapEngine::SnapEngine* snap = snapEngine()) {
        snap->windowFocused(windowId, resolvedScreen);
    }
}

void WindowTrackingAdaptor::screenDesktopChanged(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1 || !m_virtualDesktopManager) {
        return;
    }
    // A desktop past the last one is refused, as moveWindowToDesktop does.
    // An unknown screen needs no hold: the map below is keyed by the physical
    // id, and a screen that arrives later reads its entry (F81, F765).
    if (const int count = desktopCount(); count > 0 && desktop > count) {
        qCDebug(lcDbusWindow) << "screenDesktopChanged: desktop" << desktop << "past the last" << count;
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

void WindowTrackingAdaptor::seedScreenDesktop(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1 || !m_virtualDesktopManager
        || (desktopCount() > 0 && desktop > desktopCount())) {
        return;
    }
    m_virtualDesktopManager->seedScreenDesktop(screenId, desktop);
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowActivated"))) {
        return;
    }

    // Track the active window for daemon-driven navigation (move/focus/swap/etc.)
    m_lastActiveWindowId = shadowWindowId(windowId);
    m_heldScreenReports.remove(QStringLiteral("active"));

    // The focused window's screen, which window shortcuts act on (the cursor's
    // screen is their fallback when nothing has focus). A tracked virtual
    // screen of the reported monitor wins, since the effect can send a
    // physical id before the virtual-screen definitions load. A screen the
    // daemon does not know yet is held, and the rest of the activation waits
    // for it (F81, F302).
    const QString resolvedScreen = resolveFocusedWindowScreen(windowId, screenId);
    if (resolvedScreen.isEmpty() && !screenId.isEmpty()) {
        holdScreenReport(QStringLiteral("active"), screenId, [this, windowId, screenId] {
            if (shadowWindowId(windowId) == m_lastActiveWindowId) {
                windowActivated(windowId, screenId);
            }
        });
        return;
    }
    if (!resolvedScreen.isEmpty()) {
        m_lastActiveScreenId = resolvedScreen;
    }

    // Cross-monitor re-home backstop for a snap FLOAT (#724): snap tracks the
    // window floating on another monitor than the one it activated on, which
    // a move that sent no screen report leaves behind; re-key it there so
    // the float verbs and the unfloat fallback read the monitor it is on.
    // screensMatch absorbs a connector-name or EDID spelling of one monitor, and
    // a physical id reported for a split monitor was mapped to the virtual
    // screen snap tracks by resolveFocusedWindowScreen above. A window holding
    // a zone in ANY store is left alone (a
    // migrate leaves the zone behind, and an activation racing a snap commit
    // would unsnap it, F259/F77/F125), and so is an activation on a screen
    // snap does not run: a tiling destination owns the window there (F679).
    if (PhosphorSnapEngine::SnapEngine* snap = snapEngine(); snap && !resolvedScreen.isEmpty()) {
        const QString owning = snap->screenForTrackedWindow(windowId);
        if (!owning.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(owning, resolvedScreen)
            && WindowTrackingInternal::snapZoneScreen(snap, windowId, QString()).isEmpty()
            && snap->isActiveOnScreen(resolvedScreen)) {
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

    // Update last-used zone when focusing a snapped window, only where snap
    // runs the screen and holds the window on it: a zone remembered for
    // another monitor, or under a tiling screen, is not the last zone used
    // here (F222). Auto-snapped windows are skipped: only the user's focus counts.
    QString zoneId = m_service->zoneForWindow(windowId);
    PhosphorSnapEngine::SnapEngine* snapForLastUsed = snapEngine();
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone() && !m_service->isAutoSnapped(windowId)
        && snapForLastUsed && snapForLastUsed->isActiveOnScreen(resolvedScreen)
        && PhosphorScreens::ScreenIdentity::screensMatch(snapForLastUsed->screenForTrackedWindow(windowId),
                                                         resolvedScreen)) {
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
