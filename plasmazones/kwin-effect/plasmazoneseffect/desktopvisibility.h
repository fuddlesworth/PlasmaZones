// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// "Is this window's desktop the one in view WHERE THE WINDOW LIVES" — the
// per-output reading, for the effect-side gates that decide PLACEMENT.
//
// Not every global reading in the effect is wrong. A gate asking "should this
// be painted / hit-tested right now" genuinely wants the session's current
// desktop, and those are left alone. What must use the per-output form is any
// gate that decides whether to move, adopt, release or decorate a window,
// because that decision belongs to the monitor the window is on.
//
// Under per-output virtual desktops (#648, Plasma 6.7) each output shows its
// own desktop, and the global current is only one of them. Asking
// isOnCurrentDesktop() there both over- and under-fires: it answers true for a
// window on an output showing something else because some OTHER output
// switched to the window's desktop, and false for a window that is plainly
// visible because its own output switched while the global current is
// elsewhere. Either way a placement gate acts on the wrong windows.
//
// The per-output helpers above fall back to the global reading when the window
// has no output. When Plasma is not running per-output desktops every output's
// current desktop IS the global one, so these degrade exactly to what they
// replaced — there is no capability to branch on and none is needed.
//
// spansContexts answers a different question, kept here because the same
// placement gates ask it: whether the window belongs to more than one desktop
// or activity context at all, whichever one is in view.
//
// KWin::effects is never null while an effect is loaded: KWin sets it before
// loading any effect and clears it only after unloading them all. The
// per-output helpers' null checks are therefore dead branches, kept as they
// are, and spansContexts carries none.

#include <effect/effecthandler.h>
#include <virtualdesktops.h>

namespace PlasmaZones {

/// The VirtualDesktop shown on this window's own output, or the global current
/// desktop when it has none.
inline KWin::VirtualDesktop* desktopShownOn(const KWin::EffectWindow* w)
{
    if (!KWin::effects) {
        return nullptr;
    }
    KWin::LogicalOutput* const out = w ? w->screen() : nullptr;
    return out ? KWin::effects->currentDesktop(out) : KWin::effects->currentDesktop();
}

/// Whether the window is on the desktop its own output is currently showing.
inline bool isOnOwnOutputCurrentDesktop(const KWin::EffectWindow* w)
{
    if (!w) {
        return false;
    }
    KWin::LogicalOutput* const out = w->screen();
    KWin::VirtualDesktop* const shownHere = out && KWin::effects ? KWin::effects->currentDesktop(out) : nullptr;
    return shownHere ? w->isOnDesktop(shownHere) : w->isOnCurrentDesktop();
}

/// Whether the window is on the desktop @p output is showing, for a decision
/// that belongs to that output rather than to the one the window is on (a tile
/// batch for a screen). Falls back like the two above without an output.
inline bool isOnDesktopShownOn(const KWin::EffectWindow* w, KWin::LogicalOutput* output)
{
    if (!w) {
        return false;
    }
    KWin::VirtualDesktop* const shown = output && KWin::effects ? KWin::effects->currentDesktop(output) : nullptr;
    return shown ? w->isOnDesktop(shown) : w->isOnCurrentDesktop();
}

/// Whether the window lives in more than one desktop or activity context: on
/// every desktop or several, or on every activity or several. KWin state such
/// a window holds (its border, a maximize, its geometry) is global, so it may
/// belong to another context's live session. An empty activity set reads as
/// every activity only while the session has activities at all: without them
/// every window reports none, and reading that as every activity made the
/// desktop-switch and toggle passes skip every window (F554).
inline bool spansContexts(KWin::EffectWindow* w)
{
    if (!w) {
        return false;
    }
    const bool everyActivity = w->activities().isEmpty() && !KWin::effects->currentActivity().isEmpty();
    return w->isOnAllDesktops() || w->desktops().size() > 1 || everyActivity || w->activities().size() > 1;
}

} // namespace PlasmaZones
