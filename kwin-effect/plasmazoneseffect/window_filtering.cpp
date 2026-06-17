// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>

#include <optional>

#include "../navigationhandler.h"
#include "window_query.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)
Q_DECLARE_LOGGING_CATEGORY(lcEffectDiag)

namespace {
// Record a filter rejection: when @p out is non-null, store @p reason into it.
// Lets each filter early-exit read as a single `return rejectedBecause(out,
// "...");`, keeping the reason text co-located with the clause that produced
// it — so logWindowDiagnostics never has to re-derive (and risk drifting from)
// the filter logic.
bool rejectedBecause(QString* out, const char* reason)
{
    if (out) {
        *out = QString::fromLatin1(reason);
    }
    return false;
}
} // namespace

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap() const
{
    const auto windows = KWin::effects->stackingOrder();
    QHash<QString, KWin::EffectWindow*> windowMap;
    windowMap.reserve(windows.size());
    for (KWin::EffectWindow* w : windows) {
        // Close-shader grabs keep deleted windows in the stacking order;
        // mapping them would re-pollute the just-scrubbed id caches via
        // getWindowId and let a dying sibling shadow a live window in
        // appId-fallback lookups.
        if (w && !w->isDeleted() && shouldHandleWindow(w)) {
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

bool PlasmaZonesEffect::isWindowSnapped(const QString& windowId) const
{
    return m_navigationHandler->isWindowSnapped(windowId);
}

QString PlasmaZonesEffect::zoneForWindow(const QString& windowId) const
{
    return m_navigationHandler->zoneForWindow(windowId);
}

PhosphorWindowRule::WindowQuery PlasmaZonesEffect::windowRuleQuery(KWin::EffectWindow* w) const
{
    const QString windowId = getWindowId(w);
    return windowRuleQueryFor(w, getWindowScreenId(w), isWindowFloating(windowId), isWindowSnapped(windowId),
                              zoneForWindow(windowId));
}

PhosphorWindowRule::ResolvedActions PlasmaZonesEffect::resolveWindowRuleActions(KWin::EffectWindow* w,
                                                                                const QString& windowId) const
{
    const PhosphorWindowRule::RuleEvaluator& evaluator = m_shaderManager.animationRuleEvaluator();
    // An empty windowId can't key the per-window cache; nothing to resolve.
    if (windowId.isEmpty()) {
        return {};
    }
    // Cache hit → skip the ≈30-accessor windowRuleQuery(w) build entirely. The
    // cached verdict already reflects whatever query produced it, and resolveCached
    // ignores the query on a hit anyway.
    if (std::optional<PhosphorWindowRule::ResolvedActions> cached = evaluator.resolveCachedIfPresent(windowId)) {
        return std::move(*cached);
    }
    // Miss → build the query once and resolve (caching the result). A windowless
    // query (sub-surface / drop shadow / proxy) can't fill any slot; return empty
    // actions WITHOUT caching, so the paint hot path doesn't churn the cache for it.
    const PhosphorWindowRule::WindowQuery query = windowRuleQuery(w);
    if (!query.hasWindow()) {
        return {};
    }
    return evaluator.resolveCached(windowId, query);
}

bool PlasmaZonesEffect::isStructurallyUnmanageableWindowType(KWin::EffectWindow* w, QString* rejectReason) const
{
    // Single source of truth for the window-TYPE rejection set shared by
    // shouldHandleWindow() (snap/zone filter) and notifyWindowActivated()
    // (focus-tracking filter). Both must reject the exact same structural
    // types: a window kind that can never legitimately be a snap/autotile
    // target must also never be reported as the active window, or the daemon's
    // focus tracking gets pinned to a popup. Discussion #461 item 11 (Steam
    // image popups) was a missed sync between two hand-maintained copies of
    // this list — keeping it in one function makes that class of drift
    // unrepresentable.
    //
    // isTileableWindow() deliberately keeps its own, narrower list (it gates
    // on !isNormalWindow()) and is NOT folded in here.

    // Null is structurally unmanageable. Both current callers null-check before
    // reaching here, so this is a defensive guard that keeps the precondition
    // enforced rather than merely documented for any future caller.
    if (!w) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("null window");
        }
        return true;
    }

    // Special / non-manageable window types (inherently effect-side — KWin metadata).
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("special/desktop/dock/fullscreen/skipSwitcher window type");
        }
        return true;
    }

    // Transient / dialog / menu family. transientFor() catches child surfaces
    // that Electron/CEF apps (Steam, Discord, VS Code) spawn for image
    // previews, context menus and popups: these frequently fail to report an
    // accurate KWin window type (isDialog/isPopupWindow stay false) but always
    // set the transient-parent relationship. Without this the popup passes the
    // filter and gets snapped to a zone (discussion #461 item 11).
    if (w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isCriticalNotification()
        || w->isOnScreenDisplay() || w->isModal() || w->isPopupWindow() || w->isPopupMenu() || w->isDropdownMenu()
        || w->isMenu() || w->isTooltip() || w->transientFor()) {
        if (rejectReason) {
            // Coarse reason — logWindowDiagnostics() dumps every flag in this
            // clause individually, so the caller can pinpoint which one fired.
            *rejectReason =
                QStringLiteral("transient/dialog/utility/splash/notification/osd/modal/popup/menu/tooltip window type");
        }
        return true;
    }

    return false;
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w, QString* rejectReason) const
{
    if (rejectReason) {
        rejectReason->clear();
    }

    if (!w) {
        return rejectedBecause(rejectReason, "null window");
    }

    // Never snap our own overlay/editor windows (but allow the settings app).
    // Shared with the FFM stacking-order walk — see isOwnOverlayClass().
    const QString windowClass = w->windowClass();
    if (isOwnOverlayClass(windowClass)) {
        return rejectedBecause(rejectReason, "own overlay/editor window class");
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (isXdgDesktopPortalSurface(windowClass)) {
        return rejectedBecause(rejectReason, "xdg-desktop-portal window class");
    }

    // Plasma shell layer-shell surfaces — see isPlasmaShellSurface() for rationale.
    if (isPlasmaShellSurface(windowClass)) {
        return rejectedBecause(rejectReason, "Plasma shell layer-shell surface");
    }

    // Check user-authored / migrated Exclude rules (needed for drag gating —
    // daemon also enforces these for keyboard navigation, but the effect
    // must filter for drag operations and lifecycle reporting).
    // `m_snappingExclusionRuleSet` mirrors the Exclude-shaped slice of the
    // unified WindowRule store, refreshed on every rulesChanged via
    // loadWindowRuleAnimationsFromDbus (see shader_transitions.cpp). The
    // `!isEmpty()` fast path keeps a no-exclusions user at two pointer
    // reads — same cost as the prior list-derived check.
    if (!m_snappingExclusionRuleSet.isEmpty()) {
        if (m_snappingExclusionEvaluator.resolve(windowRuleQuery(w)).isExcluded()) {
            return rejectedBecause(rejectReason, "user exclusion rule match");
        }
    }

    // Skip structural / transient / dialog / menu window types. The predicate
    // is shared verbatim with notifyWindowActivated() so the two filters can
    // never drift — see isStructurallyUnmanageableWindowType().
    if (isStructurallyUnmanageableWindowType(w, rejectReason)) {
        return false;
    }

    // Keep-above overlays (Spectacle, color pickers, screen rulers, screenshot
    // tools that linger after capture) shouldn't be snapped to a zone — same
    // rationale as isTileableWindow's keep-above gate.
    if (w->keepAbove()) {
        return rejectedBecause(rejectReason, "keep-above window");
    }

    return true;
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

    // Lazy per-window query — built at most once across the rule-override
    // gate AND the exclusion gate below. Both gates take the same full-
    // context WindowQuery (AppId / WindowClass / Title / WindowRole /
    // DesktopFile / WindowType / Pid / state flags), and `windowRuleQueryFor`
    // walks ~10 KWin accessors plus several QString copies — wasted work
    // when both rule sets fire. The std::optional memoises so the function
    // pays at most one build no matter how many gates consult it, while
    // the `!isEmpty()` fast paths below keep the no-rules user's cost at
    // two pointer reads (query never built).
    std::optional<PhosphorWindowRule::WindowQuery> cachedQuery;
    auto query = [&]() -> const PhosphorWindowRule::WindowQuery& {
        if (!cachedQuery) {
            cachedQuery = windowRuleQuery(w);
        }
        return *cachedQuery;
    };

    // Rule-override path. ANY animation rule whose match expression matches
    // the window signals deliberate user intent to animate this app — the
    // event-scoped slot (shader vs timing, which eventPath) is NOT narrowed
    // here because the user's act of creating a rule whose match resolves
    // for this window is itself the opt-in signal. `hasAnyMatch` is the
    // event-agnostic query for exactly this — a yes/no over the bound
    // animation rule set without allocating a ResolvedActions.
    //
    // `m_shaderManager.animationRuleSet()` is filtered to OverrideAnimation*
    // /SetOpacity rules at admission (shader_transitions.cpp's
    // `isEffectRuleAction` loop), so this `hasAnyMatch` never surfaces
    // a rule whose actions are EXCLUSIVELY `ExcludeAnimations` — those
    // are routed through the exclusion gate below. A mixed-action rule
    // carrying BOTH an OverrideAnimation* action AND an `ExcludeAnimations`
    // action would be admitted to `animationRuleSet` (it matches
    // `isEffectRuleAction`) and the rule-override path would short-circuit
    // before the exclusion gate fires. Mixed shapes like that are
    // ambiguous user intent — they don't appear in the v3→v4 migration
    // output and the rule editor doesn't author them today.
    if (!m_shaderManager.animationRuleSet().isEmpty()) {
        if (m_shaderManager.animationRuleEvaluator().hasAnyMatch(query())) {
            return true;
        }
    }

    // Notification and OSD surfaces — excluded by default via the
    // Window Filtering toggle (animationExcludeNotificationsAndOsd).
    // Most shell notifications/OSDs are layer-shell surfaces already
    // rejected by the structural block above; this catches the
    // remainder — notification windows some apps spawn as ordinary
    // toplevels. Placed after the rule-override so a class-targeted
    // rule can still re-enable them, mirroring the transient filter.
    if (m_animationExcludeNotificationsAndOsd
        && (w->isNotification() || w->isCriticalNotification() || w->isOnScreenDisplay())) {
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

    // User-configured exclusion lists — routed through the unified
    // RuleEvaluator over the animation exclusion rule set, the same path
    // `shouldHandleWindow` uses for the snapping exclusions. Both filter
    // sets walk the full WindowQuery match expression (AppId / WindowClass /
    // Title / WindowRole / DesktopFile / WindowType / Pid / state flags),
    // so the two are in lockstep on match semantics even though their rule
    // sets are independent. The `!isEmpty()` fast path keeps a no-exclusions
    // user free.
    if (!m_animationExclusionRuleSet.isEmpty()) {
        if (m_animationExclusionEvaluator.resolve(query()).isExcluded()) {
            return false;
        }
    }

    return true;
}

bool PlasmaZonesEffect::isTileableWindow(KWin::EffectWindow* w, QString* rejectReason) const
{
    if (rejectReason) {
        rejectReason->clear();
    }

    if (!w) {
        return rejectedBecause(rejectReason, "null window");
    }

    // Reject menus, popups, tooltips, modals, and transient children.
    // Electron apps (Vesktop, VS Code, Discord) create separate windows
    // for context menus and dropdowns that pass shouldHandleWindow() but
    // must never enter the autotile tree.
    if (!w->isNormalWindow() || w->isModal() || w->isPopupWindow() || w->isDropdownMenu() || w->isPopupMenu()
        || w->isTooltip() || w->isMenu() || w->transientFor()) {
        // Coarse reason — logWindowDiagnostics() dumps every flag individually.
        return rejectedBecause(rejectReason, "non-normal/modal/popup/dropdown/menu/tooltip/transient window type");
    }
    // Reject keep-above windows — overlay/utility tools (Spectacle, color
    // pickers, screen rulers, etc.) set keep-above and should not enter the
    // autotile tree or receive auto-focus. Without this guard, opening
    // Spectacle while focusNewWindows is enabled disrupts the tiled layout.
    if (w->keepAbove()) {
        return rejectedBecause(rejectReason, "keep-above window");
    }
    return true;
}

void PlasmaZonesEffect::logWindowDiagnostics(KWin::EffectWindow* w, const char* context) const
{
    if (!w) {
        qCDebug(lcEffectDiag) << "[window-diag]" << context << "— null window";
        return;
    }

    QString handleReason;
    QString tileReason;
    const bool handle = shouldHandleWindow(w, &handleReason);
    const bool tileable = isTileableWindow(w, &tileReason);

    KWin::Window* kw = w->window();

    qCDebug(lcEffectDiag) << "[window-diag]" << context << "— class:" << w->windowClass() << "role:" << w->windowRole()
                          << "caption:" << w->caption() << "desktopFile:" << (kw ? kw->desktopFileName() : QString())
                          << "pid:" << w->pid();
    qCDebug(lcEffectDiag) << "[window-diag]   verdict — shouldHandleWindow:" << handle
                          << (handle ? QString() : QStringLiteral("[rejected: %1]").arg(handleReason))
                          << "| isTileableWindow:" << tileable
                          << (tileable ? QString() : QStringLiteral("[rejected: %1]").arg(tileReason));
    qCDebug(lcEffectDiag) << "[window-diag]   type — normal:" << w->isNormalWindow()
                          << "special:" << w->isSpecialWindow() << "dialog:" << w->isDialog()
                          << "utility:" << w->isUtility() << "splash:" << w->isSplash() << "modal:" << w->isModal()
                          << "toolbar:" << w->isToolbar() << "menu:" << w->isMenu()
                          << "popupWindow:" << w->isPopupWindow() << "popupMenu:" << w->isPopupMenu()
                          << "dropdownMenu:" << w->isDropdownMenu() << "tooltip:" << w->isTooltip()
                          << "notification:" << w->isNotification()
                          << "criticalNotification:" << w->isCriticalNotification()
                          << "onScreenDisplay:" << w->isOnScreenDisplay() << "appletPopup:" << w->isAppletPopup()
                          << "desktop:" << w->isDesktop() << "dock:" << w->isDock();
    qCDebug(lcEffectDiag) << "[window-diag]   state — managed:" << w->isManaged() << "x11:" << w->isX11Client()
                          << "wayland:" << w->isWaylandClient() << "fullScreen:" << w->isFullScreen()
                          << "minimized:" << w->isMinimized() << "skipSwitcher:" << w->isSkipSwitcher()
                          << "keepAbove:" << w->keepAbove() << "hasDecoration:" << w->hasDecoration()
                          << "onCurrentDesktop:" << w->isOnCurrentDesktop()
                          << "onCurrentActivity:" << w->isOnCurrentActivity()
                          << "onAllDesktops:" << w->isOnAllDesktops();
    qCDebug(lcEffectDiag) << "[window-diag]   geometry — frame:" << w->frameGeometry()
                          << "minSize:" << (kw && !kw->isInternal() ? kw->minSize() : QSizeF());

    if (KWin::EffectWindow* parent = w->transientFor()) {
        qCDebug(lcEffectDiag) << "[window-diag]   transientFor — YES — parent class:" << parent->windowClass()
                              << "caption:" << parent->caption() << "normal:" << parent->isNormalWindow()
                              << "special:" << parent->isSpecialWindow() << "pid:" << parent->pid()
                              << "frame:" << parent->frameGeometry();
    } else {
        qCDebug(lcEffectDiag) << "[window-diag]   transientFor — none";
    }
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
        if (!other || other->isDeleted()) {
            // A close-grabbed dying window of the same class (quit-and-relaunch,
            // app auto-restart) must not suppress the new instance's snap restore.
            continue;
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
        // Skip close-grabbed dying windows — a topmost close animation must
        // not become the navigation / snap-assist anchor.
        if (w && !w->isDeleted() && w->isOnCurrentActivity() && w->isOnCurrentDesktop() && !w->isMinimized()
            && shouldHandleWindow(w)) {
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
