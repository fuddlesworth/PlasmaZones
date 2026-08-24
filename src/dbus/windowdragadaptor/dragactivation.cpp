// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragactivation.h"
#include <limits>

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

HoldGraceDecision resolveHoldGrace(bool rawHeld, qint64 nowMs, qint64 lastHeldMs, int graceMs)
{
    HoldGraceDecision out;
    if (rawHeld) {
        out.held = true;
        out.nextLastHeldMs = nowMs;
        return out;
    }
    out.nextLastHeldMs = lastHeldMs;
    if (graceMs <= 0 || lastHeldMs < 0) {
        return out;
    }
    const qint64 elapsed = nowMs - lastHeldMs;
    if (elapsed < 0 || elapsed > graceMs) {
        return out;
    }
    out.held = true;
    out.remainingMs = graceMs - elapsed;
    return out;
}

int graceExpiryDueMs(qint64 remainingMs)
{
    // Clamp BEFORE the +1. Adding first would overflow on a qint64 near the
    // maximum and wrap to a negative, which qBound would then floor to 1 — the
    // opposite of the saturation intended. Callers pass a grace remainder, so
    // the huge case is unreachable today; the function is exported, so it is
    // written to be correct anyway rather than to be correct by luck.
    constexpr qint64 kMaxDue = qint64(std::numeric_limits<int>::max());
    const qint64 clamped = qBound<qint64>(qint64(0), remainingMs, kMaxDue - 1);
    return static_cast<int>(qBound<qint64>(qint64(1), clamped + 1, kMaxDue));
}

bool shouldRearmGraceExpiry(bool timerActive, int timerRemainingMs, int dueMs)
{
    return !timerActive || timerRemainingMs > dueMs;
}

} // namespace PlasmaZones
