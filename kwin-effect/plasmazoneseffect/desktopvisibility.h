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
// Both helpers fall back to the global reading when the window has no output.
// When Plasma is not running per-output desktops every output's current
// desktop IS the global one, so these degrade exactly to what they replaced —
// there is no capability to branch on and none is needed.

#include <effect/effecthandler.h>
#include <virtualdesktops.h>

namespace PlasmaZones {

/// The VirtualDesktop shown on this window's own output, or the global current
/// desktop when it has none. Null only when there is no effects handler.
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

} // namespace PlasmaZones
