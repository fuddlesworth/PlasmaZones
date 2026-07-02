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
    const int previous = m_screenCurrentDesktop.value(screenId, m_currentDesktop);
    if (previous == desktop) {
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
