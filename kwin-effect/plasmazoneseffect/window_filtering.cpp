// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>

#include "../navigationhandler.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                                    const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!isDaemonReady("ensure pre-snap geometry")) {
        return;
    }

    // Use pre-captured geometry if provided, otherwise read from window.
    QRectF geom = preCapturedGeometry.isValid() ? preCapturedGeometry : w->frameGeometry();
    if (geom.width() <= 0 || geom.height() <= 0) {
        return;
    }

    // Use virtual-screen-aware ID — getWindowScreenId() falls back to the physical
    // ID when virtual screen defs haven't loaded yet, so it is safe to call
    // unconditionally. Using it here ensures the stored screen ID always matches
    // the ID used by later lookups.
    const QString screenId = getWindowScreenId(w);

    // Post the store directly with overwrite=false. The daemon's storePreTileGeometry
    // enforces per-windowId idempotency — a second capture for the same runtime
    // instance is a no-op. We deliberately skip the prior async hasPreTileGeometry
    // pre-check: that path matched on appId too, so a stale cross-session entry from
    // a prior window instance (keyed by appId) would block the fresh per-instance
    // capture and freeze float-restore at ancient coordinates.
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
        {windowId, static_cast<int>(geom.x()), static_cast<int>(geom.y()), static_cast<int>(geom.width()),
         static_cast<int>(geom.height()), screenId, false},
        QStringLiteral("storePreTileGeometry"));
    qCInfo(lcEffect) << "Stored pre-tile geometry for window" << windowId << "geom=" << geom;
}

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap(bool filterHandleable) const
{
    const auto windows = KWin::effects->stackingOrder();
    QHash<QString, KWin::EffectWindow*> windowMap;
    windowMap.reserve(windows.size());
    for (KWin::EffectWindow* w : windows) {
        if (w && (!filterHandleable || shouldHandleWindow(w))) {
            windowMap[getWindowId(w)] = w;
        }
    }
    return windowMap;
}

KWin::EffectWindow* PlasmaZonesEffect::getValidActiveWindowOrFail(const QString& action)
{
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for" << action;
        emitNavigationFeedback(false, action, QStringLiteral("no_window"));
        return nullptr;
    }
    return activeWindow;
}

bool PlasmaZonesEffect::isWindowFloating(const QString& windowId) const
{
    return m_navigationHandler->isWindowFloating(windowId);
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    // Never snap our own overlay/editor windows (but allow the settings app)
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return false;
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return false;
    }

    // Plasma shell layer-shell surfaces — see isPlasmaShellSurface() for rationale.
    if (isPlasmaShellSurface(windowClass)) {
        return false;
    }

    // Check user-configured exclusion lists (needed for drag gating — daemon also enforces
    // for keyboard nav, but the effect must filter for drag operations and lifecycle reporting)
    if (!m_excludedApplications.isEmpty() || !m_excludedWindowClasses.isEmpty()) {
        KWin::Window* kw = w->window();
        const QString appName = kw ? kw->desktopFileName() : QString();
        if (matchesExclusionLists(appName, windowClass, m_excludedApplications, m_excludedWindowClasses)) {
            return false;
        }
    }

    // Skip special / non-manageable window types (inherently effect-side — KWin metadata)
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()) {
        return false;
    }

    // Skip transient/dialog windows unconditionally. Dialogs, utilities, tooltips,
    // notifications, etc. should never be zone-managed. User-configured exclusion
    // lists and minimum size checks are handled by the daemon.
    //
    // transientFor() catches child surfaces that Electron/CEF apps (Steam, Discord,
    // VS Code) spawn for image previews, context menus and popups: these frequently
    // fail to report an accurate KWin window type (isDialog/isPopupWindow stay
    // false) but always set the transient-parent relationship. Without this the
    // popup passes the filter and gets snapped to a zone (discussion #461 item 11).
    // Mirrors the transient bucket already enforced by shouldAnimateWindow() and
    // isTileableWindow().
    if (w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow() || w->transientFor()) {
        return false;
    }

    return true;
}

bool PlasmaZonesEffect::matchesExclusionLists(const QString& appName, const QString& windowClass,
                                              const QStringList& apps, const QStringList& classes)
{
    for (const QString& excluded : apps) {
        if (!excluded.isEmpty() && appName.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }
    for (const QString& excluded : classes) {
        if (!excluded.isEmpty() && windowClass.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool PlasmaZonesEffect::shouldAnimateWindow(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    const QString windowClass = w->windowClass();

    // Structural non-window surfaces — panels (docks), the desktop,
    // plasmoid / Plasma-shell surfaces, and other special or
    // skip-switcher windows are never application windows; a
    // window-event shader on them is always wrong. Hard-excluded with
    // no toggle and ahead of the rule-override path, mirroring the
    // structural rejections `shouldHandleWindow()` already applies.
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher()
        || isPlasmaShellSurface(windowClass)) {
        return false;
    }

    // Rule-override path. ANY AnimationAppRule whose classPattern
    // substring-matches the window's class signals deliberate user
    // intent to animate this app — kind (Shader vs Timing) and
    // eventPath are not narrowed here because the user's act of
    // creating a class-targeted rule is itself the opt-in signal.
    // Empty `windowClass` short-circuits (no rule can match an
    // unidentified window) and an empty rule list short-circuits
    // (no override possible) so the default-state path pays just
    // two pointer reads. Same case-insensitive substring rule
    // `AnimationAppRuleList::firstMatchOfKind` uses, so the override
    // scope mirrors the per-rule match contract exactly.
    if (!windowClass.isEmpty()) {
        const auto& rules = m_shaderManager.appRules();
        if (!rules.isEmpty()) {
            for (const auto& rule : rules.entries()) {
                if (!rule.classPattern.isEmpty() && windowClass.contains(rule.classPattern, Qt::CaseInsensitive)) {
                    return true;
                }
            }
        }
    }

    // Notification and OSD surfaces — excluded by default via the
    // Window Filtering toggle (animationExcludeNotificationsAndOsd).
    // Most shell notifications/OSDs are layer-shell surfaces already
    // rejected by the structural block above; this catches the
    // remainder — notification windows some apps spawn as ordinary
    // toplevels. Placed after the rule-override so a class-targeted
    // rule can still re-enable them, mirroring the transient filter.
    if (m_animationExcludeNotificationsAndOsd && (w->isNotification() || w->isOnScreenDisplay())) {
        return false;
    }

    // Transient-window filter — covers dialogs / popups / tooltips /
    // dropdowns / menus / utility windows. Mirrors the snapping
    // exclusion's transient bucket, plus the popup-window family that
    // KWin distinguishes (PopupMenu, DropdownMenu, Tooltip) so a user
    // who wants the popup category animated can still opt out of these
    // sub-types via the toggle.
    if (m_animationExcludeTransientWindows) {
        if (w->isDialog() || w->isUtility() || w->isPopupWindow() || w->isPopupMenu() || w->isDropdownMenu()
            || w->isTooltip() || w->isMenu() || w->isSplash() || w->transientFor()) {
            return false;
        }
    }

    // Min-size filter — windows narrower or shorter than the threshold
    // are excluded. Zero (the default) disables each axis independently
    // so a user can set just one bound. Frame geometry is read live —
    // during minimize/close lifecycle a window may already be collapsed
    // when this fires, which is acceptable: the user opted into the
    // size threshold so a transient sub-threshold frame should suppress
    // the animation consistently with explicit-size cases.
    const QRectF frame = w->frameGeometry();
    if (m_animationMinWindowWidth > 0 && frame.width() < m_animationMinWindowWidth) {
        return false;
    }
    if (m_animationMinWindowHeight > 0 && frame.height() < m_animationMinWindowHeight) {
        return false;
    }

    // User-configured exclusion lists — substring-matched via the
    // shared `matchesExclusionLists` helper that `shouldHandleWindow`
    // also uses, so the animation and snapping filter sets stay in
    // lockstep on match semantics even though their lists are
    // independent.
    if (!m_animationExcludedApplications.isEmpty() || !m_animationExcludedWindowClasses.isEmpty()) {
        KWin::Window* kw = w->window();
        const QString appName = kw ? kw->desktopFileName() : QString();
        if (matchesExclusionLists(appName, windowClass, m_animationExcludedApplications,
                                  m_animationExcludedWindowClasses)) {
            return false;
        }
    }

    return true;
}

bool PlasmaZonesEffect::isTileableWindow(KWin::EffectWindow* w) const
{
    // Reject menus, popups, tooltips, modals, and transient children.
    // Electron apps (Vesktop, VS Code, Discord) create separate windows
    // for context menus and dropdowns that pass shouldHandleWindow() but
    // must never enter the autotile tree.
    if (!w->isNormalWindow() || w->isModal() || w->isPopupWindow() || w->isDropdownMenu() || w->isPopupMenu()
        || w->isTooltip() || w->isMenu() || w->transientFor()) {
        return false;
    }
    // Reject keep-above windows — overlay/utility tools (Spectacle, color
    // pickers, screen rulers, etc.) set keep-above and should not enter the
    // autotile tree or receive auto-focus. Without this guard, opening
    // Spectacle while focusNewWindows is enabled disrupts the tiled layout.
    if (w->keepAbove()) {
        return false;
    }
    return true;
}

bool PlasmaZonesEffect::isEligibleForTilingNotify(KWin::EffectWindow* w) const
{
    if (!w || !shouldHandleWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not handleable)"
                          << (w ? getWindowId(w) : QStringLiteral("null"));
        return false;
    }
    if (!isTileableWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not tileable)" << getWindowId(w);
        return false;
    }
    if (w->isMinimized()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (minimized)" << getWindowId(w);
        return false;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (wrong desktop/activity)" << getWindowId(w);
        return false;
    }
    // Reject windows smaller than the user-configured minimum size.
    // Prevents small utility windows (emoji picker, color picker, etc.)
    // from entering the tiling tree and disrupting the layout.
    const QRectF frame = w->frameGeometry();
    if ((m_cachedMinWindowWidth > 0 && frame.width() < m_cachedMinWindowWidth)
        || (m_cachedMinWindowHeight > 0 && frame.height() < m_cachedMinWindowHeight)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (too small)" << getWindowId(w)
                          << "size=" << frame.size() << "threshold=" << m_cachedMinWindowWidth << "x"
                          << m_cachedMinWindowHeight;
        return false;
    }
    qCDebug(lcEffect) << "isEligibleForTilingNotify: accepted" << getWindowId(w) << "size=" << frame.size()
                      << "class=" << w->windowClass() << "skipSwitcher=" << w->isSkipSwitcher()
                      << "keepAbove=" << w->keepAbove() << "transient=" << (w->transientFor() != nullptr);
    return true;
}

bool PlasmaZonesEffect::hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    QString windowClass = w->windowClass();
    pid_t windowPid = w->pid();

    // Check all existing windows for same class but different PID
    // This detects when another app (e.g., Cachy Update) spawns a window
    // of a class that the user has previously snapped (e.g., Ghostty)
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* other : windows) {
        if (other == w) {
            continue; // Skip self
        }
        if (!shouldHandleWindow(other)) {
            continue; // Skip non-managed windows
        }
        if (other->windowClass() == windowClass && other->pid() != windowPid) {
            // Found another window of the same class with different PID
            // This means the new window was likely spawned by a different app
            return true;
        }
    }

    return false;
}

bool PlasmaZonesEffect::isDaemonReady(const char* methodName) const
{
    if (!m_daemonServiceRegistered) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- daemon not ready";
        return false;
    }
    return true;
}

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

KWin::EffectWindow* PlasmaZonesEffect::getActiveWindow() const
{
    // Prefer KWin's active (focused) window when it is manageable and on current desktop
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    if (active && active->isOnCurrentActivity() && active->isOnCurrentDesktop() && !active->isMinimized()
        && shouldHandleWindow(active)) {
        return active;
    }
    // Fallback: topmost manageable window on current desktop (e.g. when activeWindow() is
    // null or refers to a dialog/utility we don't handle)
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (w && w->isOnCurrentActivity() && w->isOnCurrentDesktop() && !w->isMinimized() && shouldHandleWindow(w)) {
            return w;
        }
    }
    return nullptr;
}

bool PlasmaZonesEffect::isWindowSticky(KWin::EffectWindow* w) const
{
    return w && w->isOnAllDesktops();
}

void PlasmaZonesEffect::updateWindowStickyState(KWin::EffectWindow* w)
{
    if (!w || !m_daemonServiceRegistered) {
        return;
    }

    QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    bool sticky = isWindowSticky(w);
    // Use fire-and-forget instead of QDBusInterface to avoid synchronous D-Bus
    // introspection. slotWindowAdded → updateWindowStickyState fires for every
    // window during login; QDBusInterface creation blocks the compositor thread
    // for ~25s if the daemon hasn't entered app.exec() yet (daemonReady is
    // emitted before the event loop starts).
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("setWindowSticky"), {windowId, sticky},
                                                   QStringLiteral("setWindowSticky"));
}

} // namespace PlasmaZones
