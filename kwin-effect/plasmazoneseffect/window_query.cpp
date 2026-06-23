// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "window_query.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/WindowTypeEnum.h>

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <sys/types.h> // pid_t

namespace PlasmaZones {

PhosphorProtocol::WindowType windowTypeFor(KWin::EffectWindow* w)
{
    using PhosphorProtocol::WindowType;
    if (!w) {
        return WindowType::Unknown;
    }
    if (w->isDesktop()) {
        return WindowType::Desktop;
    }
    if (w->isDock()) {
        return WindowType::Dock;
    }
    if (w->isOnScreenDisplay()) {
        return WindowType::OnScreenDisplay;
    }
    if (w->isNotification()) {
        return WindowType::Notification;
    }
    if (w->isSplash()) {
        return WindowType::Splash;
    }
    if (w->isTooltip()) {
        return WindowType::Tooltip;
    }
    if (w->isDropdownMenu() || w->isPopupMenu() || w->isMenu()) {
        return WindowType::Menu;
    }
    if (w->isUtility()) {
        return WindowType::Utility;
    }
    if (w->isDialog()) {
        return WindowType::Dialog;
    }
    if (w->isPopupWindow()) {
        return WindowType::Popup;
    }
    if (w->isNormalWindow()) {
        return WindowType::Normal;
    }
    return WindowType::Unknown;
}

PhosphorWindowRules::WindowQuery windowRuleQueryFor(KWin::EffectWindow* w, const QString& screenId, bool isFloating,
                                                    bool isSnapped, const QString& zoneId)
{
    PhosphorWindowRules::WindowQuery query;
    if (!w) {
        return query;
    }
    // PlasmaZones placement state — caller-supplied from the effect's runtime
    // caches (see header). Bools are always engaged when the window exists; the
    // zone UUID is gated on non-empty so a non-snapped window stays a non-match
    // (the engaged-empty foot-gun the string fields below also avoid).
    query.isFloating = isFloating;
    query.isSnapped = isSnapped;
    if (!zoneId.isEmpty()) {
        query.zone = zoneId;
    }
    // Context fields — let a window-domain rule pin screen / desktop / activity
    // (e.g. "red border on monitor 2"). screenId is resolved by the caller (the
    // effect member getWindowScreenId — not reachable from this free helper);
    // virtualDesktop / activity are derived here, mirroring the daemon-side
    // setWindowMetadata derivation in window_identity.cpp (0 / "" = all/unknown).
    query.screenId = screenId;
    // `WindowQuery` fields are `std::optional` — leaving a field disengaged
    // makes a predicate over it inert (returns false). Engaging it with an
    // empty string instead would silently match `Equals ""` and `Regex "^$"`,
    // and would also flip `hasWindow()` from "no window context" to "engaged
    // but empty". Gate each string assignment on a non-empty value to keep
    // the optional / engaged-empty distinction meaningful.
    const QString windowClass = w->windowClass();
    if (!windowClass.isEmpty()) {
        query.windowClass = windowClass;
    }
    const QString title = w->caption();
    if (!title.isEmpty()) {
        query.title = title;
    }
    const QString windowRole = w->windowRole();
    if (!windowRole.isEmpty()) {
        query.windowRole = windowRole;
    }
    // WindowType is always engaged when the window exists — the mapper
    // returns `Unknown` as the explicit "no specific type predicate
    // matched" value. A predicate like `WindowType Equals Normal` then
    // distinguishes `Unknown` from `Normal` correctly without needing
    // a separate "no value" sentinel.
    query.windowType = windowTypeFor(w);

    KWin::Window* kw = w->window();
    if (kw) {
        const QString desktopFile = kw->desktopFileName();
        if (!desktopFile.isEmpty()) {
            query.desktopFile = desktopFile;
        }
        // Canonical appId derivation lives in `PlasmaZonesEffect::getWindowAppId`
        // (window_identity.cpp). Inlined here because `getWindowAppId` is a
        // private member and this builder is a free helper — the actual logic
        // is one call to `PhosphorIdentity::WindowId::normalizeAppId`, the
        // canonical implementation lives in `phosphor-identity`.
        const QString appId = ::PhosphorIdentity::WindowId::normalizeAppId(desktopFile, windowClass);
        if (!appId.isEmpty()) {
            query.appId = appId;
        }
    }
    // pid 0 is KWin's "unknown" sentinel — Wayland surfaces during early
    // lifecycle and X11 windows missing the _NET_WM_PID hint return 0
    // from EffectWindow::pid(). Engaging `query.pid = 0` would let a
    // `Pid Equals 0` predicate silently match every such window. Gate
    // on pid > 0 so the optional stays disengaged in the no-process case.
    // pid_t comes from <sys/types.h>; included explicitly (don't rely on
    // KWin's effect/window.h re-exporting it transitively).
    const pid_t pid = w->pid();
    if (pid > 0) {
        query.pid = static_cast<int>(pid);
    }
    // Window state flags — read live so a rule like "isFullscreen=true ⇒
    // Float" matches the moment we evaluate. Bool fields are always engaged
    // when the window exists; reactive re-evaluation on state-change signals
    // is a separate concern (callers re-run the query at lifecycle / drag
    // events, which is when filter-style predicates are consulted).
    query.isMinimized = w->isMinimized();
    query.isFullscreen = w->isFullScreen();
    query.isSticky = w->isOnAllDesktops();
    // EffectWindow has no direct maximized accessor; the underlying
    // KWin::Window exposes maximizeMode(). MaximizeFull is what the user
    // intuits as "maximized" (both axes); horizontal-only / vertical-only
    // modes are partial states that don't fit a single boolean and would
    // surprise rules that target "is the window maximized."
    if (kw) {
        query.isMaximized = (kw->maximizeMode() == KWin::MaximizeFull);
        // KWin::Window-only accessory / capability flags (not exposed on
        // EffectWindow). Always engaged when the underlying window exists.
        query.skipTaskbar = kw->skipTaskbar();
        query.skipPager = kw->skipPager();
        query.isResizable = kw->isResizable();
        // captionNormal is the raw WM_NAME without the WM-added " — App" suffix
        // that caption() (used for Title above) includes. Gate non-empty like
        // the other string fields so a disengaged optional stays a non-match.
        const QString captionNormal = kw->captionNormal();
        if (!captionNormal.isEmpty()) {
            query.captionNormal = captionNormal;
        }
    }
    // isFocused mirrors the live active-window state so a rule like
    // "isFocused=false ⇒ SetBorderColor(gray)" resolves correctly. The
    // evaluator's per-window match cache is keyed on (windowId, ruleSet
    // revision) — neither moves on focus change — so callers MUST invalidate
    // it on KWin's windowActivated signal (see slotWindowActivated), exactly
    // as they already do for windowClass / desktopFile changes.
    query.isFocused = (w == KWin::effects->activeWindow());
    // Transient / notification family + live frame size — engaged so user rules
    // can match on them (e.g. the built-in "Don't animate small windows"
    // template's `Width < 300` ExcludeAnimations rule). Each predicate is defined
    // to match shouldAnimateWindow's inline animation gate, so a user rule and the
    // config toggle classify the same windows:
    //   transient    → the dialog/utility/popup/menu/tooltip/splash + transient-
    //                   parent bucket the transient toggle filters.
    //   notification → notification / critical-notification / on-screen-display.
    //   width/height → frame extent; a `Width LessThan N` leaf reproduces the
    //                  `frame.width() < N` strict-less-than gate (integer
    //                  truncation is safe at integer thresholds).
    query.isTransient = w->isDialog() || w->isUtility() || w->isPopupWindow() || w->isPopupMenu() || w->isDropdownMenu()
        || w->isTooltip() || w->isMenu() || w->isSplash() || w->transientFor() != nullptr;
    query.isNotification = w->isNotification() || w->isCriticalNotification() || w->isOnScreenDisplay();
    // Stacking / accessory flags read straight off EffectWindow. Always engaged
    // when the window exists, like the other bool flags above.
    query.keepAbove = w->keepAbove();
    query.keepBelow = w->keepBelow();
    query.isModal = w->isModal();
    query.hasDecoration = w->hasDecoration();
    query.skipSwitcher = w->isSkipSwitcher();
    const QRectF frame = w->frameGeometry();
    query.width = static_cast<int>(frame.width());
    query.height = static_cast<int>(frame.height());
    // Frame position — from the same frameGeometry() already read for the size.
    query.positionX = static_cast<int>(frame.x());
    query.positionY = static_cast<int>(frame.y());
    // virtualDesktop: first desktop's 1-based x11 number (0 = all/unknown).
    // activity: first activity UUID (empty = all/unknown). Both mirror the
    // daemon-side setWindowMetadata derivation in window_identity.cpp so a
    // window-domain rule pinning VirtualDesktop / Activity resolves the same
    // way the context cascade does.
    if (kw) {
        const QList<KWin::VirtualDesktop*> desktops = kw->desktops();
        if (!desktops.isEmpty() && desktops.first()) {
            query.virtualDesktop = static_cast<int>(desktops.first()->x11DesktopNumber());
        }
    }
    const QStringList activities = w->activities();
    if (!activities.isEmpty()) {
        query.activity = activities.first();
    }
    return query;
}

} // namespace PlasmaZones
