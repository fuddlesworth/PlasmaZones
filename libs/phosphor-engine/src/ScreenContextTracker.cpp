// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/ScreenContextTracker.h>

namespace PhosphorEngine {

PlacementStateKey ScreenContextTracker::currentKeyForScreen(const QString& screenId) const
{
    int desktop = m_currentDesktop;
    if (auto perOut = m_screenCurrentDesktop.constFind(screenId); perOut != m_screenCurrentDesktop.constEnd()) {
        desktop = perOut.value();
    }
    if (auto pin = m_screenDesktopOverride.constFind(screenId); pin != m_screenDesktopOverride.constEnd()) {
        desktop = pin.value();
    }
    return PlacementStateKey{screenId, desktop, m_currentActivity};
}

ContextChange ScreenContextTracker::setCurrentDesktop(int desktop)
{
    // KWin desktops are >= 1 and there is no reserved "unset" value, so a spurious
    // 0/negative push would poison m_currentDesktop and every key derived from it.
    // Reject it, symmetric with the same guard in setCurrentDesktopForScreen.
    if (desktop < 1) {
        return {};
    }
    if (desktop == m_currentDesktop) {
        // A same-desktop push still ESTABLISHES the desktop context: the daemon's
        // startup push lands here whenever the session begins on the default
        // desktop. Without recording it, the next genuine change would read as
        // initialization and skip arming.
        m_desktopContextEverSet = true;
        return {};
    }
    // Only arm a switch when a desktop context was already established by a prior
    // call. The daemon pushes the initial desktop BEFORE the first screen update;
    // that first push must NOT read as a switch. There is no reserved "unset"
    // desktop value, so a separate established-flag — not a sentinel comparison —
    // carries "context exists".
    const bool armSwitch = m_desktopContextEverSet;
    m_desktopContextEverSet = true;
    m_currentDesktop = desktop;
    return {true, armSwitch};
}

ContextChange ScreenContextTracker::setCurrentDesktopForScreen(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1) {
        return {};
    }
    const auto perOut = m_screenCurrentDesktop.constFind(screenId);
    if (perOut == m_screenCurrentDesktop.constEnd()) {
        // FIRST per-output push for this screen: establish the pin even when
        // it happens to equal the global desktop. Presence, not value — the
        // old value-compare against the global fell into the same-desktop
        // bail and never inserted the entry, so the screen stayed unpinned
        // and a later GLOBAL setCurrentDesktop dragged its key onto a
        // desktop the screen was not showing (its state then read as
        // untracked until a re-push healed it, order-dependently).
        // `changed` reports whether the key's EFFECTIVE desktop moved (a
        // first push equal to the global changes nothing observable);
        // establishing is never a switch, so armSwitch stays false either
        // way.
        //
        // Not a missed arm in practice, and arming here would be actively
        // wrong. The effect pushes a desktop for EVERY output at daemon
        // (re)registration, bypassing its own dedup, so this branch is consumed
        // for every screen at startup and every genuine user switch takes the
        // arming branch below. Arming whenever the context was already
        // established would make that re-sync report a spurious desktop switch
        // for every screen on every daemon restart — which bumps the global
        // stagger generation and flips a screen leaving the set from a
        // reversible park to a destructive untrack.
        m_desktopContextEverSet = true;
        m_screenCurrentDesktop.insert(screenId, desktop);
        return {desktop != m_currentDesktop, false};
    }
    if (perOut.value() == desktop) {
        // Same per-screen desktop still establishes the context (mirrors the
        // same-desktop branch of setCurrentDesktop for the startup push).
        m_desktopContextEverSet = true;
        return {};
    }
    // PURE context swap — no state migration is the engine's concern. Arm the
    // desktop-switch flag exactly like setCurrentDesktop.
    const bool armSwitch = m_desktopContextEverSet;
    m_desktopContextEverSet = true;
    m_screenCurrentDesktop.insert(screenId, desktop);
    return {true, armSwitch};
}

ContextChange ScreenContextTracker::setCurrentActivity(const QString& activity)
{
    if (activity == m_currentActivity) {
        // A same-activity push still establishes context — but only a NON-EMPTY
        // one ("" == "" is the daemon pushing "activities unavailable", which is
        // no context at all).
        m_activityContextEverSet = m_activityContextEverSet || !activity.isEmpty();
        return {};
    }
    // Only arm when an activity context was already established. The
    // established-flag (not a bare empty-string sentinel on the previous value)
    // keeps the "a" -> "" -> "b" sequence — an activities-service restart hiccup
    // — armed on the "" -> "b" leg.
    const bool armSwitch = m_activityContextEverSet;
    m_activityContextEverSet = true;
    m_currentActivity = activity;
    return {true, armSwitch};
}

void ScreenContextTracker::removeScreensIf(const std::function<bool(const QString&)>& pred)
{
    for (auto it = m_screenDesktopOverride.begin(); it != m_screenDesktopOverride.end();) {
        if (pred(it.key())) {
            it = m_screenDesktopOverride.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_screenCurrentDesktop.begin(); it != m_screenCurrentDesktop.end();) {
        if (pred(it.key())) {
            it = m_screenCurrentDesktop.erase(it);
        } else {
            ++it;
        }
    }
}

void ScreenContextTracker::pruneDesktop(int removedDesktop)
{
    // COUNT-based semantics, deliberately: the daemon's desktopCountChanged
    // handler calls this for every desktop NUMBER now above the new count —
    // it does not know which desktop identity was removed. Values are dropped,
    // never renumbered, and that is consistent with every sibling per-desktop
    // store (engine states, assignment maps), which all treat numbers as
    // positional and rely on the daemon's next desktop pushes to re-establish
    // shifted positions. Renumbering only these maps would desync them from
    // the stores they key into. A surviving in-range pin whose CONTENT
    // shifted heals on the next setCurrentDesktopForScreen push, which KWin
    // triggers when it relocates the screen off the removed desktop.
    //
    // Dropping a per-output entry here is safe, and NOT in tension with
    // releaseScreenOwnership's argument that the same entry must survive a mode
    // leave. The difference is what re-establishes it. On this path the daemon
    // clamps every screen's desktop and pushes the new values BEFORE the count
    // change propagates, so a dropped entry is rewritten immediately. On a mode
    // leave nothing pushes at all, and the global desktop the lookup would fall
    // back to is written once at startup — so there the drop is permanent and
    // merges every output onto one desktop.
    for (auto it = m_screenDesktopOverride.begin(); it != m_screenDesktopOverride.end();) {
        if (it.value() == removedDesktop) {
            it = m_screenDesktopOverride.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_screenCurrentDesktop.begin(); it != m_screenCurrentDesktop.end();) {
        if (it.value() == removedDesktop) {
            it = m_screenCurrentDesktop.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace PhosphorEngine
