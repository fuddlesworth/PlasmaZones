// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Sticky (on-all-desktops) window state: the compositor's report of it, and
// the change signal the engines' per-desktop membership pass hangs off.
//
// Its own translation unit because WindowTrackingService.cpp is over the
// file-size ceiling, and this is the one concern in it with an outbound
// signal rather than only storage.

#include <PhosphorPlacement/WindowTrackingService.h>

namespace PhosphorPlacement {

void WindowTrackingService::setWindowSticky(const QString& rawWindowId, bool sticky)
{
    // Canonicalize so sticky state survives the effect-restart-after-class-mutation
    // re-identification skew (issue #628). The daemon seeds the canonical mapping
    // in WindowTrackingAdaptor::setWindowMetadata, so canonicalizeForLookup
    // resolves to the first-seen composite without seeding here.
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // Emit on a real transition only. The effect calls this from
    // windowDesktopsChanged, which fires for every edit to the desktop set
    // (and for every window at login), so an unconditional emit would put the
    // membership pass on a hot path for a value that did not move.
    const auto it = m_windowStickyStates.constFind(windowId);
    const bool had = it != m_windowStickyStates.constEnd();
    if (had && it.value() == sticky) {
        return;
    }
    m_windowStickyStates[windowId] = sticky;
    // A first observation of a NON-sticky window is not a transition: that is
    // the default every window starts at, and announcing it would ask the
    // consumers to undo memberships nothing ever granted.
    if (had || sticky) {
        Q_EMIT windowStickyChanged(windowId, sticky);
    }
}

bool WindowTrackingService::isWindowSticky(const QString& rawWindowId) const
{
    return m_windowStickyStates.value(canonicalizeForLookup(rawWindowId), false);
}

} // namespace PhosphorPlacement
