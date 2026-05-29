// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "window_query.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/WindowTypeEnum.h>

#include <effect/effecthandler.h>
#include <window.h>

namespace PlasmaZones {

namespace {

/// Map KWin's overlapping window-type predicates onto exactly one
/// PhosphorProtocol::WindowType. Ordered most-specific-first because a window
/// can satisfy several predicates at once (a modal dialog is both a dialog and
/// modal — modality is orthogonal state, deliberately not a WindowType).
/// Mirrors the same precedence as the sister helper in window_identity.cpp
/// (used for D-Bus metadata pushes); kept independent because the consumers
/// differ — one feeds the rule evaluator, the other serialises over D-Bus.
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

} // namespace

PhosphorWindowRule::WindowQuery windowRuleQueryFor(KWin::EffectWindow* w)
{
    PhosphorWindowRule::WindowQuery query;
    if (!w) {
        return query;
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
    }
    return query;
}

} // namespace PlasmaZones
