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

bool windowIsTransient(KWin::EffectWindow* w)
{
    if (!w) {
        return false;
    }
    return w->isDialog() || w->isUtility() || w->isPopupWindow() || w->isPopupMenu() || w->isDropdownMenu()
        || w->isTooltip() || w->isMenu() || w->isSplash() || w->transientFor() != nullptr;
}

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

QString centreScreenOrientation(KWin::EffectWindow* w)
{
    if (!w) {
        return {};
    }
    // Position-resolved output (screenAt on the frame centre), NOT w->screen():
    // KWin can assign a window the wrong one of two identical-model outputs
    // (discussion #724).
    const QPointF centreF = w->frameGeometry().center();
    const auto* output = KWin::effects->screenAt(QPoint(qRound(centreF.x()), qRound(centreF.y())));
    if (!output) {
        return {};
    }
    const QRect g = output->geometry();
    if (!g.isValid()) {
        return {};
    }
    return g.height() > g.width() ? QStringLiteral("portrait") : QStringLiteral("landscape");
}

PhosphorRules::WindowQuery ruleQueryFor(KWin::EffectWindow* w, const QString& screenId, bool isFloating, bool isSnapped,
                                        bool isTiled, const QString& zoneId)
{
    PhosphorRules::WindowQuery query;
    if (!w) {
        return query;
    }
    // PlasmaZones placement state — caller-supplied from the effect's runtime
    // caches (see header). Bools are always engaged when the window exists; the
    // zone UUID is gated on non-empty so a non-snapped window stays a non-match
    // (the engaged-empty foot-gun the string fields below also avoid).
    query.isFloating = isFloating;
    query.isSnapped = isSnapped;
    query.isTiled = isTiled;
    if (!zoneId.isEmpty()) {
        query.zone = zoneId;
    }
    // Engine mode (context field) — derived from the snapped / tiled state above
    // so a per-mode rule (`Mode Equals "tiling"`) resolves this window's border /
    // title / colour the same way the daemon resolves its per-mode gaps. A
    // floating (unmanaged) window has no mode, so query.mode is left empty —
    // which is NOT inert in both polarities: `mode` is a plain QString, so
    // valueForField hands the evaluator an ENGAGED empty value. A positive leaf
    // (`Mode Equals "tiling"`) correctly never matches, but a negated one
    // (`None{Mode Equals "tiling"}`) matches every floating window precisely
    // because the inner leaf failed. That is the intended reading of "not
    // tiled" here, so the field is stamped rather than withheld — unlike
    // ActiveLayout, whose empty stamp carries no such meaning. Scrolling is
    // indistinguishable from tiling at
    // this level (strip windows ride the tile pipeline); the effect's
    // ruleQuery() funnel re-stamps "scrolling" from the per-screen engine
    // discriminator, which this free helper cannot reach.
    if (isTiled) {
        query.mode = QStringLiteral("tiling");
    } else if (isSnapped) {
        query.mode = QStringLiteral("snapping");
    }
    // Context fields — let a window-domain rule pin screen / desktop / activity
    // (e.g. "red border on monitor 2"). screenId is resolved by the caller (the
    // effect member getWindowScreenId — not reachable from this free helper);
    // virtualDesktop / activity are derived here, mirroring the daemon-side
    // setWindowMetadata derivation in window_identity.cpp (0 / "" = all/unknown).
    query.screenId = screenId;
    // Orientation of the window's screen ("portrait" when taller than wide), so a
    // window rule can match ScreenOrientation the same way a context rule does.
    // Only derived from the frame centre when the caller has no screen id to
    // offer: a centre-resolved output is wrong for a scroll strip's off-screen
    // parked windows, so a caller that HAS an id (the effect's ruleQuery()
    // funnel) derives the field from that id instead and only falls back to
    // this helper when the id resolves to no output.
    if (screenId.isEmpty()) {
        if (const QString orientation = centreScreenOrientation(w); !orientation.isEmpty()) {
            query.screenOrientation = orientation;
        }
    }
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
        // NO INVALIDATION EDGE, deliberately: these flags rarely change
        // after map (X11 clients and KWin window rules can flip them), and
        // no change-signal is connected for them — a verdict scoped on one
        // refreshes at the next natural invalidation (focus, placement,
        // class swap, rule edit) rather than on its own edge. Accepted
        // staleness; wiring five rarely-firing signals was judged not worth
        // the connection overhead per window.
        query.skipTaskbar = kw->skipTaskbar();
        query.skipPager = kw->skipPager();
        query.isResizable = kw->isResizable();
        query.isMovable = kw->isMovable();
        query.isMaximizable = kw->isMaximizable();
        // captionNormal is the raw WM_NAME without the WM-added " — App" suffix
        // that caption() (used for Title above) includes. Gate non-empty like
        // the other string fields so a disengaged optional stays a non-match.
        const QString captionNormal = kw->captionNormal();
        if (!captionNormal.isEmpty()) {
            query.captionNormal = captionNormal;
        }
    }
    // isFocused mirrors the live active-window state so a rule like
    // "isFocused=false ⇒ SetBorderColorActive(gray)" resolves correctly. The
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
    query.isTransient = windowIsTransient(w);
    query.isNotification = w->isNotification() || w->isCriticalNotification() || w->isOnScreenDisplay();
    // Stacking / accessory flags read straight off EffectWindow. Always engaged
    // when the window exists, like the other bool flags above.
    //
    // keepAbove / keepBelow are substituted with the window's pre-rule
    // snapshot by the caller's applyOwnLayerFlags pass when a SetWindowLayer
    // rule owns the window — see window_filtering.cpp — so a `WHEN KeepAbove`
    // predicate never reads its own rule's effect. A MANUAL keep-above toggle
    // (window menu) has no cache-invalidation edge, deliberately: routing it
    // through invalidateRuleCacheForStateChange would re-run the layer
    // reconcile and instantly re-assert the rule over the user's toggle,
    // which the reconcile's own docs rule out. A verdict scoped on these
    // flags refreshes at the next natural invalidation instead.
    query.keepAbove = w->keepAbove();
    query.keepBelow = w->keepBelow();
    query.isModal = w->isModal();
    // KNOWN SELF-FEED HAZARD, unresolved: hasDecoration is rule OUTPUT
    // (SetHideTitleBar reaches KWin::Window::setNoBorder through the
    // decoration bridge) stamped back in as rule INPUT with no snapshot
    // substitution — the exact shape applyOwnLayerFlags neutralises for the
    // layer flags. Whether it actually oscillates depends on whether
    // setNoBorder(true) flips EffectWindow::hasDecoration(), which is
    // KWin-version behaviour this code cannot verify statically. Do not
    // scope a SetHideTitleBar rule on HasDecoration; a substitution needs a
    // pre-rule snapshot sourced from the DecorationManager's restore state
    // if this ever bites in practice.
    query.hasDecoration = w->hasDecoration();
    query.skipSwitcher = w->isSkipSwitcher();
    const QRectF frame = w->frameGeometry();
    query.width = static_cast<int>(frame.width());
    query.height = static_cast<int>(frame.height());
    // Frame position — from the same frameGeometry() already read for the size.
    query.positionX = static_cast<int>(frame.x());
    query.positionY = static_cast<int>(frame.y());
    // virtualDesktop: first non-null desktop's 1-based x11 number (0 = all/
    // unknown). activity: first activity UUID (empty = all/unknown). Both
    // mirror the METADATA derivation this effect pushes to the daemon
    // (window_identity.cpp), so the two ends describe a window identically.
    //
    // They do NOT mirror the daemon's own CONTEXT cascade, and a rule author
    // should not expect them to: buildRuleQueryForWindow
    // (src/dbus/windowtrackingadaptor/rules.cpp) resolves the governing
    // desktop / activity through WindowContext::effectiveDesktop /
    // effectiveActivity, which prefer the SCREEN's current desktop when a
    // window spans several and handle sticky windows. This builder sees only
    // the window, so for a spanning or sticky window it reports the first
    // entry where the daemon reports the one actually on screen. Both values
    // are always engaged (the fields are a plain int and QString, not
    // optionals), so a negated VirtualDesktop / Activity leaf matches the
    // unknown 0 / "" case here too.
    if (kw) {
        const QList<KWin::VirtualDesktop*> desktops = kw->desktops();
        for (const KWin::VirtualDesktop* vd : desktops) {
            if (vd) {
                query.virtualDesktop = static_cast<int>(vd->x11DesktopNumber());
                break;
            }
        }
    }
    const QStringList activities = w->activities();
    if (!activities.isEmpty()) {
        query.activity = activities.first();
    }
    // activeLayout is left at the empty string HERE and re-stamped by the
    // caller: ruleQuery (window_filtering.cpp) resolves it from the daemon's
    // per-screen map, which this window-only builder has no handle on. It is
    // not an inert default — the field is a plain QString, so an empty value
    // still resolves ENGAGED and a negated leaf would match every window —
    // which is why the map's bring-up window is covered by holding
    // ActiveLayout rules out of the evaluator entirely (see
    // TilingHandler::activeLayoutsSeeded).
    //
    // DELIBERATELY NOT STAMPED effect-side:
    //  - tiledWindowCount: optional and left disengaged, so a positive count
    //    leaf is inert here by design; the context resolvers exclude negated
    //    references structurally for the same absence-polarity reason.
    return query;
}

} // namespace PlasmaZones
