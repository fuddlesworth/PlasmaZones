// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragactivation.h"

namespace PlasmaZones {

ActivationDecision resolveActivationActive(bool triggerHeld, bool toggleMode, bool alwaysActiveOnDrag,
                                           bool prevTriggerHeld, bool activationToggled)
{
    ActivationDecision out;
    out.nextActivationToggled = activationToggled;

    if (toggleMode) {
        // Rising edge: trigger went from released to held — flip the latch.
        // The latch represents "the overlay diverges from its default state":
        // default-off in normal mode (so latched-on shows the overlay),
        // default-on in always-active mode (so latched-on hides it). The
        // inversion below maps the latch back to overlay-active.
        if (triggerHeld && !prevTriggerHeld) {
            out.nextActivationToggled = !activationToggled;
        }
        out.active = out.nextActivationToggled;
    } else {
        out.active = triggerHeld;
    }
    out.nextPrevTriggerHeld = triggerHeld;

    // Always-active inversion (#249): the activation list serves dual
    // purpose. With AlwaysActive in the list, the overlay is implicitly on
    // for the whole drag, and the same non-sentinel triggers (configured
    // under "Hold to activate" / "Toggle mode") become deactivate-while-held
    // / toggle-off. The toggle latch survives mode switches so a toggled-on
    // suppression in always-active mode persists across release/press
    // cycles without the deactivation press flipping it back.
    if (alwaysActiveOnDrag) {
        out.active = !out.active;
    }

    return out;
}

} // namespace PlasmaZones
