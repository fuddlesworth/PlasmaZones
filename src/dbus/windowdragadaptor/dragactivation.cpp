// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragactivation.h"

namespace PlasmaZones {

ActivationDecision resolveActivationActive(bool triggerHeld, bool deactivateHeld, bool toggleMode,
                                           bool alwaysActiveOnDrag, bool prevTriggerHeld, bool activationToggled)
{
    ActivationDecision out;
    out.nextActivationToggled = activationToggled;

    if (toggleMode) {
        // Rising edge: trigger went from released to held — flip the latch.
        if (triggerHeld && !prevTriggerHeld) {
            out.nextActivationToggled = !activationToggled;
        }
        out.active = out.nextActivationToggled;
    } else {
        out.active = triggerHeld;
    }
    out.nextPrevTriggerHeld = triggerHeld;

    // Deactivation override (#249): only consulted in always-active mode.
    // Hold-to-activate users already have a deactivate affordance (release
    // the activation modifier), so the deactivation list is a no-op there
    // and the UI hides the row to match.
    if (alwaysActiveOnDrag && deactivateHeld) {
        out.active = false;
    }

    return out;
}

} // namespace PlasmaZones
